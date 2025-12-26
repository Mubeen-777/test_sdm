// File: source/modules/websocket_bridge.cpp
#include <websocketpp/config/asio.hpp>
#include <websocketpp/server.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <opencv2/opencv.hpp>
#include <chrono>
#include <functional>
#include <memory>
#include <vector>
#include <cstdint>
#include <string>
#include "camera.h"
#include "lane_detector.h"
#include "LocationManager.h"
#include "../core/DatabaseManager.h"

using namespace std;
using json = nlohmann::json;
using server = websocketpp::server<websocketpp::config::asio>;

// Simple base64 implementation
static const std::string base64_chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

std::string base64_encode(unsigned char const *bytes_to_encode, size_t in_len)
{
    std::string ret;
    int i = 0;
    int j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];

    while (in_len--)
    {
        char_array_3[i++] = *(bytes_to_encode++);
        if (i == 3)
        {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for (i = 0; i < 4; i++)
                ret += base64_chars[char_array_4[i]];
            i = 0;
        }
    }

    if (i)
    {
        for (j = i; j < 3; j++)
            char_array_3[j] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;

        for (j = 0; j < i + 1; j++)
            ret += base64_chars[char_array_4[j]];

        while (i++ < 3)
            ret += '=';
    }

    return ret;
}

class SmartDriveBridge
{
private:
    server ws_server;
    thread server_thread;
    atomic<bool> running;

    // Camera system
    unique_ptr<CameraManager> camera;
    unique_ptr<UltraFastLaneDetector> lane_detector;
    thread camera_thread;
    atomic<bool> camera_running;

    // Location
    unique_ptr<LocationManager> location_manager;

    // Client connections
    using connection_hdl = websocketpp::connection_hdl;
    struct connection_data
    {
        json info;
        time_t connected_at;
    };

    map<connection_hdl, connection_data, owner_less<connection_hdl>> clients;
    mutex clients_mutex;

    // Video buffer
    queue<string> video_buffer;
    mutex video_mutex;
    condition_variable video_cv;

    // Data with mutex for thread safety
    struct LiveData
    {
        atomic<double> speed{0};
        atomic<double> acceleration{0};
        atomic<double> safety_score{1000};
        atomic<int> rapid_accel_count{0};
        atomic<int> hard_brake_count{0};
        atomic<int> lane_departures{0};
        atomic<bool> trip_active{false};
        atomic<uint64_t> trip_id{0};

        // String needs protection
        mutex lane_status_mutex;
        string lane_status = "CENTERED";

        void set_lane_status(const string &status)
        {
            lock_guard<mutex> lock(lane_status_mutex);
            lane_status = status;
        }

        string get_lane_status()
        {
            lock_guard<mutex> lock(lane_status_mutex);
            return lane_status;
        }
    } live_data;

    // Database connection
    DatabaseManager *db_manager = nullptr;

public:
    // Add this function to SmartDriveBridge class in websocket_bridge.cpp
    void send_video_frame(const string &base64_frame)
    {
        json video_msg;
        video_msg["type"] = "video_frame";
        video_msg["data"] = base64_frame;
        video_msg["timestamp"] = time(nullptr);

        broadcast_message(video_msg.dump());
    }

    // Update camera_loop function to send frames:
    void camera_loop()
    {
        cv::Mat frame;
        int frame_count = 0;
        auto last_frame_time = chrono::steady_clock::now();

        while (camera_running.load())
        {
            try
            {
                if (camera && camera->grabFrame(frame) && !frame.empty())
                {
                    frame_count++;

                    // Process lane detection
                    process_frame(frame, 0);

                    // Encode frame as JPEG
                    vector<uchar> buffer;
                    cv::imencode(".jpg", frame, buffer, {cv::IMWRITE_JPEG_QUALITY, 70});

                    if (!buffer.empty())
                    {
                        // Convert to base64
                        string base64_frame = base64_encode(buffer.data(), buffer.size());

                        // Send via WebSocket
                        send_video_frame(base64_frame);
                    }
                }
            }
            catch (const exception &e)
            {
                cerr << "Camera loop error: " << e.what() << endl;
                this_thread::sleep_for(chrono::seconds(1));
            }
        }
    }
    SmartDriveBridge() : running(false), camera_running(false), db_manager(nullptr)
    {
        // Initialize WebSocket server
        ws_server.init_asio();
        ws_server.set_reuse_addr(true);

        ws_server.set_open_handler(bind(&SmartDriveBridge::on_open, this, placeholders::_1));
        ws_server.set_close_handler(bind(&SmartDriveBridge::on_close, this, placeholders::_1));
        ws_server.set_message_handler(bind(&SmartDriveBridge::on_message, this, placeholders::_1, placeholders::_2));

        // Set up error handler
        ws_server.set_fail_handler([this](connection_hdl hdl)
                                   {
            lock_guard<mutex> lock(clients_mutex);
            clients.erase(hdl); });
    }

    ~SmartDriveBridge()
    {
        stop();
        if (db_manager)
        {
            delete db_manager;
        }
    }

    bool initialize()
    {
        cout << "=== Smart Drive WebSocket Bridge ===" << endl;

        try
        {
            // Initialize database
            db_manager = new DatabaseManager("compiled/SDM.db");
            if (!db_manager->open())
            {
                cerr << "Failed to open database" << endl;
            }

            cout << "Initializing camera system..." << endl;

            // Initialize camera
            camera = make_unique<CameraManager>();
            CameraManager::CameraConfig cam_cfg;
            cam_cfg.source = CameraManager::findDroidCamDevice();
            cam_cfg.width = 640;
            cam_cfg.height = 480;
            cam_cfg.fps = 30;
            cam_cfg.type = CameraManager::CAMERA_V4L2;

            if (!camera->initialize(cam_cfg))
            {
                cerr << "Failed to initialize camera! Using fallback..." << endl;
                camera.reset(nullptr);
            }
            else
            {
                cout << "✓ Camera initialized" << endl;
            }

            // Initialize lane detector
            if (camera)
            {
                lane_detector = make_unique<UltraFastLaneDetector>();
                if (!lane_detector->initialize(""))
                {
                    cerr << "Failed to initialize lane detector" << endl;
                    lane_detector.reset(nullptr);
                }
                else
                {
                    cout << "✓ Lane detector initialized" << endl;
                }
            }

            // Initialize location manager
            location_manager = make_unique<LocationManager>(1000);

            cout << "✓ Bridge initialized successfully" << endl;
            return true;
        }
        catch (const exception &e)
        {
            cerr << "Initialization error: " << e.what() << endl;
            return false;
        }
    }

    void start(uint16_t port = 8081)
    {
        if (running)
            return;

        try
        {
            running = true;

            // Start WebSocket server
            ws_server.listen(port);
            ws_server.start_accept();

            server_thread = thread([this, port]()
                                   {
                cout << "WebSocket server starting on port " << port << endl;
                try {
                    ws_server.run();
                } catch (const exception& e) {
                    cerr << "WebSocket server error: " << e.what() << endl;
                } });

            // Start camera processing if available
            if (camera)
            {
                camera_running = true;
                camera_thread = thread(&SmartDriveBridge::camera_loop, this);
                cout << "✓ Camera processing started" << endl;
            }

            // Start location updates
            if (location_manager)
            {
                location_manager->setLocationCallback([this](LocationData loc)
                                                      { update_location(loc); });
                location_manager->start();
                cout << "✓ Location tracking started" << endl;
            }

            cout << "✓ Bridge server started on ws://localhost:" << port << endl;
        }
        catch (const exception &e)
        {
            cerr << "Failed to start bridge: " << e.what() << endl;
            running = false;
        }
    }

    void stop()
    {
        if (!running)
            return;

        running = false;
        camera_running = false;

        // Stop location manager
        if (location_manager)
        {
            location_manager->stop();
        }

        // Stop camera thread
        if (camera_thread.joinable())
        {
            camera_thread.join();
        }

        // Stop WebSocket server
        try
        {
            ws_server.stop_listening();

            // Close all connections
            {
                lock_guard<mutex> lock(clients_mutex);
                for (auto &client : clients)
                {
                    try
                    {
                        ws_server.close(client.first, websocketpp::close::status::going_away, "Server shutdown");
                    }
                    catch (...)
                    {
                        // Ignore errors during shutdown
                    }
                }
                clients.clear();
            }

            ws_server.stop();

            if (server_thread.joinable())
            {
                server_thread.join();
            }
        }
        catch (const exception &e)
        {
            cerr << "Error stopping server: " << e.what() << endl;
        }

        // Close camera
        if (camera)
        {
            camera->release();
        }

        cout << "Bridge stopped" << endl;
    }

    bool is_running() const { return running; }

private:
    void on_open(connection_hdl hdl)
    {
        lock_guard<mutex> lock(clients_mutex);

        connection_data data;
        data.connected_at = time(nullptr);

        // Fixed JSON construction
        data.info = json::object();
        data.info["type"] = "dashboard";
        data.info["connected_at"] = data.connected_at;

        clients[hdl] = data;

        cout << "New client connected. Total: " << clients.size() << endl;

        // Send initial data
        send_initial_data(hdl);
    }

    void on_close(connection_hdl hdl)
    {
        lock_guard<mutex> lock(clients_mutex);
        clients.erase(hdl);
        cout << "Client disconnected. Remaining: " << clients.size() << endl;
    }

    void on_message(connection_hdl hdl, server::message_ptr msg)
    {
        try
        {
            string payload = msg->get_payload();

            if (payload.empty())
            {
                send_error(hdl, "Empty message");
                return;
            }

            json data = json::parse(payload);
            string cmd = data.value("command", "");

            cout << "Received command: " << cmd << endl;

            if (cmd == "start_trip")
            {
                handle_start_trip(hdl, data);
            }
            else if (cmd == "stop_trip")
            {
                handle_stop_trip(hdl, data);
            }
            else if (cmd == "toggle_camera")
            {
                handle_toggle_camera(hdl, data);
            }
            else if (cmd == "get_stats")
            {
                handle_get_stats(hdl, data);
            }
            else if (cmd == "get_trip_history")
            {
                handle_get_trip_history(hdl, data);
            }
            else if (cmd == "get_vehicles")
            {
                handle_get_vehicles(hdl, data);
            }
            else if (cmd == "ping")
            {
                handle_ping(hdl, data);
            }
            else
            {
                send_error(hdl, "Unknown command: " + cmd);
            }
        }
        catch (const json::exception &e)
        {
            cerr << "JSON parse error: " << e.what() << endl;
            send_error(hdl, "Invalid JSON: " + string(e.what()));
        }
        catch (const exception &e)
        {
            cerr << "Error processing message: " << e.what() << endl;
            send_error(hdl, "Processing error: " + string(e.what()));
        }
    }

    void send_initial_data(connection_hdl hdl)
    {
        // Fixed JSON construction - use explicit object creation
        json data_obj = json::object();
        data_obj["server_version"] = "2.0";
        data_obj["camera_available"] = (camera != nullptr);
        data_obj["lane_detection"] = (lane_detector != nullptr);
        data_obj["location_available"] = (location_manager != nullptr);
        data_obj["timestamp"] = time(nullptr);

        json response = json::object();
        response["type"] = "init";
        response["data"] = data_obj;

        send_message(hdl, response.dump());
    }

    void handle_start_trip(connection_hdl hdl, const json &data)
    {
        uint64_t driver_id = data.value("driver_id", 1ULL);
        uint64_t vehicle_id = data.value("vehicle_id", 1ULL);

        live_data.trip_active = true;
        live_data.trip_id = chrono::duration_cast<chrono::milliseconds>(
                                chrono::system_clock::now().time_since_epoch())
                                .count();

        // Save trip start to database
        if (db_manager && db_manager->isOpen())
        {
            // Implement database save here
        }

        // Fixed JSON construction
        json data_obj = json::object();
        data_obj["trip_id"] = live_data.trip_id.load();
        data_obj["start_time"] = time(nullptr);
        data_obj["driver_id"] = driver_id;
        data_obj["vehicle_id"] = vehicle_id;
        data_obj["status"] = "active";

        json response = json::object();
        response["type"] = "trip_started";
        response["data"] = data_obj;

        send_message(hdl, response.dump());
        broadcast_live_data();
    }

    void handle_stop_trip(connection_hdl hdl, const json &data)
    {
        live_data.trip_active = false;

        // Save trip end to database
        if (db_manager && db_manager->isOpen())
        {
            // Implement database save here
        }

        // Fixed JSON construction
        json data_obj = json::object();
        data_obj["trip_id"] = live_data.trip_id.load();
        data_obj["end_time"] = time(nullptr);
        data_obj["distance"] = 0; // Calculate from GPS
        data_obj["duration"] = 0;

        json response = json::object();
        response["type"] = "trip_stopped";
        response["data"] = data_obj;

        send_message(hdl, response.dump());
        broadcast_live_data();

        // Reset trip-specific data
        live_data.trip_id = 0;
    }

    void handle_toggle_camera(connection_hdl hdl, const json &data)
    {
        bool enable = data.value("enable", !camera_running.load());

        if (enable && !camera_running.load())
        {
            if (camera)
            {
                camera_running = true;
                if (!camera_thread.joinable())
                {
                    camera_thread = thread(&SmartDriveBridge::camera_loop, this);
                }
            }
        }
        else if (!enable && camera_running.load())
        {
            camera_running = false;
        }

        // FIXED: Use .load() for atomic<bool>
        json data_obj = json::object();
        data_obj["enabled"] = camera_running.load(); // Use .load() here
        data_obj["available"] = (camera != nullptr);

        json response = json::object();
        response["type"] = "camera_status";
        response["data"] = data_obj;

        send_message(hdl, response.dump());
    }

    void handle_get_stats(connection_hdl hdl, const json &data)
    {
        // Fixed JSON construction with .load() for atomic values
        json data_obj = json::object();
        data_obj["speed"] = live_data.speed.load();
        data_obj["acceleration"] = live_data.acceleration.load();
        data_obj["safety_score"] = live_data.safety_score.load();
        data_obj["lane_status"] = live_data.get_lane_status(); // Use getter for thread-safe access
        data_obj["rapid_accel_count"] = live_data.rapid_accel_count.load();
        data_obj["hard_brake_count"] = live_data.hard_brake_count.load();
        data_obj["lane_departures"] = live_data.lane_departures.load();
        data_obj["trip_active"] = live_data.trip_active.load();
        data_obj["trip_id"] = live_data.trip_id.load();

        json response = json::object();
        response["type"] = "stats_response";
        response["data"] = data_obj;

        send_message(hdl, response.dump());
    }

    void handle_get_trip_history(connection_hdl hdl, const json &data)
    {
        // Query database for trip history
        json response = json::object();
        response["type"] = "trip_history";
        response["data"] = json::array(); // Empty array for now

        if (db_manager && db_manager->isOpen())
        {
            // Implement database query here
        }

        send_message(hdl, response.dump());
    }

    void handle_get_vehicles(connection_hdl hdl, const json &data)
    {
        json response = json::object();
        response["type"] = "vehicles";
        response["data"] = json::array(); // Empty array for now

        if (db_manager && db_manager->isOpen())
        {
            // Implement database query here
        }

        send_message(hdl, response.dump());
    }

    void handle_ping(connection_hdl hdl, const json &data)
    {
        json response = json::object();
        response["type"] = "pong";
        response["timestamp"] = time(nullptr);

        send_message(hdl, response.dump());
    }

    void send_error(connection_hdl hdl, const string &message)
    {
        json response = json::object();
        response["type"] = "error";
        response["message"] = message;
        response["timestamp"] = time(nullptr);

        send_message(hdl, response.dump());
    }

    void process_frame(cv::Mat &frame, double fps)
    {
        // Add FPS display
        cv::putText(frame, "FPS: " + to_string((int)fps),
                    cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 0.7,
                    cv::Scalar(0, 255, 0), 2);

        // Process lane detection if available
        if (lane_detector)
        {
            try
            {
                auto result = lane_detector->detectLanes(frame);

                // Draw lanes
                lane_detector->drawLanes(frame, result, true);

                // Check for lane departure
                string direction;
                double deviation;
                bool departure = lane_detector->checkLaneDeparture(result, frame, direction, deviation);

                if (departure)
                {
                    live_data.lane_departures++;
                    live_data.set_lane_status(direction); // Use setter for thread-safe access
                    lane_detector->drawDepartureWarning(frame, direction, deviation, true);

                    // Fixed JSON construction for warning
                    json data_obj = json::object();
                    data_obj["direction"] = direction;
                    data_obj["deviation"] = deviation;
                    data_obj["count"] = live_data.lane_departures.load();
                    data_obj["timestamp"] = time(nullptr);

                    json warning = json::object();
                    warning["type"] = "lane_warning";
                    warning["data"] = data_obj;

                    broadcast_message(warning.dump());
                }
                else
                {
                    live_data.set_lane_status("CENTERED");
                }
            }
            catch (const exception &e)
            {
                cerr << "Lane detection error: " << e.what() << endl;
            }
        }

        // Display trip status
        string status_text = live_data.trip_active.load() ? "TRIP ACTIVE" : "TRIP INACTIVE";
        cv::Scalar status_color = live_data.trip_active.load() ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 0, 255);
        cv::putText(frame, status_text, cv::Point(10, 60),
                    cv::FONT_HERSHEY_SIMPLEX, 0.7, status_color, 2);
    }

    void update_location(const LocationData &loc)
    {
        if (!loc.valid)
            return;

        static double last_speed = 0;
        static auto last_time = chrono::steady_clock::now();

        auto current_time = chrono::steady_clock::now();
        double time_diff = chrono::duration<double>(current_time - last_time).count();

        if (time_diff > 0)
        {
            double new_acceleration = (loc.speed_kmh - last_speed) / time_diff;
            last_speed = loc.speed_kmh;

            // Check for harsh events
            if (new_acceleration > 3.0)
            {
                live_data.rapid_accel_count++;
                broadcast_warning("rapid_acceleration", new_acceleration);
            }
            else if (new_acceleration < -4.0)
            {
                live_data.hard_brake_count++;
                broadcast_warning("hard_braking", new_acceleration);
            }

            live_data.acceleration = new_acceleration;
        }

        last_time = current_time;
        live_data.speed = loc.speed_kmh;

        // Update safety score
        double score = 1000.0 - (live_data.rapid_accel_count.load() * 5.0) -
                       (live_data.hard_brake_count.load() * 10.0) -
                       (live_data.lane_departures.load() * 3.0);
        live_data.safety_score = max(0.0, min(1000.0, score));

        // Broadcast live data
        if (live_data.trip_active.load())
        {
            broadcast_live_data();
        }
    }

    void broadcast_live_data()
    {
        // Fixed JSON construction with .load() for atomic values
        json data_obj = json::object();
        data_obj["speed"] = live_data.speed.load();
        data_obj["acceleration"] = live_data.acceleration.load();
        data_obj["safety_score"] = live_data.safety_score.load();
        data_obj["lane_status"] = live_data.get_lane_status(); // Use getter
        data_obj["rapid_accel_count"] = live_data.rapid_accel_count.load();
        data_obj["hard_brake_count"] = live_data.hard_brake_count.load();
        data_obj["lane_departures"] = live_data.lane_departures.load();
        data_obj["trip_active"] = live_data.trip_active.load();
        data_obj["trip_id"] = live_data.trip_id.load();
        data_obj["timestamp"] = time(nullptr);

        json data = json::object();
        data["type"] = "live_data";
        data["data"] = data_obj;

        broadcast_message(data.dump());
    }

    void broadcast_warning(const string &type, double value)
    {
        // Fixed JSON construction
        json data_obj = json::object();
        data_obj["warning_type"] = type;
        data_obj["value"] = value;
        data_obj["timestamp"] = time(nullptr);
        data_obj["trip_active"] = live_data.trip_active.load();

        json warning = json::object();
        warning["type"] = "warning";
        warning["data"] = data_obj;

        broadcast_message(warning.dump());
    }

    void send_message(connection_hdl hdl, const string &msg)
    {
        try
        {
            ws_server.send(hdl, msg, websocketpp::frame::opcode::text);
        }
        catch (const exception &e)
        {
            cerr << "Error sending message: " << e.what() << endl;
            lock_guard<mutex> lock(clients_mutex);
            clients.erase(hdl);
        }
    }

    void broadcast_message(const string &msg)
    {
        lock_guard<mutex> lock(clients_mutex);
        vector<connection_hdl> to_remove;

        for (const auto &client : clients)
        {
            try
            {
                ws_server.send(client.first, msg, websocketpp::frame::opcode::text);
            }
            catch (...)
            {
                to_remove.push_back(client.first);
            }
        }

        // Remove dead connections
        for (const auto &hdl : to_remove)
        {
            clients.erase(hdl);
        }
    }
};

int main(int argc, char **argv)
{
    cout << "Smart Drive WebSocket Bridge v2.0" << endl;
    cout << "==================================" << endl;

    SmartDriveBridge bridge;

    if (!bridge.initialize())
    {
        cerr << "Failed to initialize bridge. Exiting..." << endl;
        return 1;
    }

    uint16_t port = 8081;
    if (argc > 1)
    {
        try
        {
            port = static_cast<uint16_t>(stoi(argv[1]));
        }
        catch (...)
        {
            cerr << "Invalid port number. Using default 8081." << endl;
        }
    }

    cout << "Starting WebSocket bridge on port " << port << "..." << endl;
    cout << "Frontend should connect to: ws://localhost:" << port << endl;
    cout << endl;
    cout << "Commands:" << endl;
    cout << "  Enter 'stop' or 'exit' to stop the server" << endl;
    cout << "  Enter 'status' to show current status" << endl;
    cout << "  Enter 'help' to show this help" << endl;
    cout << endl;

    bridge.start(port);

    // Command loop
    string command;
    while (bridge.is_running())
    {
        cout << "> ";
        getline(cin, command);

        if (command == "stop" || command == "exit")
        {
            break;
        }
        else if (command == "status")
        {
            cout << "Bridge is running on port " << port << endl;
        }
        else if (command == "help")
        {
            cout << "Commands: stop, exit, status, help" << endl;
        }
        else if (!command.empty())
        {
            cout << "Unknown command. Type 'help' for available commands." << endl;
        }
    }

    cout << "Stopping bridge..." << endl;
    bridge.stop();

    cout << "Bridge stopped successfully." << endl;
    return 0;
}