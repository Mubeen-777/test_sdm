// main.cpp - REAL MOVEMENT TEST
#include "LocationManager.h"
#include <iomanip>
#include <sstream>
#include <fstream>
#include <csignal>

using namespace std;

atomic<bool> running(true);
LocationManager* g_locManager = nullptr;

void signalHandler(int signum) {
    cout << "\nInterrupt signal received. Stopping..." << endl;
    running = false;
    if (g_locManager) {
        g_locManager->stop();
    }
}

string formatTimestamp(uint64_t timestamp) {
    auto time_point = chrono::system_clock::time_point(chrono::nanoseconds(timestamp));
    auto time_t_val = chrono::system_clock::to_time_t(time_point);
    auto ms = chrono::duration_cast<chrono::milliseconds>(
        time_point.time_since_epoch()) % 1000;
    
    stringstream ss;
    ss << put_time(localtime(&time_t_val), "%H:%M:%S");
    ss << '.' << setfill('0') << setw(3) << ms.count();
    return ss.str();
}

void onLocationUpdate(const LocationData& loc) {
    static auto last_print = chrono::steady_clock::now();
    auto now = chrono::steady_clock::now();
    auto elapsed = chrono::duration_cast<chrono::milliseconds>(now - last_print).count();
    
    // Update display every second
    if (elapsed >= 1000) {
        cout << "\r[" << formatTimestamp(loc.timestamp) << "] "
             << fixed << setprecision(6) 
             << "Lat: " << loc.latitude << "° "
             << "Lon: " << loc.longitude << "° | "
             << setprecision(1)
             << "Speed: " << loc.speed_kmh << " km/h | "
             << "Acc: " << g_locManager->getSmoothedAcceleration() << " m/s² | "
             << "Src: " << loc.source
             << "          " << flush;
        last_print = now;
    }
}

void onSpeedUpdate(double speed) {
    // Optional: Log speed changes
    static double last_speed = 0;
    if (abs(speed - last_speed) > 5.0) {
        cout << "\n[SPEED] Changed to: " << fixed << setprecision(1) 
             << speed << " km/h" << endl;
        last_speed = speed;
    }
}

void onAccelerationUpdate(double accel) {
    // Log significant acceleration changes
    static double last_accel = 0;
    if (abs(accel - last_accel) > 0.5) {
        cout << "\n[ACCEL] " << fixed << setprecision(2) 
             << accel << " m/s²" << endl;
        last_accel = accel;
    }
}

void onEvent(string event, double value) {
    cout << "\n[EVENT] " << event << " = " << fixed << setprecision(2) 
         << value << (event == "SPEEDING" ? " km/h" : " m/s²") << endl;
}

int main() {
    cout << "========================================" << endl;
    cout << "REAL-TIME LOCATION TRACKING SYSTEM" << endl;
    cout << "========================================" << endl;
    cout << "This system calculates speed and acceleration" << endl;
    cout << "from actual position changes using multiple APIs." << endl;
    cout << "\nNote: For accurate speed calculation:" << endl;
    cout << "1. Move to different locations" << endl;
    cout << "2. Use mobile hotspot for IP changes" << endl;
    cout << "3. Or use manual location input" << endl;
    cout << "========================================" << endl;
    
    // Setup signal handler
    signal(SIGINT, signalHandler);
    
    // Create location manager with 2-second intervals (to respect API limits)
    LocationManager locManager(2000);
    g_locManager = &locManager;
    
    // Set callbacks
    locManager.setLocationCallback(onLocationUpdate);
    locManager.setSpeedCallback(onSpeedUpdate);
    locManager.setAccelerationCallback(onAccelerationUpdate);
    locManager.setEventCallback(onEvent);
    
    // Start the system
    if (!locManager.start()) {
        cerr << "Failed to start LocationManager!" << endl;
        return 1;
    }
    
    cout << "\nSystem started. Collecting location data..." << endl;
    cout << "Press Ctrl+C to stop\n" << endl;
    
    // Main loop
    while (running) {
        this_thread::sleep_for(chrono::seconds(1));
        
        // Optional: Manual location updates for testing
        // Uncomment to simulate movement:
        /*
        static double test_lat = 40.7128;
        static double test_lon = -74.0060;
        test_lat += 0.0001; // Simulate moving north
        locManager.updateLocationManually(test_lat, test_lon, 10.0);
        */
    }
    
    // Cleanup
    locManager.stop();
    
    cout << "\n\n========================================" << endl;
    cout << "Final Statistics:" << endl;
    cout << "Smoothed Speed: " << fixed << setprecision(1) 
         << locManager.getSmoothedSpeed() << " km/h" << endl;
    cout << "Smoothed Acceleration: " << fixed << setprecision(2) 
         << locManager.getSmoothedAcceleration() << " m/s²" << endl;
    cout << "========================================" << endl;
    
    return 0;
}