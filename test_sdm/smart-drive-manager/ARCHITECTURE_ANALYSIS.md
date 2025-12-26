# Smart Drive Manager - Architecture Analysis

## Overview
Smart Drive Manager is a comprehensive fleet management system with a C++ backend database and a JavaScript frontend. The system tracks drivers, vehicles, trips, expenses, incidents, and provides real-time monitoring through WebSocket connections.

---

## System Architecture

### High-Level Architecture
```
┌─────────────────────────────────────────────────────────────┐
│                    FRONTEND (JavaScript)                     │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐   │
│  │ app.js   │  │database.js│  │websocket │  │  Other   │   │
│  │          │  │           │  │-client.js│  │   JS     │   │
│  └──────────┘  └──────────┘  └──────────┘  └──────────┘   │
└─────────────────────────────────────────────────────────────┘
         │                    │                    │
         │ HTTP API           │ WebSocket          │
         │ (Port 8080)        │ (Port 8081)        │
         │                    │                    │
┌────────┴────────────────────┴────────────────────┴────────┐
│                    BACKEND (C++)                           │
│  ┌──────────────┐              ┌──────────────────┐       │
│  │ SDMServer    │              │ websocket_bridge │       │
│  │ (HTTP API)   │              │ (Real-time data) │       │
│  └──────┬───────┘              └────────┬─────────┘       │
│         │                                │                 │
│  ┌──────┴────────────────────────────────┴──────┐         │
│  │         DatabaseManager                        │         │
│  │         (Binary Database: SDM.db)             │         │
│  └──────────────────────────────────────────────┘         │
└─────────────────────────────────────────────────────────────┘
```

---

## Frontend Structure

### Core Files

#### 1. `index.html`
- **Purpose**: Main dashboard HTML with all page templates
- **Key Features**:
  - Single Page Application (SPA) structure
  - Contains templates for all pages (dashboard, trips, vehicles, expenses, etc.)
  - Loads all JavaScript modules
- **Dependencies**: All JS files loaded at bottom

#### 2. `app.js` - Main Application Controller
- **Purpose**: Core application logic and WebSocket communication
- **Key Responsibilities**:
  - Initializes the application
  - Manages WebSocket connection (`ws://localhost:8081`)
  - Handles real-time data updates (speed, acceleration, safety score)
  - Manages page navigation
  - Updates dashboard UI with live data
- **Connection Flow**:
  ```
  app.js → WebSocket (ws://localhost:8081) → websocket_bridge.cpp
  ```
- **Data Flow**:
  - Receives: `video_frame`, `live_data`, `lane_warning`, `trip_started`, `trip_stopped`
  - Sends: `start_trip`, `stop_trip`, `toggle_camera`, `ping`

#### 3. `database.js` - Database API Client
- **Purpose**: HTTP API client for database operations
- **Base URL**: `http://localhost:8080`
- **Key Methods**:
  - Authentication: `login()`, `register()`, `logout()`, `verifySession()`
  - Trips: `startTrip()`, `endTrip()`, `getTripHistory()`, `getTripStatistics()`
  - Vehicles: `getVehicles()`, `addVehicle()`, `updateVehicleOdometer()`
  - Expenses: `addExpense()`, `getExpenses()`, `getExpenseSummary()`
  - Drivers: `getDriverProfile()`, `updateDriverProfile()`, `getDriverBehavior()`
  - Incidents: `reportIncident()`, `getIncidents()`, `getIncidentStatistics()`
- **⚠️ ISSUE**: Many methods return mock data instead of calling the API
- **Integration Point**: Should connect to `SDMServer` on port 8080

#### 4. `main.js`
- **Purpose**: Entry point, checks authentication and initializes app
- **Flow**: Checks localStorage for session → Redirects to login if missing → Initializes `SmartDriveWebApp`

#### 5. `login.js`
- **Purpose**: Handles user authentication
- **Flow**: 
  - Submits to `http://localhost:8080/api/login`
  - Stores session_id and user_data in localStorage
  - Redirects to `index.html` on success

#### 6. `websocket-client.js`
- **Purpose**: WebSocket wrapper class (alternative to app.js WebSocket)
- **Connection**: `ws://localhost:8081`
- **Status**: Appears to be an alternative implementation, not actively used by app.js

#### 7. `server.js` (Node.js)
- **Purpose**: Development server for frontend
- **Port**: 3000 (default)
- **Features**:
  - Serves static files
  - Provides mock API endpoints for testing
  - Can start WebSocket bridge process
- **⚠️ NOTE**: This is a development server, not the production backend

#### 8. `analytics.js`
- **Purpose**: Charts and analytics visualization
- **Uses**: Chart.js library
- **Data Source**: Currently uses mock/sample data
- **⚠️ ISSUE**: Should fetch real data from database API

#### 9. `models.js`
- **Purpose**: Modal management system
- **Features**: Handles form submissions, modal display/hide
- **Integration**: Calls `window.db` methods when available

#### 10. `camera.js`
- **Purpose**: Camera feed handling (if separate from app.js)

---

## Backend Structure

### Core Components

#### 1. `SDMServer` (`source/server/SDMServer.h`)
- **Purpose**: Main HTTP server for API requests
- **Port**: 8080 (configurable)
- **Architecture**:
  - Multi-threaded server with worker threads
  - Request queue for handling concurrent requests
  - Uses `RequestHandler` to process requests
- **Initialization Flow**:
  ```
  1. DatabaseManager → Opens/creates SDM.db
  2. CacheManager → Initializes caching layer
  3. IndexManager → Loads/creates indexes
  4. SecurityManager → Handles authentication
  5. SessionManager → Manages user sessions
  6. Feature Managers → TripManager, VehicleManager, etc.
  7. RequestHandler → Processes HTTP requests
  ```
- **Request Processing**:
  ```
  Client → HTTP Request → RequestQueue → Worker Thread → RequestHandler → Manager → DatabaseManager → Response
  ```

#### 2. `DatabaseManager` (`source/core/DatabaseManager.h`)
- **Purpose**: Low-level database operations
- **Database Format**: Binary file (`SDM.db`)
- **Structure**:
  - Fixed-size records (DriverProfile: 1024 bytes, VehicleInfo: 1024 bytes, etc.)
  - Sequential storage with offsets
  - No SQL, direct file I/O
- **Key Operations**:
  - `create_driver()`, `read_driver()`, `update_driver()`, `delete_driver()`
  - `create_vehicle()`, `read_vehicle()`, `update_vehicle()`, `delete_vehicle()`
  - `create_trip()`, `read_trip()`, `update_trip()`
  - `create_expense()`, `get_expenses_by_driver()`
  - `create_maintenance()`, `get_maintenance_by_vehicle()`
- **File Structure**:
  ```
  [SDMHeader: 4096 bytes]
  [Driver Table: max_drivers × 1024 bytes]
  [Vehicle Table: max_vehicles × 1024 bytes]
  [Trip Table: max_trips × 1024 bytes]
  [Maintenance Table: 100000 × 1024 bytes]
  [Expense Table: 500000 × 1024 bytes]
  [Document Table: 100000 × 1024 bytes]
  [Incident Table: 50000 × 2048 bytes]
  ```

#### 3. `TripManager` (`source/core/TripManager.h`)
- **Purpose**: High-level trip management
- **Features**:
  - Trip lifecycle: `start_trip()`, `log_gps_point()`, `end_trip()`
  - Driving event detection (harsh braking, rapid acceleration, speeding)
  - Safety score calculation
  - Trip statistics and analytics
- **Dependencies**: DatabaseManager, CacheManager, IndexManager

#### 4. `websocket_bridge.cpp` (`source/modules/websocket_bridge.cpp`)
- **Purpose**: Real-time data bridge via WebSocket
- **Port**: 8081
- **Features**:
  - Camera feed streaming (base64 encoded JPEG frames)
  - Lane detection processing
  - GPS/location tracking
  - Live data broadcasting (speed, acceleration, safety score)
  - Trip start/stop commands
- **Data Flow**:
  ```
  Camera → OpenCV → Lane Detector → Base64 Encode → WebSocket → Frontend
  GPS → LocationManager → Update Live Data → WebSocket → Frontend
  ```
- **Commands Handled**:
  - `start_trip`: Creates trip in database (⚠️ partially implemented)
  - `stop_trip`: Ends trip in database (⚠️ partially implemented)
  - `toggle_camera`: Starts/stops camera feed
  - `get_stats`: Returns current live data
  - `get_trip_history`: Queries database (⚠️ not fully implemented)
  - `get_vehicles`: Queries database (⚠️ not fully implemented)

#### 5. Other Managers
- **VehicleManager**: Vehicle CRUD operations
- **ExpenseManager**: Expense tracking
- **DriverManager**: Driver profile management
- **IncidentManager**: Incident reporting
- **SessionManager**: User session management
- **SecurityManager**: Authentication and authorization
- **CacheManager**: Caching layer for performance
- **IndexManager**: Index management for fast lookups

---

## Data Structures

### Database Records (from `sdm_types.hpp`)

1. **DriverProfile** (1024 bytes)
   - driver_id, username, password_hash, role
   - full_name, email, phone, license_number
   - Statistics: total_trips, total_distance, safety_score
   - Timestamps: created_time, last_login

2. **VehicleInfo** (1024 bytes)
   - vehicle_id, owner_driver_id
   - license_plate, make, model, year, type
   - Technical: engine_capacity, fuel_tank_capacity
   - Odometer: current_odometer, last_service_odometer
   - Insurance and registration info

3. **TripRecord** (1024 bytes)
   - trip_id, driver_id, vehicle_id
   - start_time, end_time, duration
   - Location: start/end lat/lon, addresses
   - Metrics: distance, avg_speed, max_speed, fuel_consumed
   - Events: harsh_braking_count, rapid_acceleration_count, etc.

4. **ExpenseRecord** (1024 bytes)
   - expense_id, driver_id, vehicle_id, trip_id
   - category, amount, currency, description
   - Fuel-specific: fuel_quantity, fuel_price_per_unit, station

5. **MaintenanceRecord** (1024 bytes)
   - maintenance_id, vehicle_id, driver_id
   - type, service_date, odometer_reading
   - service_center, technician, description
   - Costs: labor_cost, parts_cost, total_cost

6. **IncidentReport** (2048 bytes)
   - incident_id, driver_id, vehicle_id, trip_id
   - type, incident_time, location (lat/lon)
   - description, police_report_number
   - Damage estimates and insurance info

---

## Communication Flow

### HTTP API Flow (Port 8080)
```
Frontend (database.js)
    ↓ POST /api/login
SDMServer (RequestHandler)
    ↓
SessionManager (authenticate)
    ↓
DatabaseManager (read_driver)
    ↓ Response: {session_id, driver_id, ...}
Frontend (stores in localStorage)
```

### WebSocket Flow (Port 8081)
```
Frontend (app.js)
    ↓ WebSocket Connect
websocket_bridge
    ↓
Camera Thread → Frame Processing → Base64 → WebSocket Message
    ↓
Frontend receives video_frame
```

### Trip Start Flow
```
Frontend: startTrip() in app.js
    ↓ WebSocket: {command: "start_trip", driver_id, vehicle_id}
websocket_bridge: handle_start_trip()
    ↓ (⚠️ Currently only sets live_data, doesn't fully save to DB)
DatabaseManager: create_trip() (needs to be called)
    ↓
TripManager: start_trip() (should be used)
    ↓
Response: {type: "trip_started", data: {trip_id, ...}}
```

---

## API Protocol Mismatch ⚠️ CRITICAL

### Backend API Format (RequestHandler)
The backend `RequestHandler` expects a **single endpoint** with JSON body containing an `operation` field:

```json
POST http://localhost:8080/
Content-Type: application/json

{
  "operation": "trip_start",
  "session_id": "abc123",
  "vehicle_id": "1",
  "latitude": "31.5204",
  "longitude": "74.3587"
}
```

**Supported Operations:**
- Authentication: `user_login`, `user_register`, `user_logout`
- Trips: `trip_start`, `trip_end`, `trip_get_history`, `trip_get_statistics`, `trip_log_gps`
- Vehicles: `vehicle_add`, `vehicle_get_list`, `vehicle_update_odometer`, `vehicle_add_maintenance`, `vehicle_get_alerts`
- Expenses: `expense_add`, `expense_add_fuel`, `expense_get_summary`, `expense_set_budget`, `expense_get_budget_alerts`
- Drivers: `driver_get_profile`, `driver_update_profile`, `driver_get_behavior`, `driver_get_leaderboard`, `driver_get_recommendations`
- Incidents: `incident_report`, `incident_get_list`, `incident_get_statistics`

### Frontend API Format (database.js)
The frontend `database.js` uses **REST-style endpoints**:

```javascript
POST http://localhost:8080/api/login
POST http://localhost:8080/api/trip/start
GET  http://localhost:8080/api/trips
```

**This is a fundamental mismatch!** The frontend is calling endpoints that don't exist in the backend.

### Solution Required
1. **Option A**: Modify `database.js` to use the operation-based API format
2. **Option B**: Modify `RequestHandler` to support REST endpoints (requires HTTP routing)
3. **Option C**: Add a REST-to-operation adapter layer

---

## Integration Issues & Gaps

### 🔴 Critical Issues

1. **API Protocol Mismatch** ⚠️ **MOST CRITICAL**
   - Frontend uses REST endpoints (`/api/login`, `/api/trip/start`)
   - Backend expects operation-based API (single endpoint with `operation` field)
   - **Result**: Frontend API calls will fail or return errors
   - **Fix Required**: Either change frontend to match backend, or add REST routing to backend

2. **Frontend Not Using Database API**
   - `database.js` has API methods but many return mock data
   - Example: `getMaintenanceHistory()` returns hardcoded array
   - Example: `getBudgetAlerts()` returns mock data
   - **Fix Required**: All methods should call `sendRequest()` to backend

2. **WebSocket Bridge Not Fully Integrated with Database**
   - `handle_start_trip()` in websocket_bridge.cpp has comment: "Implement database save here"
   - `handle_stop_trip()` has comment: "Implement database save here"
   - `handle_get_trip_history()` returns empty array
   - `handle_get_vehicles()` returns empty array
   - **Fix Required**: Call TripManager and VehicleManager methods

3. **Analytics Using Mock Data**
   - `analytics.js` uses hardcoded sample data
   - Charts don't fetch real data from database
   - **Fix Required**: Fetch data via `database.js` API calls

4. **Frontend Pages Not Loading Real Data**
   - Trips page: `loadTrips()` likely uses mock data
   - Vehicles page: `loadVehicles()` likely uses mock data
   - Expenses page: `loadExpenses()` likely uses mock data
   - **Fix Required**: Call `window.db` methods when pages load

### 🟡 Medium Issues

5. **RequestHandler Not Documented**
   - Need to see how HTTP requests are routed to managers
   - Need to verify all API endpoints are implemented

6. **Session Management**
   - Frontend stores session_id in localStorage
   - Backend SessionManager needs to validate on each request
   - Need to verify session expiration handling

7. **Error Handling**
   - Frontend needs better error handling for API failures
   - Backend needs proper error responses

### 🟢 Minor Issues

8. **Mock Server (server.js)**
   - Development server provides mock endpoints
   - Should be removed or clearly marked as dev-only

9. **Code Duplication**
   - WebSocket handling in both `app.js` and `websocket-client.js`
   - Should consolidate

---

## File Dependencies Map

### Frontend Dependencies
```
index.html
  ├── app.js (main controller)
  │     ├── database.js (API client)
  │     ├── websocket-client.js (alternative WS)
  │     └── models.js (modals)
  ├── main.js (entry point)
  ├── login.js (authentication)
  ├── analytics.js (charts)
  └── camera.js (camera handling)
```

### Backend Dependencies
```
main.cpp (CLI entry)
  └── SDMServer
        ├── DatabaseManager
        ├── CacheManager
        ├── IndexManager
        ├── SecurityManager
        ├── SessionManager
        ├── TripManager → DatabaseManager
        ├── VehicleManager → DatabaseManager
        ├── ExpenseManager → DatabaseManager
        ├── DriverManager → DatabaseManager
        ├── IncidentManager → DatabaseManager
        └── RequestHandler → All Managers

websocket_bridge.cpp
  ├── DatabaseManager (⚠️ needs better integration)
  ├── CameraManager
  ├── LaneDetector
  └── LocationManager
```

---

## Data Flow Examples

### Example 1: User Login
```
1. User enters credentials in login.html
2. login.js → POST /api/login → SDMServer
3. RequestHandler → SessionManager.login()
4. SessionManager → SecurityManager.verify_password()
5. SessionManager → DatabaseManager.read_driver()
6. SessionManager → Creates session, returns session_id
7. Frontend stores session_id in localStorage
8. Redirect to index.html
```

### Example 2: Start Trip (Current - Incomplete)
```
1. User clicks "Start Trip" in dashboard
2. app.js → WebSocket: {command: "start_trip", driver_id, vehicle_id}
3. websocket_bridge → handle_start_trip()
4. Sets live_data.trip_active = true
5. ⚠️ Should call: TripManager.start_trip() → DatabaseManager.create_trip()
6. Returns: {type: "trip_started", data: {trip_id}}
7. Frontend updates UI
```

### Example 2: Start Trip (Should Be)
```
1. User clicks "Start Trip"
2. Option A: app.js → WebSocket → websocket_bridge → TripManager.start_trip()
3. Option B: database.js → POST /api/trip/start → SDMServer → RequestHandler → TripManager
4. TripManager.start_trip() → DatabaseManager.create_trip()
5. TripManager → IndexManager.insert_primary()
6. Response with trip_id
7. Frontend updates UI
```

### Example 3: View Trip History (Current - Mock Data)
```
1. User navigates to Trips page
2. Frontend calls loadTrips() (likely uses mock data)
3. ⚠️ Should call: database.js.getTripHistory()
4. ⚠️ Should: POST /api/trips → SDMServer → RequestHandler → TripManager
5. TripManager.get_driver_trips() → DatabaseManager.get_trips_by_driver()
6. Returns real trip data
7. Frontend displays in table
```

---

## Configuration

### Backend Config (`sdm_config.hpp`)
- Database path: `compiled/SDM.db`
- Index path: `compiled/indexes`
- Server port: 8080
- Max connections: 1000
- Worker threads: 16
- Cache size: 256 MB
- Session timeout: 1800 seconds (30 minutes)

### Frontend Config
- API Base: `http://localhost:8080` (database.js)
- WebSocket URL: `ws://localhost:8081` (app.js)
- Development Server: `http://localhost:3000` (server.js)

---

## Next Steps for Integration

1. **Complete WebSocket Bridge Database Integration**
   - Implement `handle_start_trip()` to call `TripManager.start_trip()`
   - Implement `handle_stop_trip()` to call `TripManager.end_trip()`
   - Implement `handle_get_trip_history()` to query database
   - Implement `handle_get_vehicles()` to query database

2. **Fix Frontend Database API Calls**
   - Remove all mock data returns in `database.js`
   - Ensure all methods call `sendRequest()` to backend
   - Add proper error handling

3. **Implement Page Data Loading**
   - Trips page: Call `db.getTripHistory()` on page load
   - Vehicles page: Call `db.getVehicles()` on page load
   - Expenses page: Call `db.getExpenses()` on page load
   - Drivers page: Call `db.getDriverProfile()` on page load
   - Incidents page: Call `db.getIncidents()` on page load

4. **Fix Analytics Data Source**
   - `analytics.js` should fetch real data from database
   - Use `db.getTripStatistics()` for charts
   - Update charts when data changes

5. **Verify RequestHandler Endpoints**
   - Ensure all API endpoints in `database.js` are implemented in `RequestHandler`
   - Test each endpoint with real database

6. **Add Real-time Data Sync**
   - When trip ends, refresh trips list
   - When expense added, refresh expenses list
   - Use WebSocket for real-time updates where applicable

---

## Testing Checklist

- [ ] Login creates valid session
- [ ] Session validation works on API calls
- [ ] Start trip saves to database
- [ ] Stop trip updates database
- [ ] Trip history loads from database
- [ ] Vehicle list loads from database
- [ ] Add vehicle saves to database
- [ ] Expense list loads from database
- [ ] Add expense saves to database
- [ ] Driver profile loads from database
- [ ] Update driver profile saves to database
- [ ] Incident list loads from database
- [ ] Report incident saves to database
- [ ] Analytics charts show real data
- [ ] WebSocket real-time data works
- [ ] Camera feed works
- [ ] Lane detection warnings work

---

## Summary

The Smart Drive Manager has a solid architecture with:
- ✅ Well-structured C++ backend with binary database
- ✅ Modern JavaScript frontend with SPA design
- ✅ WebSocket for real-time data
- ✅ Separation of concerns (managers, handlers, etc.)

However, there are integration gaps:
- ❌ Frontend uses mock data instead of database
- ❌ WebSocket bridge doesn't fully integrate with database
- ❌ Analytics uses sample data
- ❌ Many API endpoints need to be connected

The main task is to connect the frontend to the backend database through proper API calls and ensure all data operations persist to the database.
