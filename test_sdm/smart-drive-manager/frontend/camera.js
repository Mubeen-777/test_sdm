// camera.js - Camera and Vision System
class CameraManager {
    constructor(app) {
        this.app = app;
        this.visionChart = null;
        this.detectionLog = [];
    }

    initVisionSystem() {
        this.initVisionChart();
        this.setupDetectionOverlay();
        this.startDetectionSimulation();
    }

    initVisionChart() {
        const canvas = document.getElementById('visionChart');
        if (!canvas) return;

        const ctx = canvas.getContext('2d');
        
        this.visionChart = new Chart(ctx, {
            type: 'line',
            data: {
                labels: Array.from({length: 20}, (_, i) => (i + 1).toString()),
                datasets: [
                    {
                        label: 'Lane Deviation %',
                        data: this.generateRandomData(20, 0, 12),
                        borderColor: '#4361ee',
                        backgroundColor: 'rgba(67, 97, 238, 0.1)',
                        fill: true,
                        tension: 0.4
                    },
                    {
                        label: 'Driver Attention %',
                        data: this.generateRandomData(20, 85, 100),
                        borderColor: '#4cc9f0',
                        backgroundColor: 'rgba(76, 201, 240, 0.1)',
                        fill: true,
                        tension: 0.4
                    }
                ]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                plugins: {
                    legend: {
                        display: true,
                        position: 'top'
                    }
                },
                scales: {
                    y: {
                        beginAtZero: true,
                        max: 100,
                        title: {
                            display: true,
                            text: 'Percentage'
                        }
                    },
                    x: {
                        title: {
                            display: true,
                            text: 'Time (seconds)'
                        }
                    }
                }
            }
        });
    }

    setupDetectionOverlay() {
        // Setup lane detection visualization
        const laneDetection = document.getElementById('laneDetection');
        if (laneDetection) {
            // Create lane visualization
            this.createLaneVisualization();
        }
    }

    createLaneVisualization() {
        const container = document.querySelector('.vision-container');
        if (!container) return;

        // Create lane center line
        const centerLine = document.createElement('div');
        centerLine.className = 'lane-center-line';
        container.appendChild(centerLine);

        // Create lane boundaries
        const leftBoundary = document.createElement('div');
        leftBoundary.className = 'lane-boundary lane-left';
        container.appendChild(leftBoundary);

        const rightBoundary = document.createElement('div');
        rightBoundary.className = 'lane-boundary lane-right';
        container.appendChild(rightBoundary);
    }

    startDetectionSimulation() {
        if (this.detectionInterval) clearInterval(this.detectionInterval);

        this.detectionInterval = setInterval(() => {
            if (this.app.isCameraActive && this.app.currentPage === 'camera') {
                this.simulateDetection();
            }
        }, 1000);
    }

    simulateDetection() {
        // Simulate random detection data
        const laneDeviation = Math.random() * 15;
        const objectsDetected = Math.floor(Math.random() * 5);
        const driverAttention = 85 + Math.random() * 15;
        const drowsinessLevel = Math.random() * 10;

        // Update display
        this.updateDetectionDisplay(laneDeviation, objectsDetected, driverAttention, drowsinessLevel);

        // Add to log
        this.addToDetectionLog(laneDeviation, objectsDetected);

        // Update chart
        this.updateVisionChart(laneDeviation, driverAttention);

        // Check for warnings
        this.checkForWarnings(laneDeviation, driverAttention, drowsinessLevel);
    }

    updateDetectionDisplay(laneDeviation, objectsDetected, driverAttention, drowsinessLevel) {
        document.getElementById('laneDeviation').textContent = laneDeviation.toFixed(1) + '%';
        document.getElementById('objectsDetected').textContent = objectsDetected.toString();
        document.getElementById('driverAttention').textContent = driverAttention.toFixed(0) + '%';
        document.getElementById('drowsinessLevel').textContent = drowsinessLevel.toFixed(1) + '%';
    }

    addToDetectionLog(laneDeviation, objectsDetected) {
        const logContainer = document.getElementById('detectionLog');
        if (!logContainer) return;

        const now = new Date();
        const timeString = now.toLocaleTimeString();

        const logEntry = document.createElement('div');
        logEntry.className = 'log-entry';

        let message = '';
        let type = 'info';

        if (laneDeviation > 10) {
            message = `High lane deviation detected: ${laneDeviation.toFixed(1)}%`;
            type = 'warning';
        } else if (objectsDetected > 3) {
            message = `Multiple objects detected: ${objectsDetected}`;
            type = 'info';
        } else {
            message = `Normal driving conditions. Lane deviation: ${laneDeviation.toFixed(1)}%`;
            type = 'info';
        }

        logEntry.innerHTML = `
            <span class="log-time">${timeString}</span>
            <span class="log-type ${type}">${type.toUpperCase()}</span>
            <span class="log-message">${message}</span>
        `;

        logContainer.insertBefore(logEntry, logContainer.firstChild);

        // Limit log entries
        if (logContainer.children.length > 20) {
            logContainer.removeChild(logContainer.lastChild);
        }

        // Store in memory
        this.detectionLog.unshift({
            time: now,
            laneDeviation,
            objectsDetected,
            type,
            message
        });

        if (this.detectionLog.length > 100) {
            this.detectionLog.pop();
        }
    }

    updateVisionChart(laneDeviation, driverAttention) {
        if (!this.visionChart) return;

        // Remove first data point and add new one
        this.visionChart.data.labels.push((this.visionChart.data.labels.length + 1).toString());
        this.visionChart.data.labels.shift();

        this.visionChart.data.datasets[0].data.push(laneDeviation);
        this.visionChart.data.datasets[0].data.shift();

        this.visionChart.data.datasets[1].data.push(driverAttention);
        this.visionChart.data.datasets[1].data.shift();

        this.visionChart.update('none');
    }

    checkForWarnings(laneDeviation, driverAttention, drowsinessLevel) {
        if (laneDeviation > 12) {
            this.app.showAlert(`High lane deviation: ${laneDeviation.toFixed(1)}%`, 'warning');
            this.app.playAlertSound();
        }

        if (driverAttention < 70) {
            this.app.showAlert(`Low driver attention: ${driverAttention.toFixed(0)}%`, 'warning');
            this.app.playAlertSound();
        }

        if (drowsinessLevel > 8) {
            this.app.showAlert(`High drowsiness level: ${drowsinessLevel.toFixed(1)}%`, 'danger');
            this.app.playAlertSound();
        }
    }

    generateRandomData(count, min, max) {
        return Array.from({length: count}, () => 
            Math.floor(Math.random() * (max - min + 1)) + min
        );
    }

    updateObjectDetection(objects) {
        const container = document.getElementById('objectDetection');
        if (!container) return;

        container.innerHTML = '';

        objects.forEach(obj => {
            const box = document.createElement('div');
            box.className = 'object-box';
            box.style.left = `${obj.x}%`;
            box.style.top = `${obj.y}%`;
            box.style.width = `${obj.width}%`;
            box.style.height = `${obj.height}%`;
            box.innerHTML = `<span>${obj.type} ${obj.confidence}%</span>`;
            container.appendChild(box);
        });
    }

    simulateObjectDetection() {
        const objects = [];
        const objectTypes = ['Car', 'Pedestrian', 'Bicycle', 'Truck', 'Motorcycle'];

        // Generate 0-3 random objects
        const numObjects = Math.floor(Math.random() * 4);
        for (let i = 0; i < numObjects; i++) {
            objects.push({
                type: objectTypes[Math.floor(Math.random() * objectTypes.length)],
                x: Math.random() * 70 + 10,
                y: Math.random() * 70 + 10,
                width: Math.random() * 15 + 5,
                height: Math.random() * 15 + 5,
                confidence: Math.floor(Math.random() * 30) + 70
            });
        }

        this.updateObjectDetection(objects);
    }

    // Public methods
    startVisionProcessing() {
        this.startDetectionSimulation();
        this.app.showToast('Vision processing started', 'success');
    }

    stopVisionProcessing() {
        if (this.detectionInterval) {
            clearInterval(this.detectionInterval);
            this.detectionInterval = null;
        }
        this.app.showToast('Vision processing stopped', 'info');
    }

    toggleNightVision() {
        const nightModeToggle = document.getElementById('nightModeToggle');
        const isNightVision = nightModeToggle ? nightModeToggle.checked : false;
        
        const visionFeed = document.getElementById('visionFeed');
        if (visionFeed) {
            if (isNightVision) {
                visionFeed.style.filter = 'grayscale(100%) brightness(0.5) contrast(1.5)';
            } else {
                visionFeed.style.filter = 'none';
            }
        }
        
        this.app.showToast(`Night vision ${isNightVision ? 'enabled' : 'disabled'}`, 'info');
    }
}

// Initialize camera manager when app is ready
if (window.app) {
    window.cameraManager = new CameraManager(window.app);
}