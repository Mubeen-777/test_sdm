
const express = require('express');
const path = require('path');
const { spawn } = require('child_process');
const cors = require('cors');
const fs = require('fs');
const http = require('http');

const app = express();

// Enable CORS for all routes
app.use(cors());

// Serve static files from current directory
app.use(express.static(__dirname));

// Parse JSON bodies
app.use(express.json());

// Backend server configuration
const BACKEND_HOST = 'localhost';
const BACKEND_PORT = 8080;

// API Proxy - Forward all POST requests to /api to the C++ backend
app.post('/api', (req, res) => {
    const postData = JSON.stringify(req.body);
    
    const options = {
        hostname: BACKEND_HOST,
        port: BACKEND_PORT,
        path: '/',
        method: 'POST',
        headers: {
            'Content-Type': 'application/json',
            'Content-Length': Buffer.byteLength(postData)
        },
        timeout: 10000
    };

    const proxyReq = http.request(options, (proxyRes) => {
        let data = '';
        proxyRes.on('data', (chunk) => { data += chunk; });
        proxyRes.on('end', () => {
            res.setHeader('Content-Type', 'application/json');
            res.status(proxyRes.statusCode).send(data);
        });
    });

    proxyReq.on('error', (err) => {
        console.error('Backend connection error:', err.message);
        res.status(503).json({
            status: 'error',
            code: 'BACKEND_UNAVAILABLE',
            message: 'Cannot connect to backend server. Make sure the C++ server is running on port 8080.'
        });
    });

    proxyReq.on('timeout', () => {
        proxyReq.destroy();
        res.status(504).json({
            status: 'error',
            code: 'BACKEND_TIMEOUT',
            message: 'Backend server request timed out'
        });
    });

    proxyReq.write(postData);
    proxyReq.end();
});

// Redirect root to login page (authentication happens client-side)
app.get('/', (req, res) => {
    res.sendFile(path.join(__dirname, 'login.html'));
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

// Catch-all route for SPA routing (must be last)
app.use((req, res, next) => {
    // Serve index.html for all other routes (SPA routing)
    res.sendFile(path.join(__dirname, 'index.html'));
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
            console.log('WebSocket bridge not found - real-time features require websocket_bridge to be running');
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
    console.log(`║  Backend API:      http://localhost:8080              ║`);
    console.log(`║  WebSocket Bridge: ws://localhost:8081               ║`);
    console.log(`╠══════════════════════════════════════════════════════╣`);
    console.log(`║  NOTE: Make sure the C++ backend server is running   ║`);
    console.log(`║        on port 8080 before using the frontend        ║`);
    console.log(`╚══════════════════════════════════════════════════════╝`);
    
    // Try to start bridge
    startBridge();
});

// Handle uncaught exceptions
process.on('uncaughtException', (err) => {
    console.error('Uncaught Exception:', err);
});