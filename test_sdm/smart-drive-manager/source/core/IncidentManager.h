#ifndef INCIDENTMANAGER_H
#define INCIDENTMANAGER_H

#include "../../include/sdm_types.hpp"
#include "DatabaseManager.h"
#include "CacheManager.h"
#include <vector>
#include<iostream>
#include <string>
using namespace std;

class IncidentManager {
private:
    DatabaseManager& db_;
    CacheManager& cache_;
    
    vector<IncidentReport> incidents_;
    uint64_t next_incident_id_;

    void update_driver_safety_after_incident(uint64_t driver_id, IncidentType type) {
        DriverProfile driver;
        if (!db_.read_driver(driver_id, driver)) {
            return;
        }
        
        uint32_t deduction = 0;
        switch(type) {
            case IncidentType::ACCIDENT:
                deduction = 150;
                cout << "\nSAFETY IMPACT: -150 points (Accident)" << endl;
                break;
            case IncidentType::BREAKDOWN:
                deduction = 0;
                break;
            case IncidentType::THEFT:
                deduction = 0;
                break;
            case IncidentType::VANDALISM:
                deduction = 0;
                break;
            case IncidentType::TRAFFIC_VIOLATION:
                deduction = 100;
                cout << "\nSAFETY IMPACT: -100 points (Traffic Violation)" << endl;
                break;
            default:
                deduction = 50;
                break;
        }
        
        if (driver.safety_score > deduction) {
            driver.safety_score -= deduction;
        } else {
            driver.safety_score = 0;
        }
        
        db_.update_driver(driver);
        cache_.invalidate_driver(driver_id);
        
        cout << "New Safety Score: " << driver.safety_score << "/1000" << endl;
    }

public:
    IncidentManager(DatabaseManager& db, CacheManager& cache)
        : db_(db), cache_(cache) {}
    
    uint64_t report_incident(uint64_t driver_id,
                            uint64_t vehicle_id,
                            IncidentType type,
                            double latitude,
                            double longitude,
                            const string& location_address,
                            const string& description,
                            uint64_t trip_id = 0) {
        uint64_t incident_id = next_incident_id_++;
        
        IncidentReport incident;
        incident.incident_id = incident_id;
        incident.driver_id = driver_id;
        incident.vehicle_id = vehicle_id;
        incident.trip_id = trip_id;
        incident.type = type;
        incident.incident_time = get_current_timestamp();
        incident.latitude = latitude;
        incident.longitude = longitude;
        strncpy(incident.location_address, location_address.c_str(), 
               sizeof(incident.location_address) - 1);
        strncpy(incident.description, description.c_str(), 
               sizeof(incident.description) - 1);
        incident.is_resolved = 0;
        
        incidents_.push_back(incident);
        
        
        update_driver_safety_after_incident(driver_id, type);
        
        return incident_id;
    }
    
    uint64_t report_accident(uint64_t driver_id, uint64_t vehicle_id,
                            double latitude, double longitude,
                            const string& description,
                            const string& other_party_info = "",
                            double estimated_damage = 0.0) {
        uint64_t incident_id = report_incident(driver_id, vehicle_id,
            IncidentType::ACCIDENT, latitude, longitude, "", description);
        
        
        for (auto& inc : incidents_) {
            if (inc.incident_id == incident_id) {
                strncpy(inc.other_party_info, other_party_info.c_str(), 
                       sizeof(inc.other_party_info) - 1);
                inc.estimated_damage = estimated_damage;
                break;
            }
        }
        
        return incident_id;
    }
    
    uint64_t report_breakdown(uint64_t driver_id, uint64_t vehicle_id,
                             double latitude, double longitude,
                             const string& issue_description) {
        return report_incident(driver_id, vehicle_id, IncidentType::BREAKDOWN,
                             latitude, longitude, "", issue_description);
    }
    
    uint64_t report_theft(uint64_t driver_id, uint64_t vehicle_id,
                         double latitude, double longitude,
                         const string& description,
                         const string& police_report_number) {
        uint64_t incident_id = report_incident(driver_id, vehicle_id,
            IncidentType::THEFT, latitude, longitude, "", description);
        
        for (auto& inc : incidents_) {
            if (inc.incident_id == incident_id) {
                strncpy(inc.police_report_number, police_report_number.c_str(),
                       sizeof(inc.police_report_number) - 1);
                break;
            }
        }
        
        return incident_id;
    }
    
    bool add_police_report(uint64_t incident_id, const string& report_number) {
        for (auto& inc : incidents_) {
            if (inc.incident_id == incident_id) {
                strncpy(inc.police_report_number, report_number.c_str(),
                       sizeof(inc.police_report_number) - 1);
                return true;
            }
        }
        return false;
    }
    
    bool add_insurance_claim(uint64_t incident_id,
                            const string& claim_number,
                            double payout_amount) {
        for (auto& inc : incidents_) {
            if (inc.incident_id == incident_id) {
                strncpy(inc.insurance_claim_number, claim_number.c_str(),
                       sizeof(inc.insurance_claim_number) - 1);
                inc.insurance_payout = payout_amount;
                return true;
            }
        }
        return false;
    }
    
    
    bool mark_resolved(uint64_t incident_id) {
        for (auto& inc : incidents_) {
            if (inc.incident_id == incident_id) {
                inc.is_resolved = 1;
                inc.resolved_date = get_current_timestamp();
                return true;
            }
        }
        return false;
    }
    
    vector<IncidentReport> get_driver_incidents(uint64_t driver_id) {
        vector<IncidentReport> result;
        for (const auto& inc : incidents_) {
            if (inc.driver_id == driver_id) {
                result.push_back(inc);
            }
        }
        return result;
    }
    
    vector<IncidentReport> get_vehicle_incidents(uint64_t vehicle_id) {
        vector<IncidentReport> result;
        for (const auto& inc : incidents_) {
            if (inc.vehicle_id == vehicle_id) {
                result.push_back(inc);
            }
        }
        return result;
    }
    
    vector<IncidentReport> get_unresolved_incidents(uint64_t driver_id) {
        vector<IncidentReport> result;
        for (const auto& inc : incidents_) {
            if (inc.driver_id == driver_id && inc.is_resolved == 0) {
                result.push_back(inc);
            }
        }
        return result;
    }
    
    vector<IncidentReport> get_incidents_by_type(uint64_t driver_id,
                                                      IncidentType type) {
        vector<IncidentReport> result;
        for (const auto& inc : incidents_) {
            if (inc.driver_id == driver_id && inc.type == type) {
                result.push_back(inc);
            }
        }
        return result;
    }
    
    struct IncidentStats {
        uint64_t driver_id;
        uint32_t total_incidents;
        uint32_t total_accidents;
        uint32_t total_breakdowns;
        uint32_t total_thefts;
        uint32_t total_violations;
        uint32_t resolved_incidents;
        uint32_t unresolved_incidents;
        double total_damage_cost;
        double total_insurance_payout;
        uint32_t incident_free_days;
    };
    
    IncidentStats get_incident_statistics(uint64_t driver_id) {
        IncidentStats stats = {};
        stats.driver_id = driver_id;
        
        auto incidents = get_driver_incidents(driver_id);
        
        uint64_t last_incident_time = 0;
        
        for (const auto& incident : incidents) {
            stats.total_incidents++;
            
            switch(incident.type) {
                case IncidentType::ACCIDENT:
                    stats.total_accidents++;
                    break;
                case IncidentType::BREAKDOWN:
                    stats.total_breakdowns++;
                    break;
                case IncidentType::THEFT:
                    stats.total_thefts++;
                    break;
                case IncidentType::TRAFFIC_VIOLATION:
                    stats.total_violations++;
                    break;
                default:
                    break;
            }
            
            if (incident.is_resolved) {
                stats.resolved_incidents++;
            } else {
                stats.unresolved_incidents++;
            }
            
            stats.total_damage_cost += incident.estimated_damage;
            stats.total_insurance_payout += incident.insurance_payout;
            
            if (incident.incident_time > last_incident_time) {
                last_incident_time = incident.incident_time;
            }
        }
        
        if (last_incident_time > 0) {
            uint64_t current_time = get_current_timestamp();
            uint64_t days_since = (current_time - last_incident_time) / 
                                 (86400ULL * 1000000000ULL);
            stats.incident_free_days = days_since;
        }
        
        return stats;
    }
    
private:
    uint64_t get_current_timestamp() {
        return chrono::system_clock::now().time_since_epoch().count();
    }
};

#endif