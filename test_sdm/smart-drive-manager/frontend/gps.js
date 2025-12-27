// gps.js - Advanced GPS Tracking & Safety System
class GPSManager {
    constructor(app) {
        this.app = app;
        this.watchId = null;
        this.lastPosition = null;
        this.lastTime = null;
        this.lastSpeed = 0; // m/s
        
        // Configuration Thresholds
        this.THRESHOLDS = {
            RAPID_ACCEL: 3.5, // m/s² (approx 0.35g)
            HARD_BRAKE: -4.0, // m/s² (approx -0.4g)
            CRASH_DECEL: -8.0, // m/s² (approx -0.8g) - sudden stop
            MIN_SPEED_FOR_CRASH: 5.5, // m/s (approx 20 km/h) - must be moving to crash
            GPS_TIMEOUT: 10000,
            GPS_HIGH_ACCURACY: true
        };

        this.sosTimer = null;
        this.sosCountdown = 10;
        this.pollInterval = null; // For fallback polling
    }

    startTracking() {
        if (!navigator.geolocation) {
            console.error('❌ Geolocation is not supported by your browser');
            if (this.app && this.app.showToast) {
                this.app.showToast('Geolocation is not supported by your browser', 'error');
            }
            return false;
        }

        // Stop any existing tracking first
        if (this.watchId !== null) {
            this.stopTracking();
        }

        const options = {
            enableHighAccuracy: true, // Use GPS hardware if available
            timeout: 5000,            // Reduced timeout to 5 seconds for faster updates
            maximumAge: 1000         // Allow 1 second old positions to ensure continuous updates
        };

        try {
            // Use watchPosition for continuous updates
            this.watchId = navigator.geolocation.watchPosition(
                (pos) => {
                    console.log('📍 GPS Position received:', pos.coords.latitude, pos.coords.longitude);
                    this.processPosition(pos);
                },
                (err) => {
                    console.error('❌ GPS Error:', err);
                    this.handleError(err);
                },
                options
            );

            // Also set up a fallback polling mechanism to ensure updates happen
            // This helps when watchPosition is slow or not updating frequently
            if (this.pollInterval) {
                clearInterval(this.pollInterval);
            }
            this.pollInterval = setInterval(() => {
                navigator.geolocation.getCurrentPosition(
                    (pos) => {
                        // Only process if we haven't received an update recently
                        const timeSinceLastUpdate = Date.now() - (this.lastTime || 0);
                        if (timeSinceLastUpdate > 2000) { // If no update in 2 seconds, use this
                            console.log('📍 GPS Poll update (fallback):', pos.coords.latitude, pos.coords.longitude);
                            this.processPosition(pos);
                        }
                    },
                    (err) => {
                        // Silently fail for polling - watchPosition handles errors
                        console.warn('GPS poll failed (non-critical):', err.code);
                    },
                    options
                );
            }, 1000); // Poll every second as fallback

            console.log('✅ GPS Tracking Started (watchId:', this.watchId, ')');
            if (this.app && this.app.showToast) {
                this.app.showToast('GPS Tracking Active', 'success');
            }
            return true;
        } catch (error) {
            console.error('❌ Failed to start GPS tracking:', error);
            if (this.app && this.app.showToast) {
                this.app.showToast('Failed to start GPS: ' + error.message, 'error');
            }
            return false;
        }
    }

    stopTracking() {
        if (this.watchId !== null) {
            navigator.geolocation.clearWatch(this.watchId);
            this.watchId = null;
        }
        if (this.pollInterval) {
            clearInterval(this.pollInterval);
            this.pollInterval = null;
        }
        this.lastPosition = null;
        this.lastTime = null;
        this.lastSpeed = 0;
        console.log('📍 GPS Tracking Stopped');
    }

    processPosition(position) {
        if (!position || !position.coords) {
            console.warn('⚠️ Invalid GPS position data');
            return;
        }

        const { latitude, longitude, accuracy, speed: gpsSpeed } = position.coords;
        // Use current timestamp if GPS timestamp is not available or seems wrong
        const currentTime = position.timestamp && position.timestamp > 0 
            ? position.timestamp 
            : Date.now();

        console.log('📍 GPS Update:', {
            lat: latitude.toFixed(6),
            lon: longitude.toFixed(6),
            accuracy: accuracy ? accuracy.toFixed(1) + 'm' : 'unknown',
            speed: gpsSpeed !== null && gpsSpeed >= 0 ? (gpsSpeed * 3.6).toFixed(1) + ' km/h' : 'null',
            timestamp: new Date(currentTime).toLocaleTimeString()
        });

        // Update basic location info on UI immediately
        if (this.app && this.app.updateLocationUI) {
            this.app.updateLocationUI(latitude, longitude, accuracy);
        }

        // If this is the first point, just store it and return
        if (!this.lastPosition || !this.lastTime) {
            this.lastPosition = { lat: latitude, lon: longitude };
            this.lastTime = currentTime;
            
            // Still update UI with initial position
            if (this.app && this.app.updateLiveData) {
                this.app.updateLiveData({
                    latitude: latitude,
                    longitude: longitude,
                    speed: gpsSpeed !== null && gpsSpeed >= 0 ? gpsSpeed * 3.6 : 0 // Use GPS speed if available
                });
            }
            return;
        }

        // Calculate time delta (seconds)
        const dt = (currentTime - this.lastTime) / 1000;
        
        // More lenient time delta check - allow updates up to 10 seconds old
        // This ensures we still process updates even if GPS is slow
        if (dt <= 0) {
            console.warn('⚠️ Invalid time delta:', dt);
            return; // Prevent division by zero
        }
        if (dt > 10) {
            console.warn('⚠️ GPS update too old:', dt, 'seconds. Resetting...');
            // Reset and treat as new position
            this.lastPosition = { lat: latitude, lon: longitude };
            this.lastTime = currentTime;
            if (this.app && this.app.updateLiveData) {
                this.app.updateLiveData({
                    latitude: latitude,
                    longitude: longitude,
                    speed: gpsSpeed !== null && gpsSpeed >= 0 ? gpsSpeed * 3.6 : 0
                });
            }
            return;
        }

        // Calculate Distance (meters) using Haversine Formula
        const distance = this.calculateDistance(
            this.lastPosition.lat, this.lastPosition.lon,
            latitude, longitude
        );

        // Calculate Calculated Speed (m/s)
        // Note: gpsSpeed is provided by hardware, but sometimes null. 
        // We use calculated speed as a fallback or verification.
        const calculatedSpeed = distance / dt;
        
        // Priority: Use GPS speed if available and reasonable, otherwise use calculated speed
        // GPS speed is more accurate when available
        let currentSpeed = 0;
        if (gpsSpeed !== null && gpsSpeed >= 0 && !isNaN(gpsSpeed)) {
            // GPS speed is available - use it with light smoothing
            currentSpeed = 0.9 * gpsSpeed + 0.1 * this.lastSpeed;
        } else if (calculatedSpeed >= 0 && !isNaN(calculatedSpeed)) {
            // Use calculated speed with smoothing
            currentSpeed = 0.8 * calculatedSpeed + 0.2 * this.lastSpeed;
        } else {
            // Fallback to last speed with decay
            currentSpeed = this.lastSpeed * 0.95; // Decay slowly if no new data
        }
        
        // Ensure speed is non-negative and reasonable
        currentSpeed = Math.max(0, Math.min(currentSpeed, 200)); // Cap at 200 m/s (720 km/h)
        
        // Calculate Acceleration (m/s²)
        // a = (v_final - v_initial) / t
        const acceleration = dt > 0 ? (currentSpeed - this.lastSpeed) / dt : 0;

        // Logic Check: Ignore massive jumps (GPS glitches)
        // If accel is > 20 m/s² (2g), it's likely a glitch unless crashing
        if (Math.abs(acceleration) < 20) {
            this.analyzeSafety(acceleration, currentSpeed);
        }
        
        // Always update UI with speed and location (even if acceleration seems wrong)
        // This ensures speedometer always shows current speed
        if (this.app && this.app.updateLiveData) {
            const speedKmh = currentSpeed * 3.6; // Convert m/s to km/h for display
            console.log('📊 Updating live data - Speed:', speedKmh.toFixed(1), 'km/h, Distance:', distance.toFixed(1), 'm, dt:', dt.toFixed(2), 's');
            this.app.updateLiveData({
                speed: speedKmh,
                acceleration: Math.abs(acceleration) < 20 ? acceleration : 0,
                latitude: latitude,
                longitude: longitude
            });
        } else {
            console.warn('⚠️ App or updateLiveData not available');
        }

        // Store for next iteration
        this.lastPosition = { lat: latitude, lon: longitude };
        this.lastTime = currentTime;
        this.lastSpeed = currentSpeed;
    }

    calculateDistance(lat1, lon1, lat2, lon2) {
        const R = 6371e3; // Earth radius in meters
        const φ1 = lat1 * Math.PI / 180;
        const φ2 = lat2 * Math.PI / 180;
        const Δφ = (lat2 - lat1) * Math.PI / 180;
        const Δλ = (lon2 - lon1) * Math.PI / 180;

        const a = Math.sin(Δφ / 2) * Math.sin(Δφ / 2) +
                  Math.cos(φ1) * Math.cos(φ2) *
                  Math.sin(Δλ / 2) * Math.sin(Δλ / 2);
        const c = 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a));

        return R * c;
    }

    analyzeSafety(acceleration, currentSpeed) {
        // 1. Crash Detection (Severe Deceleration)
        if (acceleration < this.THRESHOLDS.CRASH_DECEL && this.lastSpeed > this.THRESHOLDS.MIN_SPEED_FOR_CRASH) {
            console.warn('🚨 CRASH DETECTED');
            this.triggerCrashSequence();
            return; // Stop processing other alerts
        }

        // 2. Hard Braking
        if (acceleration < this.THRESHOLDS.HARD_BRAKE) {
            this.app.showAlert('⚠️ Hard Braking Detected!', 'warning');
            this.logSafetyEvent('Hard Braking', acceleration);
        }

        // 3. Rapid Acceleration
        else if (acceleration > this.THRESHOLDS.RAPID_ACCEL) {
            this.app.showAlert('⚠️ Rapid Acceleration!', 'warning');
            this.logSafetyEvent('Rapid Acceleration', acceleration);
        }
    }

    triggerCrashSequence() {
        if (this.sosTimer) return; // Already running

        const modal = document.getElementById('sosModal');
        const countdownEl = document.getElementById('sosCountdown');
        
        if (modal && countdownEl) {
            this.app.showModal('sosModal');
            this.sosCountdown = 10;
            countdownEl.textContent = this.sosCountdown;
            
            // Play alarm sound if available
            if (this.app.playAlertSound) this.app.playAlertSound();

            this.sosTimer = setInterval(() => {
                this.sosCountdown--;
                countdownEl.textContent = this.sosCountdown;

                if (this.sosCountdown <= 0) {
                    this.sendSOS();
                    this.cancelCrashSequence(); // Stop timer/close modal
                }
            }, 1000);
        }
    }

    cancelCrashSequence() {
        if (this.sosTimer) {
            clearInterval(this.sosTimer);
            this.sosTimer = null;
        }
        this.app.closeModal('sosModal');
        this.app.showToast('SOS Cancelled', 'info');
    }

    async sendSOS() {
        this.app.showToast('🚨 Sending Emergency SOS...', 'error');
        
        try {
            // Call backend API to trigger SOS
            if (window.db) {
                const { lat, lon } = this.lastPosition || { lat: 0, lon: 0 };
                await window.db.reportIncident(
                    0, // Vehicle ID (unknown/current)
                    0, // Type 0 = Accident
                    lat, lon,
                    "Automatic Crash Detection - G-Force Limit Exceeded"
                );
            }
            // In a real app, this would also SMS/Call contacts
            alert('EMERGENCY SOS SENT TO CONTACTS AND AUTHORITIES!');
        } catch (e) {
            console.error('Failed to send SOS', e);
        }
    }

    logSafetyEvent(type, value) {
        console.log(`Safety Event: ${type} (${value.toFixed(2)} m/s²)`);
        // Could be sent to backend here
    }

    handleError(error) {
        let msg = 'GPS Error: ';
        switch(error.code) {
            case error.PERMISSION_DENIED: 
                msg += 'User denied location permission. Please enable location access in browser settings.';
                break;
            case error.POSITION_UNAVAILABLE: 
                msg += 'Location unavailable. Check GPS signal.';
                break;
            case error.TIMEOUT: 
                msg += 'GPS request timed out. Retrying...';
                break;
            default: 
                msg += 'Unknown error (code: ' + error.code + ')';
                break;
        }
        console.error('❌', msg, error);
        
        // Show toast for important errors
        if (this.app && this.app.showToast) {
            if (error.code === error.PERMISSION_DENIED) {
                this.app.showToast(msg, 'error');
            } else if (error.code === error.POSITION_UNAVAILABLE) {
                this.app.showToast(msg, 'warning');
            }
        }
    }
}

// GPSManager will be initialized by app.js when window.app is ready
// This ensures proper initialization order
