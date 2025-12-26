// database.js - Database API Integration
class DatabaseAPI {
    constructor(app) {
        this.app = app;
        this.API_BASE = 'http://localhost:8080';
        this.endpoints = {
            // Authentication
            login: '/api/login',
            register: '/api/register',
            logout: '/api/logout',
            verify: '/api/verify',
            
            // Trips
            trip_start: '/api/trip/start',
            trip_end: '/api/trip/end',
            trip_history: '/api/trips',
            trip_stats: '/api/trip/stats',
            
            // Vehicles
            vehicle_list: '/api/vehicles',
            vehicle_add: '/api/vehicle/add',
            vehicle_update: '/api/vehicle/update',
            maintenance_add: '/api/maintenance/add',
            
            // Expenses
            expense_add: '/api/expense/add',
            expense_list: '/api/expenses',
            expense_summary: '/api/expense/summary',
            budget_set: '/api/budget/set',
            
            // Drivers
            driver_profile: '/api/driver/profile',
            driver_update: '/api/driver/update',
            driver_behavior: '/api/driver/behavior',
            driver_leaderboard: '/api/driver/leaderboard',
            
            // Incidents
            incident_report: '/api/incident/report',
            incident_list: '/api/incidents',
            incident_stats: '/api/incident/stats',
            
            // System
            stats: '/api/stats',
            backup: '/api/backup',
            settings: '/api/settings'
        };
    }

    async sendRequest(endpoint, data = {}, method = 'POST') {
        try {
            // Add session ID if available
            if (this.app.sessionId && !endpoint.includes('login') && !endpoint.includes('register')) {
                data.session_id = this.app.sessionId;
            }
            
            const response = await fetch(`${this.API_BASE}${endpoint}`, {
                method: method,
                headers: {
                    'Content-Type': 'application/json',
                    'Authorization': `Bearer ${this.app.sessionId}`
                },
                body: method !== 'GET' ? JSON.stringify(data) : undefined
            });
            
            if (!response.ok) {
                throw new Error(`HTTP ${response.status}: ${response.statusText}`);
            }
            
            const result = await response.json();
            
            if (result.status === 'error') {
                throw new Error(result.message || 'Unknown error occurred');
            }
            
            return result;
        } catch (error) {
            console.error('API Request failed:', error);
            
            // Handle session expiration
            if (error.message.includes('UNAUTHORIZED') || error.message.includes('Invalid session')) {
                this.app.showToast('Session expired. Please login again.', 'error');
                setTimeout(() => this.app.logout(), 2000);
            }
            
            throw error;
        }
    }

    // Authentication
    async login(username, password) {
        const response = await this.sendRequest(this.endpoints.login, {
            username: username,
            password: password
        });
        
        if (response.status === 'success') {
            this.app.sessionId = response.data.session_id;
            this.app.userData = {
                driver_id: response.data.driver_id,
                name: response.data.name,
                role: response.data.role,
                username: username
            };
            
            localStorage.setItem('session_id', this.app.sessionId);
            localStorage.setItem('user_data', JSON.stringify(this.app.userData));
            
            this.app.showToast('Login successful!', 'success');
            
            return response.data;
        }
        
        return null;
    }

    async register(username, password, fullName, email, phone) {
        const response = await this.sendRequest(this.endpoints.register, {
            username: username,
            password: password,
            full_name: fullName,
            email: email,
            phone: phone
        });
        
        return response.status === 'success';
    }

    async logout() {
        try {
            await this.sendRequest(this.endpoints.logout, {
                session_id: this.app.sessionId
            });
        } catch (error) {
            // Ignore errors on logout
        }
        
        this.app.sessionId = null;
        this.app.userData = null;
        localStorage.removeItem('session_id');
        localStorage.removeItem('user_data');
    }

    async verifySession() {
        if (!this.app.sessionId) return false;
        
        try {
            const response = await this.sendRequest(this.endpoints.verify, {
                session_id: this.app.sessionId
            });
            
            return response.status === 'success' && response.data.valid === true;
        } catch (error) {
            return false;
        }
    }

    // Trip Operations
    async startTrip(vehicleId, startLat, startLon, address = '') {
        const response = await this.sendRequest(this.endpoints.trip_start, {
            vehicle_id: vehicleId,
            latitude: startLat,
            longitude: startLon,
            address: address
        });
        
        if (response.status === 'success') {
            return {
                trip_id: response.data.trip_id,
                start_time: response.data.start_time
            };
        }
        
        return null;
    }

    async endTrip(tripId, endLat, endLon, address = '') {
        const response = await this.sendRequest(this.endpoints.trip_end, {
            trip_id: tripId,
            latitude: endLat,
            longitude: endLon,
            address: address
        });
        
        return response.status === 'success';
    }

    async getTripHistory(limit = 20, offset = 0, vehicleId = null, startDate = null, endDate = null) {
        const params = new URLSearchParams({
            limit: limit,
            offset: offset
        });
        
        if (vehicleId) params.append('vehicle_id', vehicleId);
        if (startDate) params.append('start_date', startDate);
        if (endDate) params.append('end_date', endDate);
        
        const response = await this.sendRequest(`${this.endpoints.trip_history}?${params}`, {}, 'GET');
        
        if (response.status === 'success') {
            return response.data.trips || [];
        }
        
        return [];
    }

    async getTripStatistics(driverId = null) {
        const params = driverId ? `?driver_id=${driverId}` : '';
        const response = await this.sendRequest(`${this.endpoints.trip_stats}${params}`, {}, 'GET');
        
        if (response.status === 'success') {
            return response.data;
        }
        
        return null;
    }

    // Vehicle Operations
    async getVehicles() {
        const response = await this.sendRequest(this.endpoints.vehicle_list, {}, 'GET');
        
        if (response.status === 'success') {
            return response.data.vehicles || [];
        }
        
        return [];
    }

    async addVehicle(licensePlate, make, model, year, type, vin = '') {
        const response = await this.sendRequest(this.endpoints.vehicle_add, {
            license_plate: licensePlate,
            make: make,
            model: model,
            year: year,
            type: type,
            vin: vin
        });
        
        if (response.status === 'success') {
            return response.data.vehicle_id;
        }
        
        return null;
    }

    async updateVehicleOdometer(vehicleId, odometer) {
        const response = await this.sendRequest(this.endpoints.vehicle_update, {
            vehicle_id: vehicleId,
            odometer: odometer
        });
        
        return response.status === 'success';
    }

    async addMaintenanceRecord(vehicleId, type, odometer, serviceCenter, description, cost) {
        const response = await this.sendRequest(this.endpoints.maintenance_add, {
            vehicle_id: vehicleId,
            type: type,
            odometer: odometer,
            service_center: serviceCenter,
            description: description,
            cost: cost
        });
        
        if (response.status === 'success') {
            return response.data.maintenance_id;
        }
        
        return null;
    }

    async getMaintenanceHistory(vehicleId) {
        // This would typically be a separate endpoint
        // For now, return mock data
        return [
            {
                id: 1,
                date: '2024-01-15',
                type: 'Oil Change',
                odometer: 45000,
                cost: 120,
                center: 'AutoCare Center'
            },
            {
                id: 2,
                date: '2023-12-10',
                type: 'Brake Service',
                odometer: 43000,
                cost: 350,
                center: 'Brake Masters'
            }
        ];
    }

    // Expense Operations
    async addExpense(vehicleId, category, amount, description, tripId = null) {
        const response = await this.sendRequest(this.endpoints.expense_add, {
            vehicle_id: vehicleId,
            category: category,
            amount: amount,
            description: description,
            trip_id: tripId
        });
        
        if (response.status === 'success') {
            return response.data.expense_id;
        }
        
        return null;
    }

    async addFuelExpense(vehicleId, tripId, quantity, pricePerUnit, station) {
        const response = await this.sendRequest(this.endpoints.expense_add, {
            vehicle_id: vehicleId,
            trip_id: tripId,
            category: 0, // Fuel
            amount: quantity * pricePerUnit,
            description: `Fuel at ${station}`,
            fuel_quantity: quantity,
            fuel_price: pricePerUnit,
            station: station
        });
        
        if (response.status === 'success') {
            return response.data.expense_id;
        }
        
        return null;
    }

    async getExpenses(period = 'month', category = null, vehicleId = null) {
        const params = new URLSearchParams({
            period: period
        });
        
        if (category !== null) params.append('category', category);
        if (vehicleId) params.append('vehicle_id', vehicleId);
        
        const response = await this.sendRequest(`${this.endpoints.expense_list}?${params}`, {}, 'GET');
        
        if (response.status === 'success') {
            return response.data.expenses || [];
        }
        
        return [];
    }

    async getExpenseSummary(startDate, endDate) {
        const params = new URLSearchParams({
            start_date: startDate,
            end_date: endDate
        });
        
        const response = await this.sendRequest(`${this.endpoints.expense_summary}?${params}`, {}, 'GET');
        
        if (response.status === 'success') {
            return response.data;
        }
        
        return null;
    }

    async setBudgetLimit(category, monthlyLimit) {
        const response = await this.sendRequest(this.endpoints.budget_set, {
            category: category,
            monthly_limit: monthlyLimit
        });
        
        return response.status === 'success';
    }

    async getBudgetAlerts() {
        // This would typically be a separate endpoint
        // For now, return mock data
        return [
            {
                category: 'Fuel',
                limit: 500,
                spent: 450,
                percentage: 90,
                over_budget: false
            },
            {
                category: 'Maintenance',
                limit: 300,
                spent: 320,
                percentage: 107,
                over_budget: true
            }
        ];
    }

    // Driver Operations
    async getDriverProfile() {
        const response = await this.sendRequest(this.endpoints.driver_profile, {}, 'GET');
        
        if (response.status === 'success') {
            return response.data;
        }
        
        return null;
    }

    async updateDriverProfile(fullName, email, phone, licenseNumber = null) {
        const data = {
            full_name: fullName,
            email: email,
            phone: phone
        };
        
        if (licenseNumber) data.license_number = licenseNumber;
        
        const response = await this.sendRequest(this.endpoints.driver_update, data);
        
        return response.status === 'success';
    }

    async getDriverBehavior() {
        const response = await this.sendRequest(this.endpoints.driver_behavior, {}, 'GET');
        
        if (response.status === 'success') {
            return response.data;
        }
        
        return null;
    }

    async getDriverLeaderboard(limit = 10) {
        const params = new URLSearchParams({
            limit: limit
        });
        
        const response = await this.sendRequest(`${this.endpoints.driver_leaderboard}?${params}`, {}, 'GET');
        
        if (response.status === 'success') {
            return response.data.leaderboard || [];
        }
        
        return [];
    }

    async getImprovementRecommendations() {
        // This would typically be a separate endpoint
        // For now, return mock data
        return [
            {
                category: 'Braking',
                recommendation: 'Reduce harsh braking by anticipating stops earlier',
                priority: 3,
                improvement: 15
            },
            {
                category: 'Speed',
                recommendation: 'Maintain consistent speed within limits',
                priority: 2,
                improvement: 10
            }
        ];
    }

    // Incident Operations
    async reportIncident(vehicleId, type, latitude, longitude, description) {
        const response = await this.sendRequest(this.endpoints.incident_report, {
            vehicle_id: vehicleId,
            type: type,
            latitude: latitude,
            longitude: longitude,
            description: description
        });
        
        if (response.status === 'success') {
            return response.data.incident_id;
        }
        
        return null;
    }

    async getIncidents(limit = 20, resolved = null) {
        const params = new URLSearchParams({
            limit: limit
        });
        
        if (resolved !== null) params.append('resolved', resolved);
        
        const response = await this.sendRequest(`${this.endpoints.incident_list}?${params}`, {}, 'GET');
        
        if (response.status === 'success') {
            return response.data.incidents || [];
        }
        
        return [];
    }

    async getIncidentStatistics() {
        const response = await this.sendRequest(this.endpoints.incident_stats, {}, 'GET');
        
        if (response.status === 'success') {
            return response.data;
        }
        
        return null;
    }

    async markIncidentResolved(incidentId) {
        // This would typically be a separate endpoint
        // For now, simulate success
        return true;
    }

    // System Operations
    async getSystemStats() {
        const response = await this.sendRequest(this.endpoints.stats, {}, 'GET');
        
        if (response.status === 'success') {
            return response.data;
        }
        
        return null;
    }

    async backupDatabase() {
        const response = await this.sendRequest(this.endpoints.backup, {});
        
        if (response.status === 'success') {
            return response.data.backup_path;
        }
        
        return null;
    }

    async updateSettings(settings) {
        const response = await this.sendRequest(this.endpoints.settings, settings);
        
        return response.status === 'success';
    }

    // Helper Methods
    async getVehicleOptions() {
        const vehicles = await this.getVehicles();
        return vehicles.map(v => ({
            value: v.vehicle_id,
            text: `${v.make} ${v.model} (${v.license_plate})`
        }));
    }

    async getCategoryOptions() {
        return [
            { value: 0, text: 'Fuel' },
            { value: 1, text: 'Maintenance' },
            { value: 2, text: 'Insurance' },
            { value: 3, text: 'Toll' },
            { value: 4, text: 'Parking' },
            { value: 5, text: 'Other' }
        ];
    }

    async getIncidentTypeOptions() {
        return [
            { value: 0, text: 'Accident' },
            { value: 1, text: 'Breakdown' },
            { value: 2, text: 'Theft' },
            { value: 3, text: 'Vandalism' },
            { value: 4, text: 'Traffic Violation' }
        ];
    }

    async getMaintenanceTypeOptions() {
        return [
            { value: 0, text: 'Oil Change' },
            { value: 1, text: 'Tire Rotation' },
            { value: 2, text: 'Brake Service' },
            { value: 3, text: 'Engine Check' },
            { value: 4, text: 'Transmission Service' },
            { value: 5, text: 'General Service' }
        ];
    }

    // Mock data for development
    async getMockData(dataType) {
        const mockData = {
            trips: [
                {
                    trip_id: 1,
                    start_time: '2024-01-20 08:30:00',
                    end_time: '2024-01-20 09:15:00',
                    vehicle_id: 1,
                    distance: 32.5,
                    duration: 2700,
                    avg_speed: 65.2,
                    safety_score: 920,
                    start_location: 'Home',
                    end_location: 'Office'
                },
                {
                    trip_id: 2,
                    start_time: '2024-01-19 17:15:00',
                    end_time: '2024-01-19 18:00:00',
                    vehicle_id: 1,
                    distance: 28.7,
                    duration: 2700,
                    avg_speed: 58.4,
                    safety_score: 890,
                    start_location: 'Office',
                    end_location: 'Home'
                }
            ],
            
            vehicles: [
                {
                    vehicle_id: 1,
                    make: 'Toyota',
                    model: 'Corolla',
                    year: 2020,
                    license_plate: 'ABC-123',
                    current_odometer: 45230,
                    fuel_type: 'Petrol',
                    vin: 'JTDBU4EE7A9012345'
                },
                {
                    vehicle_id: 2,
                    make: 'Honda',
                    model: 'Civic',
                    year: 2019,
                    license_plate: 'XYZ-789',
                    current_odometer: 52450,
                    fuel_type: 'Petrol',
                    vin: '2HGFC2F59LH123456'
                }
            ],
            
            expenses: [
                {
                    expense_id: 1,
                    date: '2024-01-20',
                    category: 'Fuel',
                    vehicle_id: 1,
                    amount: 65.50,
                    description: 'Fuel at Shell Station',
                    trip_id: 1
                },
                {
                    expense_id: 2,
                    date: '2024-01-15',
                    category: 'Maintenance',
                    vehicle_id: 1,
                    amount: 120.00,
                    description: 'Oil Change',
                    trip_id: null
                }
            ],
            
            incidents: [
                {
                    incident_id: 1,
                    date: '2024-01-10',
                    type: 'Breakdown',
                    vehicle_id: 1,
                    location: '31.5204, 74.3587',
                    description: 'Flat tire on highway',
                    resolved: true
                },
                {
                    incident_id: 2,
                    date: '2024-01-05',
                    type: 'Traffic Violation',
                    vehicle_id: 2,
                    location: '31.5497, 74.3436',
                    description: 'Speeding ticket',
                    resolved: false
                }
            ]
        };
        
        return mockData[dataType] || [];
    }
}

// Initialize database API
if (window.app) {
    window.db = new DatabaseAPI(window.app);
}