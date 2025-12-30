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
#include <iomanip>
#include "camera.h"
#include "lane_detector.h"
#include "LocationManager.h"
#include "../core/DatabaseManager.h"
#include "../core/TripManager.h"
#include "../core/VehicleManager.h"
#include "../core/CacheManager.h"
#include "../core/IndexManager.h"
#include "../../include/sdm_config.hpp"

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
    thread gps_broadcast_thread;
    atomic<bool> gps_broadcast_running;

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
    CacheManager *cache_manager = nullptr;
    IndexManager *index_manager = nullptr;
    TripManager *trip_manager = nullptr;
    VehicleManager *vehicle_manager = nullptr;

    // Active trip tracking
    uint64_t current_trip_id = 0;
    double current_trip_start_lat = 0;
    double current_trip_start_lon = 0;

    // ✅ FIX: Proper trip ID counter
    atomic<uint64_t> next_trip_id;

    // ✅ FIX: Track last GPS update time
    std::chrono::steady_clock::time_point last_gps_broadcast;

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

    // Update camera_loop function to send frames with warning cooldown only:
    void camera_loop()
    {
        cv::Mat frame;
        int frame_count = 0;
        int processed_frames = 0;
        auto last_fps_time = chrono::steady_clock::now();
        auto last_warning_time = chrono::steady_clock::now();
        double current_fps = 0;

        // Only limit warning messages to prevent flooding (not frame rate)
        const auto WARNING_COOLDOWN = chrono::milliseconds(500); // Minimum time between warnings

        while (camera_running.load())
        {
            try
            {
                if (camera && camera->grabFrame(frame) && !frame.empty())
                {
                    frame_count++;

                    // Calculate FPS every second
                    auto now = chrono::steady_clock::now();
                    auto fps_elapsed = chrono::duration_cast<chrono::milliseconds>(now - last_fps_time).count();
                    if (fps_elapsed >= 1000)
                    {
                        current_fps = processed_frames * 1000.0 / fps_elapsed;
                        processed_frames = 0;
                        last_fps_time = now;
                    }

                    processed_frames++;

                    // Process lane detection (with rate-limited warnings only)
                    process_frame_rate_limited(frame, current_fps, last_warning_time, WARNING_COOLDOWN);

                    // Encode frame as JPEG with original quality
                    vector<uchar> buffer;
                    cv::imencode(".jpg", frame, buffer, {cv::IMWRITE_JPEG_QUALITY, 70});

                    if (!buffer.empty())
                    {
                        // Convert to base64
                        string base64_frame = base64_encode(buffer.data(), buffer.size());

                        // Send via WebSocket
                        send_video_frame(base64_frame);
                    }

                    // Small sleep to prevent CPU hogging and allow GPS updates to be processed
                    // This ensures GPS broadcast thread gets CPU time
                    this_thread::sleep_for(chrono::milliseconds(10));
                }
                else
                {
                    // No frame available, small sleep to prevent busy waiting
                    this_thread::sleep_for(chrono::milliseconds(33)); // ~30 FPS max when no frames
                }
            }
            catch (const exception &e)
            {
                cerr << "Camera loop error: " << e.what() << endl;
                this_thread::sleep_for(chrono::milliseconds(100));
            }
        }
    }

    // Rate-limited frame processing to prevent message flooding
    void process_frame_rate_limited(cv::Mat &frame, double fps,
                                    chrono::steady_clock::time_point &last_warning_time,
                                    const chrono::milliseconds &warning_cooldown)
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
                    live_data.set_lane_status(direction);
                    lane_detector->drawDepartureWarning(frame, direction, deviation, true);

                    // Rate limit warning broadcasts to prevent message flooding
                    auto now = chrono::steady_clock::now();
                    if (chrono::duration_cast<chrono::milliseconds>(now - last_warning_time) >= warning_cooldown)
                    {
                        last_warning_time = now;

                        // Send warning with rate limiting
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
    SmartDriveBridge() : running(false), camera_running(false), db_manager(nullptr)
    {
        // Initialize WebSocket server
        ws_server.init_asio();
        ws_server.set_reuse_addr(true);

        // ✅ FIX: Initialize trip ID counter from current timestamp
        next_trip_id = static_cast<uint64_t>(time(nullptr)) * 1000;
        last_gps_broadcast = std::chrono::steady_clock::now();

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
        delete trip_manager;
        delete vehicle_manager;
        delete index_manager;
        delete cache_manager;
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
                cerr << "Database not found, creating new database..." << endl;
                SDMConfig config;
                config.max_drivers = 1000;
                config.max_vehicles = 5000;
                config.max_trips = 100000;

                if (!db_manager->create(config))
                {
                    cerr << "Failed to create database" << endl;
                    return false;
                }

                if (!db_manager->open())
                {
                    cerr << "Failed to open newly created database" << endl;
                    return false;
                }
            }

            // Initialize cache and index managers
            cache_manager = new CacheManager(256, 256, 512, 1024);
            index_manager = new IndexManager("compiled/indexes");
            if (!index_manager->open_indexes())
            {
                if (!index_manager->create_indexes())
                {
                    cerr << "Failed to create indexes" << endl;
                }
            }

            // Initialize trip and vehicle managers
            trip_manager = new TripManager(*db_manager, *cache_manager, *index_manager);
            vehicle_manager = new VehicleManager(*db_manager, *cache_manager, *index_manager);

            cout << "✓ Database managers initialized" << endl;

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

            // Start GPS broadcast thread (separate from camera loop)
            gps_broadcast_running = true;
            gps_broadcast_thread = thread(&SmartDriveBridge::gps_broadcast_loop, this);
            cout << "✓ GPS broadcast thread started" << endl;

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
        gps_broadcast_running = false;

        // Stop location manager
        if (location_manager)
        {
            location_manager->stop();
        }

        // Stop GPS broadcast thread
        if (gps_broadcast_thread.joinable())
        {
            gps_broadcast_thread.join();
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
    void broadcast_live_data_with_gps(double lat, double lon, double speed, double accuracy)
    {
        json data_obj = json::object();
        data_obj["speed"] = speed;
        data_obj["acceleration"] = live_data.acceleration.load();
        data_obj["safety_score"] = live_data.safety_score.load();
        data_obj["lane_status"] = live_data.get_lane_status();
        data_obj["rapid_accel_count"] = live_data.rapid_accel_count.load();
        data_obj["hard_brake_count"] = live_data.hard_brake_count.load();
        data_obj["lane_departures"] = live_data.lane_departures.load();
        data_obj["trip_active"] = live_data.trip_active.load();
        data_obj["trip_id"] = live_data.trip_id.load();
        data_obj["latitude"] = lat;
        data_obj["longitude"] = lon;
        data_obj["accuracy"] = accuracy;
        data_obj["timestamp"] = time(nullptr);
        data_obj["gps_source"] = "browser";

        json data = json::object();
        data["type"] = "live_data";
        data["data"] = data_obj;

        broadcast_message(data.dump());
    }
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

    void handle_gps_update(connection_hdl hdl, const json &data)
    {
        try
        {
            double latitude = data.value("latitude", 31.5204);
            double longitude = data.value("longitude", 74.3587);
            double speed_kmh = data.value("speed_kmh", 0.0);
            double accuracy = data.value("accuracy", 10.0);
            uint64_t timestamp = data.value("timestamp", (uint64_t)0);
            string source = data.value("source", "browser_gps");

            // ✅ FIX: Rate limit console output (once per second)
            static auto last_log_time = std::chrono::steady_clock::now();
            auto now = std::chrono::steady_clock::now();
            auto time_since_log = std::chrono::duration_cast<std::chrono::seconds>(now - last_log_time).count();

            if (time_since_log >= 1)
            {
                cout << "📍 GPS: " << fixed << setprecision(6)
                     << "Lat=" << latitude << ", Lon=" << longitude
                     << ", Speed=" << setprecision(1) << speed_kmh << " km/h" << endl;
                last_log_time = now;
            }

            // Update live data
            static double last_speed = 0;
            static auto last_time = chrono::steady_clock::now();

            auto current_time = chrono::steady_clock::now();
            double time_diff = chrono::duration<double>(current_time - last_time).count();

            if (time_diff > 0.1)
            {
                double new_acceleration = (speed_kmh - last_speed) / time_diff;
                last_speed = speed_kmh;

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
            live_data.speed = speed_kmh;

            double score = 1000.0 - (live_data.rapid_accel_count.load() * 5.0) -
                           (live_data.hard_brake_count.load() * 10.0) -
                           (live_data.lane_departures.load() * 3.0);
            live_data.safety_score = max(0.0, min(1000.0, score));

            // Log GPS point if trip is active
            if (live_data.trip_active.load() && trip_manager && current_trip_id > 0)
            {
                trip_manager->log_gps_point(current_trip_id, latitude, longitude,
                                            speed_kmh, 0.0, accuracy);
            }

            // ✅ FIX: Rate limit broadcasts (every 500ms)
            auto time_since_broadcast = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_gps_broadcast).count();
            if (time_since_broadcast >= 500)
            {
                broadcast_live_data_with_gps(latitude, longitude, speed_kmh, accuracy);
                last_gps_broadcast = now;
            }

            // ✅ FIX: Don't send acknowledgment for every GPS update (too much overhead)
            // Backend will just process silently
        }
        catch (const exception &e)
        {
            cerr << "❌ Error handling GPS update: " << e.what() << endl;
            send_error(hdl, "GPS update error: " + string(e.what()));
        }
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
            // ✅ NEW: Handle GPS updates from browser
            else if (cmd == "gps_update")
            {
                handle_gps_update(hdl, data);
            }
            else if (cmd == "get_stats")
            {
                handle_get_stats(hdl, data);
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

        if (!trip_manager)
        {
            send_error(hdl, "Trip manager not initialized");
            return;
        }

        double start_lat = 31.5204;
        double start_lon = 74.3587;
        if (location_manager)
        {
            LocationData loc = location_manager->getLocation();
            if (loc.valid)
            {
                start_lat = loc.latitude;
                start_lon = loc.longitude;
            }
        }

        // ✅ FIX: Generate unique trip ID
        uint64_t trip_id = next_trip_id.fetch_add(1);

        cout << "🚗 Starting trip with generated ID: " << trip_id << endl;

        // ✅ FIX: Try to start trip, retry with new ID if it fails
        int retry_count = 0;
        bool trip_created = false;

        while (!trip_created && retry_count < 5)
        {
            try
            {
                trip_id = trip_manager->start_trip(driver_id, vehicle_id, start_lat, start_lon, "");

                if (trip_id > 0)
                {
                    trip_created = true;
                    break;
                }
                else
                {
                    cout << "⚠️ Backend returned invalid trip_id, retrying..." << endl;
                    trip_id = next_trip_id.fetch_add(1);
                    retry_count++;
                }
            }
            catch (const exception &e)
            {
                cerr << "❌ Exception starting trip: " << e.what() << endl;
                trip_id = next_trip_id.fetch_add(1);
                retry_count++;
            }
        }

        if (trip_id > 0 && trip_created)
        {
            live_data.trip_active = true;
            live_data.trip_id = trip_id;
            current_trip_id = trip_id;
            current_trip_start_lat = start_lat;
            current_trip_start_lon = start_lon;

            cout << "✅ Trip started with ID: " << trip_id << endl;

            json data_obj = json::object();
            data_obj["trip_id"] = trip_id;
            data_obj["start_time"] = time(nullptr);
            data_obj["driver_id"] = driver_id;
            data_obj["vehicle_id"] = vehicle_id;
            data_obj["status"] = "active";
            data_obj["start_latitude"] = start_lat;
            data_obj["start_longitude"] = start_lon;

            json response = json::object();
            response["type"] = "trip_started";
            response["data"] = data_obj;

            send_message(hdl, response.dump());
            broadcast_live_data();
        }
        else
        {
            cerr << "❌ Failed to start trip after " << retry_count << " retries" << endl;
            send_error(hdl, "Failed to start trip in database. Database may be full or corrupted.");
        }
    }

    // ✅ FIXED: handle_stop_trip with validation
    void handle_stop_trip(connection_hdl hdl, const json &data)
    {
        if (!trip_manager)
        {
            send_error(hdl, "Trip manager not initialized");
            return;
        }

        // ✅ FIX: Get trip_id from data or use current_trip_id
        uint64_t trip_id = 0;

        if (data.contains("trip_id"))
        {
            trip_id = data["trip_id"].get<uint64_t>();
        }
        else
        {
            trip_id = current_trip_id > 0 ? current_trip_id : live_data.trip_id.load();
        }

        cout << "🛑 Attempting to stop trip: " << trip_id << endl;

        if (trip_id == 0)
        {
            send_error(hdl, "No active trip to stop (trip_id = 0)");
            return;
        }

        // ✅ FIX: Validate trip exists before trying to end it
        TripRecord trip_record;
        if (!trip_manager->get_trip_details(trip_id, trip_record))
        {
            cerr << "❌ Trip " << trip_id << " not found in database" << endl;
            send_error(hdl, "Trip not found. It may have already been stopped or never started.");

            // Clear local state even if trip not found
            live_data.trip_active = false;
            live_data.trip_id = 0;
            current_trip_id = 0;

            return;
        }

        double end_lat = 31.5204;
        double end_lon = 74.3587;
        if (location_manager)
        {
            LocationData loc = location_manager->getLocation();
            if (loc.valid)
            {
                end_lat = loc.latitude;
                end_lon = loc.longitude;
            }
        }

        cout << "🛑 Ending trip " << trip_id << " at: "
             << fixed << setprecision(6) << end_lat << ", " << end_lon << endl;

        bool success = trip_manager->end_trip(trip_id, end_lat, end_lon, "");

        if (success)
        {
            live_data.trip_active = false;
            live_data.trip_id = 0;
            current_trip_id = 0;

            cout << "✅ Trip " << trip_id << " ended successfully" << endl;

            json data_obj = json::object();
            data_obj["trip_id"] = trip_id;
            data_obj["end_time"] = time(nullptr);
            data_obj["distance"] = 0;
            data_obj["duration"] = 0;
            data_obj["status"] = "completed";

            json response = json::object();
            response["type"] = "trip_stopped";
            response["data"] = data_obj;

            send_message(hdl, response.dump());
            broadcast_live_data();
        }
        else
        {
            cerr << "❌ Failed to save trip " << trip_id << " to database" << endl;
            send_error(hdl, "Failed to save trip to database. Database may be corrupted or full.");
        }
    }
    void handle_toggle_camera(connection_hdl hdl, const json &data)
    {
        bool enable = data.value("enable", !camera_running.load());

        if (enable && !camera_running.load())
        {
            if (camera)
            {
                // Join the old thread if it exists and has finished
                if (camera_thread.joinable())
                {
                    camera_thread.join();
                }

                camera_running = true;
                camera_thread = thread(&SmartDriveBridge::camera_loop, this);
            }
        }
        else if (!enable && camera_running.load())
        {
            camera_running = false;

            // Wait for camera thread to finish
            if (camera_thread.joinable())
            {
                camera_thread.join();
            }
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
        if (!trip_manager)
        {
            send_error(hdl, "Trip manager not initialized");
            return;
        }

        uint64_t driver_id = data.value("driver_id", 1ULL);
        int limit = data.value("limit", 20);

        auto trips = trip_manager->get_driver_trips(driver_id, limit);

        json trips_array = json::array();
        for (const auto &trip : trips)
        {
            json trip_obj = json::object();
            trip_obj["trip_id"] = trip.trip_id;
            trip_obj["driver_id"] = trip.driver_id;
            trip_obj["vehicle_id"] = trip.vehicle_id;
            trip_obj["start_time"] = trip.start_time;
            trip_obj["end_time"] = trip.end_time;
            trip_obj["duration"] = trip.duration;
            trip_obj["distance"] = trip.distance;
            trip_obj["avg_speed"] = trip.avg_speed;
            trip_obj["max_speed"] = trip.max_speed;
            trip_obj["fuel_consumed"] = trip.fuel_consumed;
            trip_obj["fuel_efficiency"] = trip.fuel_efficiency;
            trip_obj["start_address"] = string(trip.start_address);
            trip_obj["end_address"] = string(trip.end_address);
            trips_array.push_back(trip_obj);
        }

        json response = json::object();
        response["type"] = "trip_history";
        response["data"] = trips_array;

        send_message(hdl, response.dump());
    }

    void handle_get_vehicles(connection_hdl hdl, const json &data)
    {
        if (!vehicle_manager)
        {
            send_error(hdl, "Vehicle manager not initialized");
            return;
        }

        uint64_t driver_id = data.value("driver_id", 1ULL);

        auto vehicles = vehicle_manager->get_driver_vehicles(driver_id);

        json vehicles_array = json::array();
        for (const auto &vehicle : vehicles)
        {
            json vehicle_obj = json::object();
            vehicle_obj["vehicle_id"] = vehicle.vehicle_id;
            vehicle_obj["owner_driver_id"] = vehicle.owner_driver_id;
            vehicle_obj["license_plate"] = string(vehicle.license_plate);
            vehicle_obj["make"] = string(vehicle.make);
            vehicle_obj["model"] = string(vehicle.model);
            vehicle_obj["year"] = vehicle.year;
            vehicle_obj["type"] = static_cast<int>(vehicle.type);
            vehicle_obj["current_odometer"] = vehicle.current_odometer;
            vehicle_obj["fuel_type"] = string(vehicle.fuel_type);
            vehicle_obj["vin"] = string(vehicle.vin);
            vehicles_array.push_back(vehicle_obj);
        }

        json response = json::object();
        response["type"] = "vehicles";
        response["data"] = vehicles_array;

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

    void update_location(const LocationData &loc)
    {
        if (!loc.valid)
        {
            static auto last_warning_time = chrono::steady_clock::now();
            auto now = chrono::steady_clock::now();
            auto time_since_warning = chrono::duration_cast<chrono::seconds>(now - last_warning_time).count();

            // Warn about invalid GPS every 10 seconds
            if (time_since_warning >= 10)
            {
                cerr << "⚠️ GPS: Invalid location data received" << endl;
                last_warning_time = now;
            }
            return;
        }

        // Log GPS update with rate limiting
        static auto last_log_time = chrono::steady_clock::now();
        auto now = chrono::steady_clock::now();
        auto time_since_log = chrono::duration_cast<chrono::seconds>(now - last_log_time).count();

        // Log GPS update every 5 seconds to avoid spam
        if (time_since_log >= 5)
        {
            cout << "📍 GPS Update: Lat=" << fixed << setprecision(6) << loc.latitude
                 << ", Lon=" << loc.longitude
                 << ", Speed=" << setprecision(1) << loc.speed_kmh << " km/h"
                 << ", Accuracy=" << setprecision(1) << loc.accuracy << "m" << endl;
            last_log_time = now;
        }

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

        // Log GPS point if trip is active
        if (live_data.trip_active.load() && trip_manager && current_trip_id > 0)
        {
            trip_manager->log_gps_point(current_trip_id, loc.latitude, loc.longitude,
                                        loc.speed_kmh, loc.altitude, loc.accuracy);
        }

        // Note: GPS updates are now broadcast via separate gps_broadcast_loop thread
        // This callback still updates live_data, but broadcasting happens independently
    }

    // Separate GPS broadcast loop - runs independently of camera loop
    void gps_broadcast_loop()
    {
        const auto GPS_UPDATE_INTERVAL = chrono::milliseconds(1000); // Broadcast GPS every 1 second
        static auto last_broadcast_log = chrono::steady_clock::now();
        int broadcast_count = 0;

        while (gps_broadcast_running.load())
        {
            try
            {
                // Always broadcast GPS data, regardless of trip status
                // This ensures GPS updates are sent regularly even when camera is busy
                broadcast_live_data();
                broadcast_count++;

                // Log broadcast status every 10 seconds
                auto now = chrono::steady_clock::now();
                auto time_since_log = chrono::duration_cast<chrono::seconds>(now - last_broadcast_log).count();
                if (time_since_log >= 10)
                {
                    cout << "📡 GPS Broadcast: " << broadcast_count << " broadcasts in last 10s" << endl;
                    broadcast_count = 0;
                    last_broadcast_log = now;
                }

                // Sleep for the update interval
                this_thread::sleep_for(GPS_UPDATE_INTERVAL);
            }
            catch (const exception &e)
            {
                cerr << "❌ GPS broadcast loop error: " << e.what() << endl;
                // Sleep a bit longer on error to prevent tight error loop
                this_thread::sleep_for(chrono::milliseconds(500));
            }
        }
    }

    void broadcast_live_data()
    {
        double lat = 31.5204;
        double lon = 74.3587;
        bool gps_valid = false;
        static double last_lat = 0;
        static double last_lon = 0;
        static int stuck_count = 0;

        if (location_manager)
        {
            LocationData loc = location_manager->getLocation();
            if (loc.valid)
            {
                lat = loc.latitude;
                lon = loc.longitude;
                gps_valid = true;

                // Check if GPS is stuck (same coordinates for too long)
                if (abs(lat - last_lat) < 0.000001 && abs(lon - last_lon) < 0.000001)
                {
                    stuck_count++;
                    if (stuck_count > 30) // 30 seconds of no movement
                    {
                        static auto last_stuck_warning = chrono::steady_clock::now();
                        auto now = chrono::steady_clock::now();
                        auto time_since_warning = chrono::duration_cast<chrono::seconds>(now - last_stuck_warning).count();
                        if (time_since_warning >= 10)
                        {
                            cerr << "⚠️ GPS WARNING: Coordinates unchanged for " << stuck_count << " seconds - GPS may be stuck!" << endl;
                            last_stuck_warning = now;
                        }
                    }
                }
                else
                {
                    stuck_count = 0; // Reset if coordinates changed
                    last_lat = lat;
                    last_lon = lon;
                }
            }
            else
            {
                static auto last_invalid_warning = chrono::steady_clock::now();
                auto now = chrono::steady_clock::now();
                auto time_since_warning = chrono::duration_cast<chrono::seconds>(now - last_invalid_warning).count();
                if (time_since_warning >= 10)
                {
                    cerr << "⚠️ GPS: Location manager returned invalid data" << endl;
                    last_invalid_warning = now;
                }
            }
        }

        json data_obj = json::object();
        data_obj["speed"] = live_data.speed.load();
        data_obj["acceleration"] = live_data.acceleration.load();
        data_obj["safety_score"] = live_data.safety_score.load();
        data_obj["lane_status"] = live_data.get_lane_status();
        data_obj["rapid_accel_count"] = live_data.rapid_accel_count.load();
        data_obj["hard_brake_count"] = live_data.hard_brake_count.load();
        data_obj["lane_departures"] = live_data.lane_departures.load();
        data_obj["trip_active"] = live_data.trip_active.load();
        data_obj["trip_id"] = live_data.trip_id.load();
        data_obj["latitude"] = lat;
        data_obj["longitude"] = lon;
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