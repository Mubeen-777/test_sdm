// login.js - Login Page Logic
document.addEventListener('DOMContentLoaded', () => {
    const loginForm = document.getElementById('loginForm');
    const errorMessage = document.getElementById('errorMessage');
    
    loginForm.addEventListener('submit', async (e) => {
        e.preventDefault();
        
        const username = document.getElementById('username').value;
        const password = document.getElementById('password').value;
        
        // Show loading
        const submitBtn = loginForm.querySelector('button[type="submit"]');
        const originalText = submitBtn.innerHTML;
        submitBtn.innerHTML = '<i class="fas fa-spinner fa-spin"></i> Logging in...';
        submitBtn.disabled = true;
        
        try {
            // Try to authenticate
            const response = await fetch('http://localhost:8080/api/login', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                },
                body: JSON.stringify({
                    username: username,
                    password: password
                })
            });
            
            if (!response.ok) {
                throw new Error(`HTTP ${response.status}`);
            }
            
            const result = await response.json();
            
            if (result.status === 'success') {
                // Store session data
                localStorage.setItem('session_id', result.data.session_id);
                localStorage.setItem('user_data', JSON.stringify({
                    driver_id: result.data.driver_id,
                    name: result.data.name,
                    role: result.data.role,
                    username: username
                }));
                
                // Redirect to main app
                window.location.href = 'index.html';
            } else {
                errorMessage.textContent = result.message || 'Invalid credentials';
                errorMessage.style.display = 'block';
            }
        } catch (error) {
            console.error('Login error:', error);
            errorMessage.textContent = 'Connection failed. Make sure backend is running.';
            errorMessage.style.display = 'block';
            
            // For demo purposes, allow login without backend
            if (username === 'admin' && password === 'admin123') {
                localStorage.setItem('session_id', 'demo_session_' + Date.now());
                localStorage.setItem('user_data', JSON.stringify({
                    driver_id: 1,
                    name: 'Mubeen Butt',
                    role: '1',
                    username: 'admin'
                }));
                window.location.href = 'index.html';
            }
        } finally {
            submitBtn.innerHTML = originalText;
            submitBtn.disabled = false;
        }
    });
    
    // Auto-login for development
    const urlParams = new URLSearchParams(window.location.search);
    if (urlParams.get('auto') === '1') {
        document.getElementById('username').value = 'admin';
        document.getElementById('password').value = 'admin123';
        loginForm.dispatchEvent(new Event('submit'));
    }
});
