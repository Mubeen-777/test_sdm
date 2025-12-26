// File Location: source/modules/TripManager.h
// Smart Drive Manager - Trip Management & GPS Logging

#ifndef TRIPMANAGER_H
#define TRIPMANAGER_H

#include "../../include/sdm_types.hpp"
#include "../../source/core/DatabaseManager.h"
#include "../../source/core/CacheManager.h"
#include "../../source/core/IndexManager.h"
#include "../../source/data_structures/CircularQueue.h"
#include "../../source/data_structures/DoublyLinkedList.h"
#include <vector>
#include <cmath>
#include <algorithm>

class TripManager
{
private:
    DatabaseManager &db_;
    CacheManager &cache_;
    IndexManager &index_;
    // GPS data buffer
    CircularQueue<GPSWaypoint> gps_buffer_;

    // Active trips
    struct ActiveTrip
    {
        uint64_t trip_id;
        TripRecord record;
        std::vector<GPSWaypoint> waypoints;
        uint64_t start_time;
        bool vision_active;
    };

    std::vector<ActiveTrip> active_trips_;

    // Constants
    static constexpr double EARTH_RADIUS_KM = 6371.0;
    static constexpr double HARSH_BRAKING_THRESHOLD = -3.0;     // m/s²
    static constexpr double RAPID_ACCELERATION_THRESHOLD = 3.0; // m/s²
    static constexpr double SPEEDING_THRESHOLD = 120.0;         // km/h
    void update_driver_safety_score(uint64_t driver_id, int delta)
    {
        DriverProfile driver;
        if (db_.read_driver(driver_id, driver))
        {
            int new_score = (int)driver.safety_score + delta;

            if (new_score < 0)
                new_score = 0;
            if (new_score > 1000)
                new_score = 1000;

            driver.safety_score = (uint32_t)new_score;

            db_.update_driver(driver);
            cache_.invalidate_driver(driver_id);
        }
    }

public:
    TripManager(DatabaseManager &db, CacheManager &cache, IndexManager &index,
                size_t gps_buffer_size = 50000)
        : db_(db), cache_(cache), index_(index),
          gps_buffer_(gps_buffer_size) {}

    // ========================================================================
    // TRIP LIFECYCLE
    // ========================================================================

    uint64_t start_trip(uint64_t driver_id, uint64_t vehicle_id,
                        double start_lat, double start_lon,
                        const std::string &start_address = "")
    {
        // Generate trip ID
        uint64_t trip_id = generate_trip_id();

        // Create trip record
        TripRecord trip;
        trip.trip_id = trip_id;
        trip.driver_id = driver_id;
        trip.vehicle_id = vehicle_id;
        trip.start_time = get_current_timestamp();
        trip.start_latitude = start_lat;
        trip.start_longitude = start_lon;
        strncpy(trip.start_address, start_address.c_str(), sizeof(trip.start_address) - 1);

        // Save to database
        if (!db_.create_trip(trip))
        {
            return 0; // Failed
        }

        // Add to index
        index_.insert_primary(3, trip_id, trip.start_time, 0); // entity_type=3 for Trip

        // Create active trip
        ActiveTrip active;
        active.trip_id = trip_id;
        active.record = trip;
        active.start_time = trip.start_time;
        active.vision_active = false;
        active_trips_.push_back(active);

        return trip_id;
    }

    bool log_gps_point(uint64_t trip_id, double latitude, double longitude,
                       float speed, float altitude = 0, float accuracy = 0)
    {
        // Create waypoint
        GPSWaypoint waypoint;
        waypoint.timestamp = get_current_timestamp();
        waypoint.latitude = latitude;
        waypoint.longitude = longitude;
        waypoint.speed = speed;
        waypoint.altitude = altitude;
        waypoint.accuracy = accuracy;

        // Add to buffer
        if (!gps_buffer_.try_enqueue(waypoint))
        {
            return false; // Buffer full
        }

        // Find active trip and add waypoint
        for (auto &active : active_trips_)
        {
            if (active.trip_id == trip_id)
            {
                active.waypoints.push_back(waypoint);

                // Detect driving events
                detect_driving_events(active, waypoint);

                return true;
            }
        }

        return false;
    }
    bool end_trip(uint64_t trip_id, double end_lat, double end_lon,
                  const std::string &end_address = "")
    {
        auto it = std::find_if(active_trips_.begin(), active_trips_.end(),
                               [trip_id](const ActiveTrip &t)
                               { return t.trip_id == trip_id; });

        if (it == active_trips_.end())
        {
            return false;
        }

        ActiveTrip &active = *it;

        // Stop vision processing and update safety score

        calculate_trip_metrics(active);

        active.record.end_time = get_current_timestamp();
        active.record.end_latitude = end_lat;
        active.record.end_longitude = end_lon;
        strncpy(active.record.end_address, end_address.c_str(), sizeof(active.record.end_address) - 1);
        active.record.duration = (active.record.end_time - active.record.start_time) / 1000000000;
        active.record.gps_data_count = active.waypoints.size();

        db_.update_trip(active.record);
        update_driver_stats(active.record);

        active_trips_.erase(it);

        return true;
    }

    void detect_driving_events(ActiveTrip &trip, const GPSWaypoint &current)
    {
        if (trip.waypoints.size() < 2)
            return;

        const GPSWaypoint &previous = trip.waypoints[trip.waypoints.size() - 2];

        double time_diff = (current.timestamp - previous.timestamp) / 1000000000.0;
        if (time_diff <= 0)
            return;

        double speed_diff = current.speed - previous.speed;
        double acceleration = (speed_diff / 3.6) / time_diff;

        if (acceleration < HARSH_BRAKING_THRESHOLD)
        {
            trip.record.harsh_braking_count++;
            update_driver_safety_score(trip.record.driver_id, -5);
        }

        if (acceleration > RAPID_ACCELERATION_THRESHOLD)
        {
            trip.record.rapid_acceleration_count++;
            update_driver_safety_score(trip.record.driver_id, -3);
        }

        if (current.speed > SPEEDING_THRESHOLD)
        {
            trip.record.speeding_count++;
            update_driver_safety_score(trip.record.driver_id, -10);
        }

        double heading_change = calculate_heading_change(previous, current);
        if (abs(heading_change) > 30 && current.speed > 20)
        {
            trip.record.sharp_turn_count++;
            update_driver_safety_score(trip.record.driver_id, -2);
        }
    }

    void calculate_trip_metrics(ActiveTrip &trip)
    {
        if (trip.waypoints.empty())
            return;

        // Calculate total distance
        double total_distance = 0;
        for (size_t i = 1; i < trip.waypoints.size(); i++)
        {
            total_distance += calculate_distance(
                trip.waypoints[i - 1].latitude, trip.waypoints[i - 1].longitude,
                trip.waypoints[i].latitude, trip.waypoints[i].longitude);
        }
        trip.record.distance = total_distance;

        // Calculate average speed
        if (trip.record.duration > 0)
        {
            trip.record.avg_speed = (total_distance / trip.record.duration) * 3600; // km/h
        }

        // Find max speed
        trip.record.max_speed = 0;
        for (const auto &waypoint : trip.waypoints)
        {
            if (waypoint.speed > trip.record.max_speed)
            {
                trip.record.max_speed = waypoint.speed;
            }
        }

        // Estimate fuel consumption (simplified formula)
        // Real implementation would use vehicle-specific parameters
        trip.record.fuel_consumed = estimate_fuel_consumption(trip.record);

        if (trip.record.fuel_consumed > 0)
        {
            trip.record.fuel_efficiency = total_distance / trip.record.fuel_consumed;
        }
    }

    // ========================================================================
    // TRIP QUERIES
    // ========================================================================

    std::vector<TripRecord> get_driver_trips(uint64_t driver_id, int limit = 100)
    {
        // Check cache first
        std::vector<uint64_t> cached_trip_ids;
        std::string cache_key = "driver_trips_" + std::to_string(driver_id);

        if (cache_.get_query_result(cache_key, cached_trip_ids))
        {
            std::vector<TripRecord> trips;
            for (uint64_t trip_id : cached_trip_ids)
            {
                TripRecord trip;
                if (db_.read_trip(trip_id, trip))
                {
                    trips.push_back(trip);
                }
                if (trips.size() >= limit)
                    break;
            }
            return trips;
        }

        // Fetch from database
        auto trips = db_.get_trips_by_driver(driver_id, limit);

        // Cache trip IDs
        cached_trip_ids.clear();
        for (const auto &trip : trips)
        {
            cached_trip_ids.push_back(trip.trip_id);
        }
        cache_.put_query_result(cache_key, cached_trip_ids);

        return trips;
    }

    std::vector<TripRecord> get_trips_by_date_range(uint64_t driver_id,
                                                    uint64_t start_time,
                                                    uint64_t end_time)
    {
        std::vector<TripRecord> trips;

        // Use index for range query
        auto offsets = index_.range_query_primary(3, driver_id, start_time, end_time);

        for (uint64_t offset : offsets)
        {
            TripRecord trip;
            // In production, would read from offset directly
            // For now, linear search in database
            auto all_trips = db_.get_trips_by_driver(driver_id, 10000);
            for (const auto &t : all_trips)
            {
                if (t.start_time >= start_time && t.start_time <= end_time)
                {
                    trips.push_back(t);
                }
            }
        }

        return trips;
    }

    bool get_trip_details(uint64_t trip_id, TripRecord &trip)
    {
        // Try cache first
        if (cache_.get_trip(trip_id, trip))
        {
            return true;
        }

        // Fetch from database
        if (db_.read_trip(trip_id, trip))
        {
            cache_.put_trip(trip_id, trip);
            return true;
        }

        return false;
    }

    // ========================================================================
    // ANALYTICS
    // ========================================================================

    struct TripStatistics
    {
        uint64_t total_trips;
        double total_distance;
        double total_duration;
        double avg_speed;
        double max_speed;
        double total_fuel;
        double avg_fuel_efficiency;
        uint32_t total_harsh_events;
        uint32_t safety_score;
    };

    TripStatistics get_driver_statistics(uint64_t driver_id)
    {
        TripStatistics stats = {};

        auto trips = db_.get_trips_by_driver(driver_id, 10000);

        for (const auto &trip : trips)
        {
            stats.total_trips++;
            stats.total_distance += trip.distance;
            stats.total_duration += trip.duration;
            stats.total_fuel += trip.fuel_consumed;

            if (trip.max_speed > stats.max_speed)
            {
                stats.max_speed = trip.max_speed;
            }

            stats.total_harsh_events += trip.harsh_braking_count +
                                        trip.rapid_acceleration_count +
                                        trip.speeding_count +
                                        trip.sharp_turn_count;
        }

        if (stats.total_trips > 0 && stats.total_duration > 0)
        {
            stats.avg_speed = (stats.total_distance / stats.total_duration) * 3600;
        }
        else
        {
            stats.avg_speed = 0;
        }

        if (stats.total_fuel > 0 && stats.total_distance > 0)
        {
            stats.avg_fuel_efficiency = stats.total_distance / stats.total_fuel;
        }
        else
        {
            stats.avg_fuel_efficiency = 0;
        }

        // Calculate safety score (0-1000)
        stats.safety_score = calculate_safety_score(stats);

        return stats;
    }

    // ========================================================================
    // HELPER FUNCTIONS
    // ========================================================================

private:
    uint64_t generate_trip_id()
    {
        static uint64_t counter = 1;
        return counter++;
    }

    uint64_t get_current_timestamp()
    {
        return std::chrono::system_clock::now().time_since_epoch().count();
    }

    // Haversine formula for distance calculation
    double calculate_distance(double lat1, double lon1, double lat2, double lon2)
    {
        double dLat = (lat2 - lat1) * M_PI / 180.0;
        double dLon = (lon2 - lon1) * M_PI / 180.0;

        lat1 = lat1 * M_PI / 180.0;
        lat2 = lat2 * M_PI / 180.0;

        double a = sin(dLat / 2) * sin(dLat / 2) +
                   sin(dLon / 2) * sin(dLon / 2) * cos(lat1) * cos(lat2);
        double c = 2 * atan2(sqrt(a), sqrt(1 - a));

        return EARTH_RADIUS_KM * c;
    }

    double calculate_heading_change(const GPSWaypoint &p1, const GPSWaypoint &p2)
    {
        double dLon = (p2.longitude - p1.longitude) * M_PI / 180.0;
        double lat1 = p1.latitude * M_PI / 180.0;
        double lat2 = p2.latitude * M_PI / 180.0;

        double y = sin(dLon) * cos(lat2);
        double x = cos(lat1) * sin(lat2) - sin(lat1) * cos(lat2) * cos(dLon);

        return atan2(y, x) * 180.0 / M_PI;
    }

    double estimate_fuel_consumption(const TripRecord &trip)
    {
        // Simplified fuel consumption model
        // Real implementation would use vehicle specs and driving patterns

        double base_consumption = trip.distance * 0.08; // 8L/100km base

        // Penalties for harsh driving
        double harsh_penalty = (trip.harsh_braking_count * 0.05 +
                                trip.rapid_acceleration_count * 0.05 +
                                trip.speeding_count * 0.02) *
                               0.1;

        return base_consumption * (1.0 + harsh_penalty);
    }

    uint32_t calculate_safety_score(const TripStatistics &stats)
    {
        uint32_t base_score = 1000;

        if (stats.total_distance > 0)
        {
            // Deduct points for harsh events per 100km
            double events_per_100km = (stats.total_harsh_events / stats.total_distance) * 100;
            uint32_t deduction = events_per_100km * 10;

            base_score = (base_score > deduction) ? (base_score - deduction) : 0;
        }

        return base_score;
    }

    void update_driver_stats(const TripRecord &trip)
    {
        DriverProfile driver;
        if (db_.read_driver(trip.driver_id, driver))
        {
            driver.total_trips++;
            driver.total_distance += trip.distance;
            driver.total_fuel_consumed += trip.fuel_consumed;
            driver.harsh_events_count += trip.harsh_braking_count +
                                         trip.rapid_acceleration_count +
                                         trip.speeding_count;

            // Recalculate safety score
            TripStatistics stats;
            stats.total_trips = driver.total_trips;
            stats.total_distance = driver.total_distance;
            stats.total_harsh_events = driver.harsh_events_count;
            driver.safety_score = calculate_safety_score(stats);

            db_.update_driver(driver);
            cache_.invalidate_driver(trip.driver_id);
        }
    }
};

#endif // TRIPMANAGER_H