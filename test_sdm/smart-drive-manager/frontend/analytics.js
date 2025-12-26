// analytics.js - Analytics and Charts Module
class AnalyticsManager {
    constructor(app) {
        this.app = app;
        this.charts = {};
        this.performanceData = {};
        
        // Initialize when DOM is ready
        if (document.readyState === 'loading') {
            document.addEventListener('DOMContentLoaded', () => this.init());
        } else {
            this.init();
        }
    }

    init() {
        this.setupEventListeners();
        this.loadSampleData();
    }

    setupEventListeners() {
        // Performance chart period change
        const performancePeriod = document.getElementById('performancePeriod');
        if (performancePeriod) {
            performancePeriod.addEventListener('change', () => this.updatePerformanceChart());
        }

        // Date range filters
        const startDate = document.getElementById('reportStartDate');
        const endDate = document.getElementById('reportEndDate');
        
        if (startDate && endDate) {
            // Set default dates (last 30 days)
            const end = new Date();
            const start = new Date();
            start.setDate(start.getDate() - 30);
            
            startDate.valueAsDate = start;
            endDate.valueAsDate = end;
            
            startDate.addEventListener('change', () => this.updateCharts());
            endDate.addEventListener('change', () => this.updateCharts());
        }
    }

    loadSampleData() {
        // Sample performance data for charts
        this.performanceData = {
            safetyScores: [920, 915, 910, 905, 900, 895, 890, 885, 880, 875, 870, 865, 860, 855, 850, 845, 840, 835, 830, 825],
            distances: [32.5, 28.7, 45.2, 38.9, 41.3, 36.8, 39.4, 42.7, 35.6, 40.1, 37.8, 43.2, 39.7, 41.8, 36.3, 44.1, 38.5, 42.3, 39.8, 37.2],
            fuelEfficiency: [12.5, 12.3, 12.1, 11.9, 12.0, 12.2, 12.4, 12.1, 11.8, 12.0, 12.3, 12.5, 12.2, 12.0, 11.9, 12.1, 12.3, 12.4, 12.2, 12.0],
            harshEvents: [2, 3, 1, 4, 2, 3, 1, 2, 4, 3, 2, 1, 3, 2, 4, 1, 2, 3, 1, 2]
        };

        // Initialize charts when on reports page
        if (this.app.currentPage === 'reports') {
            this.initCharts();
        }
    }

    initCharts() {
        this.initPerformanceChart();
        this.initSafetyChart();
        this.initFuelChart();
        this.initRadarChart();
        this.updateMetrics();
    }

    initPerformanceChart() {
        const canvas = document.getElementById('performanceChart');
        if (!canvas) return;

        const ctx = canvas.getContext('2d');
        
        // Generate labels for last 20 days
        const labels = [];
        for (let i = 19; i >= 0; i--) {
            const date = new Date();
            date.setDate(date.getDate() - i);
            labels.push(date.getDate().toString().padStart(2, '0'));
        }

        this.charts.performance = new Chart(ctx, {
            type: 'line',
            data: {
                labels: labels,
                datasets: [
                    {
                        label: 'Safety Score',
                        data: this.performanceData.safetyScores,
                        borderColor: '#4361ee',
                        backgroundColor: 'rgba(67, 97, 238, 0.1)',
                        fill: true,
                        tension: 0.4,
                        yAxisID: 'y'
                    },
                    {
                        label: 'Distance (km)',
                        data: this.performanceData.distances,
                        borderColor: '#4cc9f0',
                        backgroundColor: 'rgba(76, 201, 240, 0.1)',
                        fill: true,
                        tension: 0.4,
                        yAxisID: 'y1'
                    },
                    {
                        label: 'Harsh Events',
                        data: this.performanceData.harshEvents,
                        borderColor: '#f72585',
                        backgroundColor: 'rgba(247, 37, 133, 0.1)',
                        fill: true,
                        tension: 0.4,
                        yAxisID: 'y2'
                    }
                ]
            },
            options: {
                responsive: true,
                interaction: {
                    mode: 'index',
                    intersect: false
                },
                scales: {
                    x: {
                        title: {
                            display: true,
                            text: 'Days'
                        }
                    },
                    y: {
                        type: 'linear',
                        display: true,
                        position: 'left',
                        title: {
                            display: true,
                            text: 'Safety Score'
                        },
                        min: 800,
                        max: 1000
                    },
                    y1: {
                        type: 'linear',
                        display: true,
                        position: 'right',
                        title: {
                            display: true,
                            text: 'Distance (km)'
                        },
                        grid: {
                            drawOnChartArea: false
                        }
                    },
                    y2: {
                        type: 'linear',
                        display: false,
                        position: 'right',
                        grid: {
                            drawOnChartArea: false
                        }
                    }
                },
                plugins: {
                    legend: {
                        position: 'top'
                    },
                    tooltip: {
                        mode: 'index',
                        intersect: false
                    }
                }
            }
        });
    }

    initSafetyChart() {
        const canvas = document.getElementById('safetyChart');
        if (!canvas) return;

        const ctx = canvas.getContext('2d');
        
        // Generate safety trend data
        const safetyData = [];
        for (let i = 0; i < 12; i++) {
            safetyData.push(850 + Math.random() * 100);
        }

        this.charts.safety = new Chart(ctx, {
            type: 'line',
            data: {
                labels: ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'],
                datasets: [{
                    label: 'Safety Score',
                    data: safetyData,
                    borderColor: '#4cc9f0',
                    backgroundColor: 'rgba(76, 201, 240, 0.2)',
                    fill: true,
                    tension: 0.4
                }]
            },
            options: {
                responsive: true,
                scales: {
                    y: {
                        beginAtZero: false,
                        min: 800,
                        max: 1000,
                        title: {
                            display: true,
                            text: 'Safety Score'
                        }
                    }
                },
                plugins: {
                    legend: {
                        display: false
                    }
                }
            }
        });

        // Update current safety score display
        const currentScore = safetyData[safetyData.length - 1];
        document.getElementById('currentSafetyScore').textContent = Math.round(currentScore);
        
        // Calculate trend
        const trend = ((currentScore - safetyData[0]) / safetyData[0] * 100).toFixed(1);
        const trendElem = document.querySelector('.performance .value');
        if (trendElem) {
            trendElem.textContent = `${trend > 0 ? '+' : ''}${trend}%`;
            trendElem.className = `value ${trend > 0 ? 'positive' : 'negative'}`;
        }
    }

    initFuelChart() {
        const canvas = document.getElementById('fuelChart');
        if (!canvas) return;

        const ctx = canvas.getContext('2d');
        
        // Generate fuel efficiency data
        const fuelData = [];
        for (let i = 0; i < 8; i++) {
            fuelData.push(10 + Math.random() * 4);
        }

        this.charts.fuel = new Chart(ctx, {
            type: 'bar',
            data: {
                labels: ['Week 1', 'Week 2', 'Week 3', 'Week 4', 'Week 5', 'Week 6', 'Week 7', 'Week 8'],
                datasets: [{
                    label: 'Fuel Efficiency (km/L)',
                    data: fuelData,
                    backgroundColor: [
                        'rgba(76, 201, 240, 0.7)',
                        'rgba(67, 97, 238, 0.7)',
                        'rgba(58, 12, 163, 0.7)',
                        'rgba(114, 9, 183, 0.7)',
                        'rgba(156, 136, 255, 0.7)',
                        'rgba(248, 150, 30, 0.7)',
                        'rgba(247, 37, 133, 0.7)',
                        'rgba(0, 200, 83, 0.7)'
                    ],
                    borderColor: [
                        'rgb(76, 201, 240)',
                        'rgb(67, 97, 238)',
                        'rgb(58, 12, 163)',
                        'rgb(114, 9, 183)',
                        'rgb(156, 136, 255)',
                        'rgb(248, 150, 30)',
                        'rgb(247, 37, 133)',
                        'rgb(0, 200, 83)'
                    ],
                    borderWidth: 1
                }]
            },
            options: {
                responsive: true,
                scales: {
                    y: {
                        beginAtZero: false,
                        min: 8,
                        max: 16,
                        title: {
                            display: true,
                            text: 'km per Liter'
                        }
                    }
                },
                plugins: {
                    legend: {
                        display: false
                    }
                }
            }
        });
    }

    initRadarChart() {
        const canvas = document.getElementById('performanceRadar');
        if (!canvas) return;

        const ctx = canvas.getContext('2d');
        
        this.charts.radar = new Chart(ctx, {
            type: 'radar',
            data: {
                labels: ['Safety', 'Efficiency', 'Consistency', 'Compliance', 'Punctuality', 'Economy'],
                datasets: [
                    {
                        label: 'Your Performance',
                        data: [85, 78, 92, 88, 76, 82],
                        backgroundColor: 'rgba(67, 97, 238, 0.2)',
                        borderColor: 'rgba(67, 97, 238, 1)',
                        borderWidth: 2,
                        pointBackgroundColor: 'rgba(67, 97, 238, 1)'
                    },
                    {
                        label: 'Fleet Average',
                        data: [75, 72, 78, 82, 70, 76],
                        backgroundColor: 'rgba(76, 201, 240, 0.2)',
                        borderColor: 'rgba(76, 201, 240, 1)',
                        borderWidth: 2,
                        pointBackgroundColor: 'rgba(76, 201, 240, 1)'
                    }
                ]
            },
            options: {
                responsive: true,
                scales: {
                    r: {
                        angleLines: {
                            display: true
                        },
                        suggestedMin: 50,
                        suggestedMax: 100,
                        ticks: {
                            stepSize: 10
                        }
                    }
                },
                plugins: {
                    legend: {
                        position: 'top'
                    }
                }
            }
        });
    }

    updatePerformanceChart() {
        if (!this.charts.performance) return;

        const period = document.getElementById('performancePeriod')?.value || '30';
        const days = parseInt(period);
        
        // Update chart with new period data
        const labels = [];
        const safetyData = [];
        const distanceData = [];
        const eventsData = [];
        
        for (let i = days - 1; i >= 0; i--) {
            const date = new Date();
            date.setDate(date.getDate() - i);
            
            if (days <= 30) {
                labels.push(date.getDate().toString().padStart(2, '0'));
            } else if (days <= 90) {
                if (i % 3 === 0) {
                    labels.push(date.toLocaleDateString('en-US', { month: 'short', day: 'numeric' }));
                } else {
                    labels.push('');
                }
            } else {
                if (i % 7 === 0) {
                    labels.push(date.toLocaleDateString('en-US', { month: 'short', day: 'numeric' }));
                } else {
                    labels.push('');
                }
            }
            
            // Generate sample data
            safetyData.push(800 + Math.random() * 200);
            distanceData.push(20 + Math.random() * 40);
            eventsData.push(Math.floor(Math.random() * 6));
        }
        
        this.charts.performance.data.labels = labels;
        this.charts.performance.data.datasets[0].data = safetyData;
        this.charts.performance.data.datasets[1].data = distanceData;
        this.charts.performance.data.datasets[2].data = eventsData;
        
        this.charts.performance.update();
    }

    updateMetrics() {
        // Calculate and update metrics
        const avgDistance = this.calculateAverage(this.performanceData.distances);
        const avgEvents = this.calculateAverage(this.performanceData.harshEvents);
        const avgFuel = this.calculateAverage(this.performanceData.fuelEfficiency);
        
        // Update metric displays
        document.getElementById('avgTripDistance').textContent = avgDistance.toFixed(1) + ' km';
        document.getElementById('harshBrakingRate').textContent = (avgEvents / avgDistance * 100).toFixed(1);
        document.getElementById('rapidAccelRate').textContent = (avgEvents / avgDistance * 100 * 0.7).toFixed(1);
        document.getElementById('fuelEfficiency').textContent = avgFuel.toFixed(1) + ' km/L';
        
        // Update cost metrics
        const costPerKm = 0.65 / avgFuel; // Assuming fuel price $0.65/L
        const dailyDistance = avgDistance;
        const monthlyCost = dailyDistance * 30 * costPerKm;
        
        document.getElementById('costPerKm').textContent = '$' + costPerKm.toFixed(2);
        document.getElementById('dailyCost').textContent = '$' + (dailyDistance * costPerKm).toFixed(2);
        document.getElementById('monthlyCost').textContent = '$' + monthlyCost.toFixed(2);
        
        // Update other metrics
        document.getElementById('avgTripDuration').textContent = Math.round(avgDistance / 40 * 60) + ' min';
        document.getElementById('averageSpeed').textContent = (avgDistance / (avgDistance / 40)).toFixed(1) + ' km/h';
        document.getElementById('totalDriveTime').textContent = Math.round(this.performanceData.distances.reduce((a, b) => a + b, 0) / 40) + ' hours';
    }

    calculateAverage(data) {
        return data.reduce((a, b) => a + b, 0) / data.length;
    }

    generateReport(type) {
        this.app.showLoading();
        
        setTimeout(() => {
            let reportData = {};
            
            switch(type) {
                case 'daily':
                    reportData = this.generateDailyReport();
                    break;
                case 'weekly':
                    reportData = this.generateWeeklyReport();
                    break;
                case 'monthly':
                    reportData = this.generateMonthlyReport();
                    break;
                default:
                    reportData = this.generateCustomReport();
            }
            
            this.displayReport(reportData);
            this.app.hideLoading();
        }, 1000);
    }

    generateDailyReport() {
        const today = new Date();
        const dateStr = today.toLocaleDateString('en-US', { 
            weekday: 'long', 
            year: 'numeric', 
            month: 'long', 
            day: 'numeric' 
        });
        
        return {
            title: `Daily Report - ${dateStr}`,
            summary: {
                trips: 3,
                distance: '112.4 km',
                duration: '2h 45m',
                fuelUsed: '9.2 L',
                totalCost: '$42.50',
                safetyScore: '925'
            },
            highlights: [
                'Excellent driving behavior throughout the day',
                'Fuel efficiency above average (12.2 km/L)',
                'No harsh braking incidents detected',
                'All trips completed within speed limits'
            ],
            recommendations: [
                'Consider carpooling for your daily commute',
                'Schedule vehicle maintenance in the next 2 weeks'
            ]
        };
    }

    generateWeeklyReport() {
        const lastWeek = new Date();
        lastWeek.setDate(lastWeek.getDate() - 7);
        
        return {
            title: `Weekly Report - ${lastWeek.toLocaleDateString()} to ${new Date().toLocaleDateString()}`,
            summary: {
                trips: 15,
                distance: '625.8 km',
                duration: '13h 20m',
                fuelUsed: '52.3 L',
                totalCost: '$245.60',
                safetyScore: '912',
                trend: '+2.5%'
            },
            analysis: {
                bestDay: 'Monday',
                worstDay: 'Friday',
                peakHour: '08:00-09:00',
                frequentRoute: 'Home - Office',
                costPerKm: '$0.15'
            },
            trends: [
                'Safety score improved by 2.5% compared to last week',
                'Fuel efficiency decreased slightly (11.9 km/L vs 12.2 km/L)',
                'Night driving reduced by 15%'
            ]
        };
    }

    generateMonthlyReport() {
        return {
            title: `Monthly Report - January 2024`,
            summary: {
                trips: 45,
                distance: '2,145.6 km',
                duration: '42h 15m',
                fuelUsed: '178.8 L',
                totalCost: '$840.36',
                safetyScore: '905',
                ranking: 'Top 15%'
            },
            comparisons: {
                vsLastMonth: {
                    distance: '+8.2%',
                    cost: '-3.5%',
                    safety: '+4.1%',
                    efficiency: '+2.3%'
                },
                vsFleetAverage: {
                    distance: '+12.5%',
                    cost: '-8.2%',
                    safety: '+7.3%',
                    efficiency: '+5.6%'
                }
            },
            achievements: [
                'Perfect safety score for 15 consecutive days',
                'Lowest monthly fuel cost in 6 months',
                'No traffic violations reported',
                'Completed all scheduled maintenance'
            ],
            goals: [
                'Reduce idling time by 10%',
                'Achieve 13 km/L fuel efficiency',
                'Maintain safety score above 920',
                'Reduce monthly expenses by 5%'
            ]
        };
    }

    displayReport(reportData) {
        // Create report modal
        const modal = document.createElement('div');
        modal.className = 'modal active';
        modal.style.display = 'flex';
        
        modal.innerHTML = `
            <div class="modal-content" style="max-width: 800px;">
                <div class="modal-header">
                    <h3>${reportData.title}</h3>
                    <button class="close" onclick="this.parentElement.parentElement.remove()">&times;</button>
                </div>
                <div class="modal-body">
                    <div class="report-summary">
                        <h4>Summary</h4>
                        <div class="summary-grid">
                            ${Object.entries(reportData.summary).map(([key, value]) => `
                                <div class="summary-item">
                                    <span class="label">${this.formatLabel(key)}:</span>
                                    <span class="value">${value}</span>
                                </div>
                            `).join('')}
                        </div>
                    </div>
                    
                    ${reportData.highlights ? `
                    <div class="report-section">
                        <h4>Highlights</h4>
                        <ul>
                            ${reportData.highlights.map(h => `<li>${h}</li>`).join('')}
                        </ul>
                    </div>
                    ` : ''}
                    
                    ${reportData.analysis ? `
                    <div class="report-section">
                        <h4>Analysis</h4>
                        <div class="analysis-grid">
                            ${Object.entries(reportData.analysis).map(([key, value]) => `
                                <div class="analysis-item">
                                    <span class="label">${this.formatLabel(key)}:</span>
                                    <span class="value">${value}</span>
                                </div>
                            `).join('')}
                        </div>
                    </div>
                    ` : ''}
                    
                    ${reportData.trends ? `
                    <div class="report-section">
                        <h4>Trends</h4>
                        <ul>
                            ${reportData.trends.map(t => `<li>${t}</li>`).join('')}
                        </ul>
                    </div>
                    ` : ''}
                    
                    ${reportData.recommendations ? `
                    <div class="report-section">
                        <h4>Recommendations</h4>
                        <ul>
                            ${reportData.recommendations.map(r => `<li>${r}</li>`).join('')}
                        </ul>
                    </div>
                    ` : ''}
                    
                    ${reportData.achievements ? `
                    <div class="report-section">
                        <h4>Achievements</h4>
                        <ul>
                            ${reportData.achievements.map(a => `<li>${a}</li>`).join('')}
                        </ul>
                    </div>
                    ` : ''}
                    
                    ${reportData.goals ? `
                    <div class="report-section">
                        <h4>Future Goals</h4>
                        <ul>
                            ${reportData.goals.map(g => `<li>${g}</li>`).join('')}
                        </ul>
                    </div>
                    ` : ''}
                </div>
                <div class="modal-footer">
                    <button class="btn btn-secondary" onclick="this.parentElement.parentElement.parentElement.remove()">Close</button>
                    <button class="btn btn-primary" onclick="this.exportReport('${reportData.title}')">
                        <i class="fas fa-download"></i> Export PDF
                    </button>
                    <button class="btn btn-success" onclick="this.printReport('${reportData.title}')">
                        <i class="fas fa-print"></i> Print
                    </button>
                </div>
            </div>
        `;
        
        document.body.appendChild(modal);
        
        // Add export and print functionality
        modal.querySelector('.btn-primary').onclick = () => this.exportReport(reportData.title);
        modal.querySelector('.btn-success').onclick = () => this.printReport(reportData.title);
    }

    formatLabel(text) {
        return text.replace(/([A-Z])/g, ' $1').replace(/^./, str => str.toUpperCase());
    }

    exportReport(title) {
        this.app.showToast(`Exporting "${title}"...`, 'info');
        
        // Simulate export process
        setTimeout(() => {
            this.app.showToast('Report exported successfully!', 'success');
            
            // Create download link
            const data = JSON.stringify(this.performanceData, null, 2);
            const blob = new Blob([data], { type: 'application/json' });
            const url = URL.createObjectURL(blob);
            
            const a = document.createElement('a');
            a.href = url;
            a.download = `${title.replace(/\s+/g, '_')}_${new Date().toISOString().split('T')[0]}.json`;
            document.body.appendChild(a);
            a.click();
            document.body.removeChild(a);
            URL.revokeObjectURL(url);
        }, 1500);
    }

    printReport(title) {
        this.app.showToast(`Preparing "${title}" for printing...`, 'info');
        
        setTimeout(() => {
            const printWindow = window.open('', '_blank');
            printWindow.document.write(`
                <html>
                    <head>
                        <title>${title}</title>
                        <style>
                            body { font-family: Arial, sans-serif; margin: 20px; }
                            h1 { color: #333; border-bottom: 2px solid #4361ee; padding-bottom: 10px; }
                            .summary { background: #f8f9fa; padding: 15px; border-radius: 5px; margin: 20px 0; }
                            .section { margin: 20px 0; }
                            .item { margin: 10px 0; }
                            .label { font-weight: bold; color: #666; }
                            .value { color: #333; }
                            @media print {
                                .no-print { display: none; }
                                body { margin: 0; }
                            }
                        </style>
                    </head>
                    <body>
                        <h1>${title}</h1>
                        <div class="summary">
                            <h3>Performance Summary</h3>
                            <div class="item">
                                <span class="label">Total Distance:</span>
                                <span class="value">${this.performanceData.distances.reduce((a, b) => a + b, 0).toFixed(1)} km</span>
                            </div>
                            <div class="item">
                                <span class="label">Average Safety Score:</span>
                                <span class="value">${this.calculateAverage(this.performanceData.safetyScores).toFixed(0)}</span>
                            </div>
                            <div class="item">
                                <span class="label">Fuel Efficiency:</span>
                                <span class="value">${this.calculateAverage(this.performanceData.fuelEfficiency).toFixed(1)} km/L</span>
                            </div>
                        </div>
                        <div class="section">
                            <h3>Detailed Analysis</h3>
                            <p>Report generated on ${new Date().toLocaleString()}</p>
                            <p>This report contains comprehensive analytics of your driving performance.</p>
                        </div>
                        <div class="no-print">
                            <button onclick="window.print()">Print Report</button>
                            <button onclick="window.close()">Close</button>
                        </div>
                    </body>
                </html>
            `);
            printWindow.document.close();
        }, 1000);
    }

    updateCharts() {
        if (this.app.currentPage === 'reports') {
            this.updatePerformanceChart();
            this.updateMetrics();
        }
    }

    // Public methods for global access
    generateDailyReport() { return this.generateReport('daily'); }
    generateWeeklyReport() { return this.generateReport('weekly'); }
    generateMonthlyReport() { return this.generateReport('monthly'); }
    generateCustomReport() { return this.generateReport('custom'); }
}

// Initialize analytics manager
if (window.app) {
    window.analytics = new AnalyticsManager(window.app);
}