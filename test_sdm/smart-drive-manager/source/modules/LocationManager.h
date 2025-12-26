// LocationManager.h - IMPROVED VERSION WITH REAL GPS APIs
#ifndef LOCATIONMANAGER_H
#define LOCATIONMANAGER_H

#include <string>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>
#include <iostream>
#include <queue>
#include <deque>
#include <cmath>

using json = nlohmann::json;
using namespace std;

struct LocationData
{
    double latitude;
    double longitude;
    double altitude;
    double speed_kmh;
    double accuracy;
    uint64_t timestamp;
    bool valid;
    string source;

    LocationData() : latitude(0), longitude(0), altitude(0),
                     speed_kmh(0), accuracy(0), timestamp(0), 
                     valid(false), source("") {}
};

class LocationManager
{
private:
    enum class APIType {
        IP_API,           // Basic IP geolocation
        OPENSTREETMAP,    // Nominatim API (free, no key)
        IPINFO,           // More accurate IP location
        FALLBACK          // Multiple fallback options
    };

    atomic<bool> running;
    thread update_thread;
    
    // Location data with history for smoothing
    LocationData current_location;
    deque<LocationData> location_history;
    mutex location_mutex;
    const size_t HISTORY_SIZE = 5;  // Keep last 5 readings for smoothing
    
    // Speed calculation
    double filtered_speed_kmh;
    double filtered_acceleration_ms2;
    
    // Callbacks
    function<void(const LocationData&)> location_callback;
    function<void(double)> speed_callback;
    function<void(double)> acceleration_callback;
    function<void(string, double)> event_callback;
    
    // Update interval
    int update_interval_ms;
    
    // API configuration
    APIType current_api;
    vector<pair<APIType, string>> api_endpoints;
    int api_retry_count;
    
    // CURL callback
    static size_t WriteCallback(void *contents, size_t size, size_t nmemb, string *userp)
    {
        userp->append((char *)contents, size * nmemb);
        return size * nmemb;
    }
    
    // CURL instance for connection reuse
    CURL *curl_handle;
    
public:
    LocationManager(int interval_ms = 5)  // 500ms = 2Hz update rate
        : running(false), 
          update_interval_ms(interval_ms),
          filtered_speed_kmh(0),
          filtered_acceleration_ms2(0),
          current_api(APIType::IP_API),
          api_retry_count(0),
          curl_handle(nullptr)
    {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        curl_handle = curl_easy_init();
        
        // Initialize multiple API endpoints
        api_endpoints = {
            {APIType::OPENSTREETMAP, "https://nominatim.openstreetmap.org/reverse?format=json&"},
            {APIType::IP_API, "http://ip-api.com/json/?fields=status,lat,lon,query"},
            {APIType::IPINFO, "https://ipinfo.io/json"}
        };
        
        cout << "LocationManager initialized with " << update_interval_ms 
             << "ms update interval" << endl;
    }

    ~LocationManager()
    {
        stop();
        if (curl_handle) {
            curl_easy_cleanup(curl_handle);
        }
        curl_global_cleanup();
    }

    // Start continuous location updates
    bool start()
    {
        if (running)
            return false;

        running = true;
        update_thread = thread(&LocationManager::updateLoop, this);
        cout << "LocationManager started (interval: " << update_interval_ms << "ms)" << endl;
        return true;
    }

    void stop()
    {
        running = false;
        if (update_thread.joinable())
        {
            update_thread.join();
        }
        cout << "LocationManager stopped" << endl;
    }

    // Get current location
    LocationData getLocation()
    {
        lock_guard<mutex> lock(location_mutex);
        return current_location;
    }
    
    // Get smoothed speed
    double getSmoothedSpeed() {
        return filtered_speed_kmh;
    }
    
    // Get smoothed acceleration
    double getSmoothedAcceleration() {
        return filtered_acceleration_ms2;
    }

    // Set callbacks
    void setLocationCallback(function<void(const LocationData&)> callback)
    {
        location_callback = callback;
    }
    
    void setSpeedCallback(function<void(double)> callback)
    {
        speed_callback = callback;
    }

    void setAccelerationCallback(function<void(double)> callback)
    {
        acceleration_callback = callback;
    }
    
    void setEventCallback(function<void(string, double)> callback)
    {
        event_callback = callback;
    }
    
    // Manual location update (for testing with real GPS data)
    void updateLocationManually(double lat, double lon, double accuracy = 10.0) {
        LocationData new_loc;
        new_loc.latitude = lat;
        new_loc.longitude = lon;
        new_loc.accuracy = accuracy;
        new_loc.valid = true;
        new_loc.timestamp = chrono::duration_cast<chrono::nanoseconds>(
            chrono::system_clock::now().time_since_epoch()).count();
        new_loc.source = "MANUAL";
        
        processNewLocation(new_loc);
    }

private:
    void updateLoop()
    {
        auto last_success_time = chrono::steady_clock::now();
        
        while (running)
        {
            auto loop_start = chrono::steady_clock::now();
            
            // Try to fetch from current API
            bool success = fetchLocationFromAPI(current_api);
            
            if (!success) {
                // Try fallback APIs
                success = tryFallbackAPIs();
            }
            
            if (success) {
                last_success_time = chrono::steady_clock::now();
                api_retry_count = 0;
            } else {
                api_retry_count++;
                
                // If no updates for 30 seconds, print warning
                auto now = chrono::steady_clock::now();
                auto time_since_success = chrono::duration_cast<chrono::seconds>(
                    now - last_success_time).count();
                
                if (time_since_success > 30) {
                    cerr << "Warning: No location updates for " << time_since_success 
                         << " seconds" << endl;
                }
            }
            
            // Calculate loop time and sleep for remainder
            auto loop_end = chrono::steady_clock::now();
            auto elapsed_ms = chrono::duration_cast<chrono::milliseconds>(
                loop_end - loop_start).count();
            
            if (elapsed_ms < update_interval_ms) {
                this_thread::sleep_for(chrono::milliseconds(
                    update_interval_ms - elapsed_ms));
            }
        }
    }
    
    bool tryFallbackAPIs() {
        for (auto& api : api_endpoints) {
            if (api.first == current_api) continue;
            
            cout << "Trying fallback API: " << apiToString(api.first) << endl;
            if (fetchLocationFromAPI(api.first)) {
                current_api = api.first;
                return true;
            }
        }
        return false;
    }
    
    string apiToString(APIType api) {
        switch(api) {
            case APIType::IP_API: return "IP-API";
            case APIType::OPENSTREETMAP: return "OpenStreetMap";
            case APIType::IPINFO: return "IPInfo";
            default: return "Unknown";
        }
    }

    bool fetchLocationFromAPI(APIType api_type)
    {
        if (!curl_handle) {
            curl_handle = curl_easy_init();
            if (!curl_handle) return false;
        }
        
        string url = getAPIUrl(api_type);
        string response;
        
        curl_easy_setopt(curl_handle, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 5L);
        curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "LocationManager/1.0");
        
        // For OpenStreetMap, add delay to respect rate limits
        if (api_type == APIType::OPENSTREETMAP) {
            this_thread::sleep_for(chrono::seconds(1));
        }
        
        CURLcode res = curl_easy_perform(curl_handle);
        
        if (res != CURLE_OK)
        {
            cerr << "CURL error for " << apiToString(api_type) 
                 << ": " << curl_easy_strerror(res) << endl;
            return false;
        }
        
        return parseLocationResponse(response, api_type);
    }
    
    string getAPIUrl(APIType api_type) {
        for (auto& api : api_endpoints) {
            if (api.first == api_type) {
                return api.second;
            }
        }
        return api_endpoints[0].second; // Default
    }

    bool parseLocationResponse(const string &response, APIType api_type)
    {
        try
        {
            auto j = json::parse(response);
            LocationData new_loc;
            
            bool parsed = false;
            
            switch(api_type) {
                case APIType::IP_API:
                    parsed = parseIPAPI(j, new_loc);
                    break;
                case APIType::OPENSTREETMAP:
                    parsed = parseOpenStreetMap(j, new_loc);
                    break;
                case APIType::IPINFO:
                    parsed = parseIPInfo(j, new_loc);
                    break;
                default:
                    parsed = parseIPAPI(j, new_loc);
            }
            
            if (parsed) {
                new_loc.source = apiToString(api_type);
                processNewLocation(new_loc);
                return true;
            }
        }
        catch (const exception &e)
        {
            cerr << "JSON parse error for " << apiToString(api_type) 
                 << ": " << e.what() << endl;
        }
        
        return false;
    }
    
    bool parseIPAPI(const json& j, LocationData& loc) {
        if (j.contains("lat") && j.contains("lon")) {
            loc.latitude = j["lat"];
            loc.longitude = j["lon"];
            loc.valid = true;
            loc.timestamp = chrono::duration_cast<chrono::nanoseconds>(
                chrono::system_clock::now().time_since_epoch()).count();
            loc.accuracy = 1000.0; // IP-based accuracy
            loc.altitude = 0.0;
            return true;
        }
        return false;
    }
    
    bool parseOpenStreetMap(const json& j, LocationData& loc) {
        if (j.contains("lat") && j.contains("lon")) {
            loc.latitude = stod(j["lat"].get<string>());
            loc.longitude = stod(j["lon"].get<string>());
            loc.valid = true;
            loc.timestamp = chrono::duration_cast<chrono::nanoseconds>(
                chrono::system_clock::now().time_since_epoch()).count();
            loc.accuracy = 50.0; // Better accuracy
            loc.altitude = 0.0;
            return true;
        }
        return false;
    }
    
    bool parseIPInfo(const json& j, LocationData& loc) {
        if (j.contains("loc")) {
            string loc_str = j["loc"];
            size_t comma = loc_str.find(',');
            if (comma != string::npos) {
                loc.latitude = stod(loc_str.substr(0, comma));
                loc.longitude = stod(loc_str.substr(comma + 1));
                loc.valid = true;
                loc.timestamp = chrono::duration_cast<chrono::nanoseconds>(
                    chrono::system_clock::now().time_since_epoch()).count();
                loc.accuracy = 500.0;
                loc.altitude = 0.0;
                return true;
            }
        }
        return false;
    }
    
    void processNewLocation(LocationData& new_loc) {
        lock_guard<mutex> lock(location_mutex);
        
        // Calculate speed if we have previous location
        if (!location_history.empty()) {
            LocationData prev = location_history.back();
            
            double distance = calculateHaversineDistance(
                prev.latitude, prev.longitude,
                new_loc.latitude, new_loc.longitude);
            
            double time_diff = (new_loc.timestamp - prev.timestamp) / 1e9;
            
            if (time_diff > 0.1 && distance > 0) { // Minimum 100ms difference
                double instant_speed_ms = distance / time_diff;
                new_loc.speed_kmh = instant_speed_ms * 3.6;
                
                // Apply simple low-pass filter
                filtered_speed_kmh = 0.7 * filtered_speed_kmh + 0.3 * new_loc.speed_kmh;
                
                // Calculate acceleration
                if (prev.speed_kmh > 0 || new_loc.speed_kmh > 0) {
                    double prev_speed_ms = prev.speed_kmh / 3.6;
                    double curr_speed_ms = new_loc.speed_kmh / 3.6;
                    double instant_accel = (curr_speed_ms - prev_speed_ms) / time_diff;
                    
                    filtered_acceleration_ms2 = 0.8 * filtered_acceleration_ms2 + 0.2 * instant_accel;
                    
                    // Check for events
                    checkEvents(filtered_acceleration_ms2, filtered_speed_kmh);
                    
                    // Trigger acceleration callback
                    if (acceleration_callback && abs(filtered_acceleration_ms2) > 0.5) {
                        acceleration_callback(filtered_acceleration_ms2);
                    }
                }
                
                // Trigger speed callback
                if (speed_callback && filtered_speed_kmh > 1.0) {
                    speed_callback(filtered_speed_kmh);
                }
            }
        }
        
        // Update history
        location_history.push_back(new_loc);
        if (location_history.size() > HISTORY_SIZE) {
            location_history.pop_front();
        }
        
        current_location = new_loc;
        
        // Trigger location callback
        if (location_callback) {
            location_callback(new_loc);
        }
    }
    
    double calculateHaversineDistance(double lat1, double lon1, double lat2, double lon2)
    {
        const double R = 6371000.0; // Earth radius in meters
        
        double phi1 = lat1 * M_PI / 180.0;
        double phi2 = lat2 * M_PI / 180.0;
        double delta_phi = (lat2 - lat1) * M_PI / 180.0;
        double delta_lambda = (lon2 - lon1) * M_PI / 180.0;
        
        double a = sin(delta_phi / 2.0) * sin(delta_phi / 2.0) +
                   cos(phi1) * cos(phi2) *
                   sin(delta_lambda / 2.0) * sin(delta_lambda / 2.0);
        
        double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
        
        return R * c;
    }
    
    void checkEvents(double accel, double speed) {
        const double RAPID_ACCEL_THRESHOLD = 2.5; // m/s²
        const double HARD_BRAKE_THRESHOLD = -3.0; // m/s²
        const double SPEEDING_THRESHOLD = 80.0; // km/h
        
        if (speed > 10.0) { // Only check when moving
            // Rapid acceleration
            if (accel > RAPID_ACCEL_THRESHOLD) {
                string event = "RAPID_ACCELERATION";
                cout << "⚠️ " << event << ": " << fixed << accel << " m/s²" << endl;
                if (event_callback) {
                    event_callback(event, accel);
                }
            }
            
            // Hard braking
            if (accel < HARD_BRAKE_THRESHOLD) {
                string event = "HARD_BRAKING";
                cout << "🚨 " << event << ": " << fixed << accel << " m/s²" << endl;
                if (event_callback) {
                    event_callback(event, accel);
                }
            }
            
            // Speeding
            if (speed > SPEEDING_THRESHOLD) {
                string event = "SPEEDING";
                cout << "🚫 " << event << ": " << fixed << speed << " km/h" << endl;
                if (event_callback) {
                    event_callback(event, speed);
                }
            }
        }
    }
};

#endif // LOCATIONMANAGER_H