// main.js - Main Initialization File (UPDATED)
document.addEventListener('DOMContentLoaded', () => {
    console.log('📱 Smart Drive Web App Loading...');
    
    // First check authentication
    const sessionId = localStorage.getItem('session_id');
    const userData = localStorage.getItem('user_data');
    
    if (!sessionId || !userData) {
        console.log('❌ No valid session found, redirecting to login...');
        window.location.href = 'login.html';
        return;
    }
    
    try {
        // Initialize main application
        window.app = new SmartDriveWebApp();
        
        console.log('✅ Smart Drive Web App initialized');
    } catch (error) {
        console.error('❌ Failed to initialize app:', error);
        alert('Failed to initialize application. Please refresh the page.');
    }
});