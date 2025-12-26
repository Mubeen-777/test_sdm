// app.js - FIXED VERSION WITH WORKING BUTTONS AND REAL-TIME DATA
class SmartDriveWebApp {
    constructor() {
        this.WS_URL = 'ws://localhost:8081';
        this.sessionId = localStorage.getItem('session_id');
        this.userData = JSON.parse(localStorage.getItem('user_data') || '{}');
        this.ws = null;
        this.isConnected = false;
        this.isCameraActive = false;
        this.isTripActive = false;
        this.currentPage = 'dashboard';
        this.reconnectAttempts = 0;
        this.maxReconnectAttempts = 5;

        // Real-time data from WebSocket
        this.liveData = {
            speed: 0,
            acceleration: 0,
            safety_score: 1000,
            lane_status: 'CENTERED',
            rapid_accel_count: 0,
            hard_brake_count: 0,
            lane_departures: 0,
            trip_active: false,
            latitude: 31.5204,
            longitude: 74.3587
        };

        // FPS tracking
        this.frameCount = 0;
        this.lastFpsUpdate = Date.now();
        this.fpsCounter = 0;

        this.init();
    }

    async init() {
        console.log('🚀 Starting Smart Drive Application...');

        if (!this.checkAuth()) {
            console.log('❌ No session, redirecting to login...');
            window.location.href = 'login.html';
            return;
        }

        this.setupEventListeners();
        this.updateUserUI();
        this.loadPage('dashboard');
        this.connectWebSocket();

        console.log('✅ App initialized');
    }

    checkAuth() {
        return this.sessionId && this.userData.driver_id;
    }

    connectWebSocket() {
        console.log('🔌 Connecting to WebSocket:', this.WS_URL);

        try {
            this.ws = new WebSocket(this.WS_URL);

            this.ws.onopen = () => {
                console.log('✅ WebSocket Connected');
                this.isConnected = true;
                this.reconnectAttempts = 0;
                this.updateConnectionStatus(true);
                this.showToast('Connected to backend system', 'success');

                // Keep-alive ping
                setInterval(() => {
                    if (this.ws && this.ws.readyState === WebSocket.OPEN) {
                        this.ws.send(JSON.stringify({ command: 'ping' }));
                    }
                }, 30000);
            };

            this.ws.onmessage = (event) => {
                try {
                    const data = JSON.parse(event.data);
                    this.handleWebSocketMessage(data);
                } catch (error) {
                    console.error('Error parsing message:', error);
                }
            };

            this.ws.onerror = (error) => {
                console.error('❌ WebSocket error:', error);
                this.updateConnectionStatus(false);
            };

            this.ws.onclose = () => {
                console.log('🔌 WebSocket disconnected');
                this.isConnected = false;
                this.updateConnectionStatus(false);

                if (this.reconnectAttempts < this.maxReconnectAttempts) {
                    this.reconnectAttempts++;
                    console.log(`🔄 Reconnecting... (${this.reconnectAttempts}/${this.maxReconnectAttempts})`);
                    setTimeout(() => this.connectWebSocket(), 3000);
                }
            };

        } catch (error) {
            console.error('❌ WebSocket creation failed:', error);
            this.showToast('Cannot connect to backend. Check if websocket_bridge is running.', 'error');
        }
    }

    handleWebSocketMessage(data) {
        switch (data.type) {
            case 'video_frame':
                this.displayVideoFrame(data.data, data.timestamp);
                break;

            case 'live_data':
                this.updateLiveData(data.data);
                break;

            case 'lane_warning':
                this.showToast(`⚠️ LANE DEPARTURE: ${data.data.direction}`, 'warning');
                this.playAlertSound();
                this.liveData.lane_departures++;
                this.updateDashboard();
                break;

            case 'warning':
                this.showToast(`⚠️ ${data.data.warning_type}: ${data.data.value.toFixed(2)}`, 'warning');
                this.playAlertSound();
                break;

            case 'trip_started':
                this.isTripActive = true;
                this.liveData.trip_active = true;
                this.updateTripControls();
                this.showToast('Trip started successfully!', 'success');
                break;

            case 'trip_stopped':
                this.isTripActive = false;
                this.liveData.trip_active = false;
                this.updateTripControls();
                this.showToast(`Trip ended. Distance: ${data.data.distance || 'N/A'}`, 'success');
                break;

            case 'camera_status':
                this.isCameraActive = data.data.enabled;
                this.updateCameraStatus(data.data.enabled);
                break;
        }
    }

    updateLiveData(data) {
        Object.assign(this.liveData, data);
        this.updateDashboard();
    }

    // In app.js, replace the displayVideoFrame function:

    displayVideoFrame(base64Data, timestamp) {
        // For dashboard
        const videoFeed = document.getElementById('videoFeed');
        const noVideo = document.getElementById('noVideo');

        if (videoFeed && noVideo) {
            videoFeed.src = 'data:image/jpeg;base64,' + base64Data;
            videoFeed.style.display = 'block';
            noVideo.style.display = 'none';
        }

        // For vision page (camera template)
        const visionFeed = document.getElementById('visionFeed');
        const visionPlaceholder = document.getElementById('visionPlaceholder');

        if (visionFeed && visionPlaceholder) {
            visionFeed.src = 'data:image/jpeg;base64,' + base64Data;
            visionFeed.style.display = 'block';
            visionPlaceholder.style.display = 'none';
        }

        // Update FPS
        this.frameCount++;
        this.fpsCounter++;
        const now = Date.now();
        const elapsed = now - this.lastFpsUpdate;

        if (elapsed >= 1000) {
            const fps = Math.round(this.fpsCounter * 1000 / elapsed);

            // Dashboard FPS
            const fpsDisplay = document.getElementById('fpsDisplay');
            if (fpsDisplay) fpsDisplay.textContent = `FPS: ${fps}`;

            // Vision page FPS
            const visionFps = document.getElementById('visionFps');
            if (visionFps) visionFps.textContent = `FPS: ${fps}`;

            this.fpsCounter = 0;
            this.lastFpsUpdate = now;
        }
    }

    // BUTTON HANDLERS - These are the real working functions
    startTrip() {
        if (!this.isConnected) {
            this.showToast('Not connected to backend!', 'error');
            return;
        }

        const vehicleId = document.getElementById('vehicleSelect')?.value || 1;

        this.ws.send(JSON.stringify({
            command: 'start_trip',
            driver_id: this.userData.driver_id || 1,
            vehicle_id: parseInt(vehicleId)
        }));

        this.showToast('Starting trip...', 'info');
    }

    stopTrip() {
        if (!this.isConnected) {
            this.showToast('Not connected to backend!', 'error');
            return;
        }

        this.ws.send(JSON.stringify({
            command: 'stop_trip'
        }));

        this.showToast('Stopping trip...', 'info');
    }

    toggleCamera() {
        if (!this.isConnected) {
            this.showToast('Not connected to backend!', 'error');
            return;
        }

        this.ws.send(JSON.stringify({
            command: 'toggle_camera',
            enable: !this.isCameraActive
        }));

        this.showToast(this.isCameraActive ? 'Stopping camera...' : 'Starting camera...', 'info');
    }

    // UI UPDATE FUNCTIONS
    updateDashboard() {
        // Update speed
        const speedValue = document.getElementById('speedValue');
        if (speedValue) speedValue.textContent = Math.round(this.liveData.speed);

        // Update acceleration
        const accelValue = document.getElementById('accelValue');
        if (accelValue) accelValue.textContent = this.liveData.acceleration.toFixed(2);

        // Update safety score
        const safetyScore = document.getElementById('safetyScore');
        if (safetyScore) safetyScore.textContent = this.liveData.safety_score;

        // Update GPS
        const gpsLat = document.getElementById('gpsLat');
        const gpsLon = document.getElementById('gpsLon');
        if (gpsLat) gpsLat.textContent = this.liveData.latitude.toFixed(6) + '°';
        if (gpsLon) gpsLon.textContent = this.liveData.longitude.toFixed(6) + '°';

        // Update event counters
        const harshBrakeCount = document.getElementById('harshBrakeCount');
        if (harshBrakeCount) harshBrakeCount.textContent = this.liveData.hard_brake_count;

        const rapidAccelCount = document.getElementById('rapidAccelCount');
        if (rapidAccelCount) rapidAccelCount.textContent = this.liveData.rapid_accel_count;

        const laneDepartureCount = document.getElementById('laneDepartureCount');
        if (laneDepartureCount) laneDepartureCount.textContent = this.liveData.lane_departures;

        // Update top bar stats
        const liveSpeed = document.getElementById('liveSpeed');
        const liveAccel = document.getElementById('liveAccel');
        const liveSafety = document.getElementById('liveSafety');

        if (liveSpeed) liveSpeed.textContent = Math.round(this.liveData.speed);
        if (liveAccel) liveAccel.textContent = this.liveData.acceleration.toFixed(1);
        if (liveSafety) liveSafety.textContent = this.liveData.safety_score;

        // Draw speedometer
        this.drawSpeedometer(this.liveData.speed);
    }

    drawSpeedometer(speed) {
        const canvas = document.getElementById('speedometer');
        if (!canvas) return;

        const ctx = canvas.getContext('2d');
        const centerX = canvas.width / 2;
        const centerY = canvas.height / 2;
        const radius = 100;

        ctx.clearRect(0, 0, canvas.width, canvas.height);

        // Outer circle
        ctx.beginPath();
        ctx.arc(centerX, centerY, radius, 0, 2 * Math.PI);
        ctx.strokeStyle = 'rgba(67, 97, 238, 0.2)';
        ctx.lineWidth = 15;
        ctx.stroke();

        // Speed arc
        const maxSpeed = 200;
        const speedAngle = (Math.min(speed, maxSpeed) / maxSpeed) * 1.5 * Math.PI - 0.75 * Math.PI;
        const color = speed < 60 ? '#4cc9f0' : speed < 100 ? '#f8961e' : '#f72585';

        ctx.beginPath();
        ctx.arc(centerX, centerY, radius, -0.75 * Math.PI, speedAngle);
        ctx.strokeStyle = color;
        ctx.lineWidth = 15;
        ctx.stroke();

        // Center dot
        ctx.beginPath();
        ctx.arc(centerX, centerY, 10, 0, 2 * Math.PI);
        ctx.fillStyle = color;
        ctx.fill();

        // Needle
        ctx.save();
        ctx.translate(centerX, centerY);
        ctx.rotate(speedAngle);
        ctx.beginPath();
        ctx.moveTo(0, 0);
        ctx.lineTo(radius - 25, 0);
        ctx.strokeStyle = '#333';
        ctx.lineWidth = 4;
        ctx.stroke();
        ctx.restore();

        // Speed markers
        ctx.fillStyle = '#666';
        ctx.font = '12px Arial';
        ctx.textAlign = 'center';

        for (let i = 0; i <= 200; i += 40) {
            const angle = (i / maxSpeed) * 1.5 * Math.PI - 0.75 * Math.PI;
            const x = centerX + (radius + 20) * Math.cos(angle);
            const y = centerY + (radius + 20) * Math.sin(angle);
            ctx.fillText(i, x, y);
        }
    }

    updateTripControls() {
        const startBtn = document.getElementById('startTripBtn');
        const stopBtn = document.getElementById('stopTripBtn');
        const tripStatus = document.getElementById('tripStatus');

        if (startBtn) startBtn.disabled = this.isTripActive;
        if (stopBtn) stopBtn.disabled = !this.isTripActive;
        if (tripStatus) {
            tripStatus.textContent = this.isTripActive ? 'Active' : 'Inactive';
            tripStatus.parentElement.classList.toggle('active', this.isTripActive);
        }
    }

    // In app.js, update the updateCameraStatus function:

    updateCameraStatus(enabled) {
        this.isCameraActive = enabled;

        // Update dashboard camera button
        const cameraToggleBtn = document.getElementById('cameraToggleBtn');
        const cameraStatusBadge = document.getElementById('cameraStatusBadge');

        if (cameraToggleBtn && cameraStatusBadge) {
            if (enabled) {
                cameraToggleBtn.innerHTML = '<i class="fas fa-video-slash"></i> Stop Camera';
                cameraToggleBtn.className = 'btn btn-danger btn-sm';
                cameraStatusBadge.textContent = 'Active';
                cameraStatusBadge.className = 'status-badge success';
            } else {
                cameraToggleBtn.innerHTML = '<i class="fas fa-video"></i> Start Camera';
                cameraToggleBtn.className = 'btn btn-primary btn-sm';
                cameraStatusBadge.textContent = 'Off';
                cameraStatusBadge.className = 'status-badge';

                // Hide video feed
                const videoFeed = document.getElementById('videoFeed');
                const noVideo = document.getElementById('noVideo');
                if (videoFeed && noVideo) {
                    videoFeed.style.display = 'none';
                    noVideo.style.display = 'block';
                }
            }
        }

        // Update vision page camera button
        const visionCameraBtn = document.getElementById('visionCameraBtn');
        if (visionCameraBtn) {
            if (enabled) {
                visionCameraBtn.innerHTML = '<i class="fas fa-video-slash"></i> Stop Camera';
                visionCameraBtn.className = 'btn btn-danger';
            } else {
                visionCameraBtn.innerHTML = '<i class="fas fa-video"></i> Start Camera';
                visionCameraBtn.className = 'btn btn-primary';
            }
        }
    }

    updateConnectionStatus(connected) {
        this.isConnected = connected;
        const statusDot = document.getElementById('wsStatusDot');
        const statusText = document.getElementById('wsStatus');

        if (statusDot) {
            if (connected) {
                statusDot.classList.add('connected');
            } else {
                statusDot.classList.remove('connected');
            }
        }

        if (statusText) {
            statusText.textContent = connected ? 'Connected' : 'Disconnected';
        }
    }

    // PAGE NAVIGATION
    loadPage(pageName) {
        this.currentPage = pageName;

        // Update active nav
        document.querySelectorAll('.nav-item').forEach(item => {
            item.classList.remove('active');
            if (item.dataset.page === pageName) {
                item.classList.add('active');
            }
        });

        // Update page title
        const titles = {
            dashboard: 'Dashboard',
            lane_detection: 'Lane Detection System',
            trips: 'Trip Management',
            vehicles: 'Vehicle Management',
            expenses: 'Expense Tracking',
            drivers: 'Driver Profiles',
            incidents: 'Incident Management',
            reports: 'Analytics & Reports',
            settings: 'Settings'
        };

        const title = document.getElementById('pageTitle');
        const subtitle = document.getElementById('pageSubtitle');

        if (title) title.textContent = titles[pageName] || pageName;
        if (subtitle) {
            const subtitles = {
                dashboard: 'Real-time monitoring and GPS tracking',
                lane_detection: 'Computer vision and lane departure warnings',
                trips: 'View and manage your driving trips',
                vehicles: 'Manage your vehicles and maintenance',
                expenses: 'Track and manage vehicle expenses',
                drivers: 'Driver profiles and behavior',
                incidents: 'Report and track incidents',
                reports: 'Comprehensive analytics and reports',
                settings: 'System configuration'
            };
            subtitle.textContent = subtitles[pageName] || '';
        }

        // Load page content
        const template = document.getElementById(pageName + 'Template');
        const contentArea = document.getElementById('contentArea');

        if (template && contentArea) {
            contentArea.innerHTML = template.innerHTML;
            this.initPageComponents(pageName);
        }
    }

    initPageComponents(pageName) {
        switch (pageName) {
            case 'dashboard':
                this.initDashboard();
                break;
            case 'lane_detection':
                this.initLaneDetection();
                break;
        }
    }

    initDashboard() {
        // Initialize speedometer
        this.drawSpeedometer(0);

        // Check if we have live data already
        if (this.liveData.speed > 0) {
            this.drawSpeedometer(this.liveData.speed);
        }

        this.updateDashboard();
        this.updateTripControls();

        // Update camera button based on current status
        this.updateCameraStatus(this.isCameraActive);
    }

    initLaneDetection() {
        // Lane detection page initialized
        if (this.isCameraActive) {
            const noVideo = document.getElementById('laneNoVideo');
            if (noVideo) noVideo.style.display = 'none';
        }
    }

    // EVENT LISTENERS
    setupEventListeners() {
        // Navigation
        document.querySelectorAll('.nav-item').forEach(item => {
            item.addEventListener('click', () => {
                this.loadPage(item.dataset.page);
            });
        });

        // Logout
        const logoutBtn = document.querySelector('.btn-logout');
        if (logoutBtn) {
            logoutBtn.addEventListener('click', () => this.logout());
        }
    }

    updateUserUI() {
        const userName = document.getElementById('userName');
        const userAvatar = document.getElementById('userAvatar');

        if (userName) userName.textContent = this.userData.name || 'User';
        if (userAvatar) {
            const initials = (this.userData.name || 'U').split(' ').map(n => n[0]).join('');
            userAvatar.textContent = initials;
        }
    }

    // UTILITIES
    showToast(message, type = 'info') {
        const container = document.getElementById('toastContainer') || this.createToastContainer();

        const toast = document.createElement('div');
        toast.className = `toast ${type}`;

        const icons = {
            success: 'fa-check-circle',
            error: 'fa-exclamation-circle',
            warning: 'fa-exclamation-triangle',
            info: 'fa-info-circle'
        };

        toast.innerHTML = `
            <i class="fas ${icons[type] || icons.info}"></i>
            <span>${message}</span>
        `;

        container.appendChild(toast);

        setTimeout(() => toast.remove(), 5000);
    }

    createToastContainer() {
        const container = document.createElement('div');
        container.id = 'toastContainer';
        container.className = 'toast-container';
        document.body.appendChild(container);
        return container;
    }

    playAlertSound() {
        try {
            const ctx = new (window.AudioContext || window.webkitAudioContext)();
            const osc = ctx.createOscillator();
            const gain = ctx.createGain();

            osc.connect(gain);
            gain.connect(ctx.destination);

            osc.frequency.value = 800;
            osc.type = 'sine';
            gain.gain.value = 0.3;

            osc.start();
            setTimeout(() => osc.stop(), 200);
        } catch (e) {
            console.log('Audio not available');
        }
    }

    logout() {
        if (this.ws) this.ws.close();
        localStorage.clear();
        window.location.href = 'login.html';
    }
}

// Initialize when DOM is ready
let app;
document.addEventListener('DOMContentLoaded', () => {
    console.log('📱 DOM loaded, initializing app...');
    app = new SmartDriveWebApp();

    // Make app globally available
    window.app = app;

    // Make button functions globally available
    window.startTrip = () => app.startTrip();
    window.stopTrip = () => app.stopTrip();
    window.toggleCamera = () => app.toggleCamera();
    window.logout = () => app.logout();
});