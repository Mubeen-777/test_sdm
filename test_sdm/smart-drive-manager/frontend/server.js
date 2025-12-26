
const express = require('express');
const path = require('path');
const { spawn } = require('child_process');
const cors = require('cors');
const fs = require('fs');

const app = express();

// Enable CORS for all routes
app.use(cors());

// Serve static files from current directory
app.use(express.static(__dirname));

// Parse JSON bodies
app.use(express.json());

// Fix: Serve index.html for the root route
app.get('/', (req, res) => {
    res.sendFile(path.join(__dirname, 'index.html'));
});

// Fix: Serve login.html directly
app.get('/login', (req, res) => {
    res.sendFile(path.join(__dirname, 'login.html'));
});

// Fix: Serve dashboard.html
app.get('/dashboard', (req, res) => {
    res.sendFile(path.join(__dirname, 'index.html'));
});

// Fix: Serve trips.html
app.get('/trips', (req, res) => {
    res.sendFile(path.join(__dirname, 'index.html'));
});

// Mock API endpoints for frontend testing
app.post('/api/login', (req, res) => {
    console.log('Login attempt:', req.body);
    
    const response = {
        status: 'success',
        data: {
            session_id: 'mock_session_' + Date.now(),
            driver_id: 1,
            name: 'Mubeen Butt',
            role: '1',
            username: req.body.username || 'admin'
        },
        message: 'Login successful'
    };
    
    res.json(response);
});

app.post('/api/verify', (req, res) => {
    console.log('Session verification:', req.body);
    
    const response = {
        status: 'success',
        data: { valid: true, user_id: 1 }
    };
    
    res.json(response);
});

app.get('/api/stats', (req, res) => {
    const stats = {
        speed: Math.floor(Math.random() * 120),
        acceleration: (Math.random() * 6 - 3).toFixed(2),
        safety_score: 850 + Math.floor(Math.random() * 150),
        lane_status: 'CENTERED',
        rapid_accel_count: 2,
        hard_brake_count: 1,
        lane_departures: 0,
        trip_active: false,
        warning_active: false,
        distance: '45.2 km'
    };
    
    res.json(stats);
});

app.post('/api/trip/start', (req, res) => {
    console.log('Starting trip:', req.body);
    
    const response = {
        status: 'success',
        data: {
            trip_id: Date.now(),
            start_time: new Date().toISOString()
        },
        message: 'Trip started successfully'
    };
    
    res.json(response);
});

app.post('/api/trip/stop', (req, res) => {
    console.log('Stopping trip:', req.body);
    
    const response = {
        status: 'success',
        data: {
            trip_id: req.body.trip_id || Date.now(),
            end_time: new Date().toISOString(),
            distance: 45.2,
            duration: '1:30:00'
        },
        message: 'Trip ended successfully'
    };
    
    res.json(response);
});

app.post('/api/camera/start', (req, res) => {
    console.log('Starting camera');
    
    const response = {
        status: 'success',
        data: { camera_active: true },
        message: 'Camera started successfully'
    };
    
    res.json(response);
});

app.post('/api/camera/stop', (req, res) => {
    console.log('Stopping camera');
    
    const response = {
        status: 'success',
        data: { camera_active: false },
        message: 'Camera stopped'
    };
    
    res.json(response);
});

app.get('/api/trips/recent', (req, res) => {
    const trips = {
        data: [
            {
                trip_id: 1,
                start_time: '2024-01-15 09:30:00',
                end_time: '2024-01-15 11:00:00',
                distance: 45.2,
                duration: '1:30:00',
                avg_speed: 60,
                safety_score: 920
            },
            {
                trip_id: 2,
                start_time: '2024-01-14 14:15:00',
                end_time: '2024-01-14 15:30:00',
                distance: 38.5,
                duration: '1:15:00',
                avg_speed: 55,
                safety_score: 880
            },
            {
                trip_id: 3,
                start_time: '2024-01-13 08:45:00',
                end_time: '2024-01-13 10:15:00',
                distance: 52.1,
                duration: '1:30:00',
                avg_speed: 65,
                safety_score: 950
            }
        ]
    };
    
    res.json(trips);
});

app.post('/api/logout', (req, res) => {
    console.log('Logout request');
    
    const response = {
        status: 'success',
        message: 'Logged out successfully'
    };
    
    res.json(response);
});

// Camera stream endpoint - returns a placeholder
app.get('/api/camera/frame', (req, res) => {
    // Return a placeholder image or message
    res.json({
        status: 'success',
        message: 'Camera frame endpoint (mock)',
        timestamp: Date.now()
    });
});

app.get('/api/camera/stream', (req, res) => {
    res.json({
        status: 'success',
        message: 'Camera streaming not implemented in mock server'
    });
});

// Additional endpoints for completeness
app.get('/api/vehicles', (req, res) => {
    const vehicles = {
        data: [
            {
                vehicle_id: 1,
                make: 'Toyota',
                model: 'Corolla',
                year: 2020,
                plate: 'ABC-123',
                mileage: 45230
            },
            {
                vehicle_id: 2,
                make: 'Honda',
                model: 'Civic',
                year: 2019,
                plate: 'XYZ-789',
                mileage: 52450
            }
        ]
    };
    
    res.json(vehicles);
});

app.get('/api/driver/profile', (req, res) => {
    const profile = {
        data: {
            driver_id: 1,
            name: 'Mubeen Butt',
            email: 'mubeen@example.com',
            license_number: 'DL-123456',
            total_trips: 15,
            total_distance: 625.8,
            safety_score: 920,
            join_date: '2023-01-15'
        }
    };
    
    res.json(profile);
});

// FIX: Changed from '/*' to '/:any' for catch-all route
app.get('/:any', (req, res) => {
    // Don't handle API routes here
    if (req.path.startsWith('/api/')) {
        res.status(404).json({ error: 'API endpoint not found' });
        return;
    }
    
    // Check if file exists in static directory
    const filePath = path.join(__dirname, req.path);
    if (fs.existsSync(filePath) && !req.path.includes('.')) {
        // Serve static file if it exists
        res.sendFile(filePath);
    } else {
        // Serve index.html for SPA routing
        res.sendFile(path.join(__dirname, 'index.html'));
    }
});

// Start C++ WebSocket bridge process (optional)
let bridgeProcess = null;

function startBridge() {
    try {
        // Try to find the WebSocket bridge in various locations
        const possiblePaths = [
            path.join(__dirname, '../source/modules/websocket_bridge'),
            path.join(__dirname, 'websocket_bridge'),
            path.join(process.cwd(), 'websocket_bridge')
        ];
        
        let bridgePath = null;
        for (const p of possiblePaths) {
            if (fs.existsSync(p)) {
                bridgePath = p;
                break;
            }
        }
        
        if (bridgePath) {
            console.log('Starting WebSocket bridge from:', bridgePath);
            bridgeProcess = spawn(bridgePath, [], {
                stdio: 'inherit',
                detached: false
            });
            
            bridgeProcess.on('error', (err) => {
                console.error('Failed to start bridge:', err);
            });
            
            bridgeProcess.on('exit', (code) => {
                console.log(`Bridge process exited with code ${code}`);
            });
        } else {
            console.log('WebSocket bridge not found, running in mock mode only');
            console.log('Note: For real-time data, run ./websocket_bridge manually');
        }
    } catch (err) {
        console.log('WebSocket bridge not available:', err.message);
    }
}

// Graceful shutdown
function shutdown() {
    console.log('\nShutting down server...');
    
    if (bridgeProcess) {
        console.log('Stopping WebSocket bridge...');
        bridgeProcess.kill();
    }
    
    process.exit(0);
}

process.on('SIGINT', shutdown);
process.on('SIGTERM', shutdown);

// Start server
const PORT = process.env.PORT || 3000;
app.listen(PORT, () => {
    console.log(`╔══════════════════════════════════════════════════════╗`);
    console.log(`║          SMART DRIVE MANAGER - FRONTEND              ║`);
    console.log(`╠══════════════════════════════════════════════════════╣`);
    console.log(`║  Frontend Server:  http://localhost:${PORT}            `);
    console.log(`║  Login Page:       http://localhost:${PORT}/login      `);
    console.log(`║  Dashboard:        http://localhost:${PORT}/dashboard  `);
    console.log(`╠══════════════════════════════════════════════════════╣`);
    console.log(`║  WebSocket Bridge: ws://localhost:8081               ║`);
    console.log(`╠══════════════════════════════════════════════════════╣`);
    console.log(`║  Login Credentials:                                  ║`);
    console.log(`║    Username: admin                                   ║`);
    console.log(`║    Password: admin123                                ║`);
    console.log(`╚══════════════════════════════════════════════════════╝`);
    
    // Try to start bridge
    startBridge();
});

// Handle uncaught exceptions
process.on('uncaughtException', (err) => {
    console.error('Uncaught Exception:', err);
});