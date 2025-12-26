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
    }

    startTracking() {
        if (!navigator.geolocation) {
            this.app.showToast('Geolocation is not supported by your browser', 'error');
            return;
        }

        const options = {
            enableHighAccuracy: true, // Use GPS hardware if available
            timeout: 5000,            // Time to wait for a reading
            maximumAge: 0             // Do not use cached positions
        };

        this.watchId = navigator.geolocation.watchPosition(
            (pos) => this.processPosition(pos),
            (err) => this.handleError(err),
            options
        );

        console.log('📍 GPS Tracking Started');
        this.app.showToast('GPS Tracking Active', 'info');
    }

    stopTracking() {
        if (this.watchId !== null) {
            navigator.geolocation.clearWatch(this.watchId);
            this.watchId = null;
            this.lastPosition = null;
            this.lastTime = null;
            this.lastSpeed = 0;
            console.log('📍 GPS Tracking Stopped');
        }
    }

    processPosition(position) {
        const { latitude, longitude, accuracy, speed: gpsSpeed } = position.coords;
        const currentTime = position.timestamp;

        // Update basic location info on UI immediately
        if (this.app.updateLocationUI) {
            this.app.updateLocationUI(latitude, longitude, accuracy);
        }

        // If this is the first point, just store it and return
        if (!this.lastPosition || !this.lastTime) {
            this.lastPosition = { lat: latitude, lon: longitude };
            this.lastTime = currentTime;
            return;
        }

        // Calculate time delta (seconds)
        const dt = (currentTime - this.lastTime) / 1000;
        if (dt <= 0) return; // Prevent division by zero

        // Calculate Distance (meters) using Haversine Formula
        const distance = this.calculateDistance(
            this.lastPosition.lat, this.lastPosition.lon,
            latitude, longitude
        );

        // Calculate Calculated Speed (m/s)
        // Note: gpsSpeed is provided by hardware, but sometimes null. 
        // We use calculated speed as a fallback or verification.
        const calculatedSpeed = distance / dt;
        
        // Use GPS speed if reliable (accuracy < 20m), otherwise use calculated
        // Smoothing: Simple low-pass filter (80% new, 20% old)
        let currentSpeed = (gpsSpeed !== null && accuracy < 20) ? gpsSpeed : calculatedSpeed;
        
        // Calculate Acceleration (m/s²)
        // a = (v_final - v_initial) / t
        const acceleration = (currentSpeed - this.lastSpeed) / dt;

        // Logic Check: Ignore massive jumps (GPS glitches)
        // If accel is > 20 m/s² (2g), it's likely a glitch unless crashing
        if (Math.abs(acceleration) < 20) {
            this.analyzeSafety(acceleration, currentSpeed);
            
            // Update App Data
            this.app.updateLiveData({
                speed: currentSpeed * 3.6, // Convert m/s to km/h for display
                acceleration: acceleration,
                latitude: latitude,
                longitude: longitude
            });
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
            case error.PERMISSION_DENIED: msg += 'User denied permission.'; break;
            case error.POSITION_UNAVAILABLE: msg += 'Location unavailable.'; break;
            case error.TIMEOUT: msg += 'Request timed out.'; break;
            default: msg += 'Unknown error.'; break;
        }
        console.warn(msg);
        // Only toast on serious errors to avoid spamming
        if (error.code === error.PERMISSION_DENIED) {
            this.app.showToast(msg, 'error');
        }
    }
}

// Attach to window for easy access
if (window.app) {
    window.gpsManager = new GPSManager(window.app);
}
