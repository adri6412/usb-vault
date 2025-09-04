// VaultUSB - Main JavaScript Application

class VaultUSB {
    constructor() {
        this.apiBase = '/api';
        this.token = localStorage.getItem('vaultusb_token');
        this.init();
    }

    init() {
        this.setupEventListeners();
        this.checkAuthentication();
        this.setupIdleTimeout();
    }

    setupEventListeners() {
        // Navigation toggle for mobile
        const navToggle = document.getElementById('navToggle');
        const navMenu = document.getElementById('navMenu');
        
        if (navToggle && navMenu) {
            navToggle.addEventListener('click', () => {
                navMenu.classList.toggle('active');
            });
        }

        // Logout button
        const logoutBtn = document.getElementById('logoutBtn');
        if (logoutBtn) {
            logoutBtn.addEventListener('click', () => {
                this.logout();
            });
        }

        // Notification close
        const notificationClose = document.getElementById('notificationClose');
        if (notificationClose) {
            notificationClose.addEventListener('click', () => {
                this.hideNotification();
            });
        }

        // Global click handler for modals
        document.addEventListener('click', (e) => {
            if (e.target.classList.contains('modal')) {
                this.hideModal(e.target);
            }
        });

        // Global escape key handler
        document.addEventListener('keydown', (e) => {
            if (e.key === 'Escape') {
                const openModal = document.querySelector('.modal:not(.hidden)');
                if (openModal) {
                    this.hideModal(openModal);
                }
            }
        });
    }

    checkAuthentication() {
        if (!this.token && !window.location.pathname.includes('login')) {
            window.location.href = '/';
            return;
        }
    }

    setupIdleTimeout() {
        let idleTimer;
        const idleTimeout = 10 * 60 * 1000; // 10 minutes

        const resetIdleTimer = () => {
            clearTimeout(idleTimer);
            idleTimer = setTimeout(() => {
                this.handleIdleTimeout();
            }, idleTimeout);
        };

        // Reset timer on user activity
        ['mousedown', 'mousemove', 'keypress', 'scroll', 'touchstart'].forEach(event => {
            document.addEventListener(event, resetIdleTimer, true);
        });

        resetIdleTimer();
    }

    handleIdleTimeout() {
        if (this.token) {
            this.showNotification('Session expired due to inactivity', 'warning');
            this.logout();
        }
    }

    async apiRequest(endpoint, options = {}) {
        const url = `${this.apiBase}${endpoint}`;
        const config = {
            headers: {
                'Content-Type': 'application/json',
                ...(this.token && { 'Authorization': `Bearer ${this.token}` })
            },
            ...options
        };

        try {
            const response = await fetch(url, config);
            
            if (response.status === 401) {
                this.logout();
                throw new Error('Unauthorized');
            }

            if (response.status === 423) {
                throw new Error('Vault is locked');
            }

            if (!response.ok) {
                const errorData = await response.json().catch(() => ({}));
                throw new Error(errorData.message || `HTTP ${response.status}`);
            }

            const contentType = response.headers.get('content-type');
            if (contentType && contentType.includes('application/json')) {
                return await response.json();
            } else {
                return response;
            }
        } catch (error) {
            console.error('API request failed:', error);
            throw error;
        }
    }

    showNotification(message, type = 'info') {
        const notification = document.getElementById('notification');
        const notificationText = document.getElementById('notificationText');
        
        if (!notification || !notificationText) return;

        notificationText.textContent = message;
        notification.className = `notification ${type}`;
        notification.classList.remove('hidden');

        // Auto-hide after 5 seconds
        setTimeout(() => {
            this.hideNotification();
        }, 5000);
    }

    hideNotification() {
        const notification = document.getElementById('notification');
        if (notification) {
            notification.classList.add('hidden');
        }
    }

    showLoading() {
        const loading = document.getElementById('loading');
        if (loading) {
            loading.classList.remove('hidden');
        }
    }

    hideLoading() {
        const loading = document.getElementById('loading');
        if (loading) {
            loading.classList.add('hidden');
        }
    }

    showModal(modalId) {
        const modal = document.getElementById(modalId);
        if (modal) {
            modal.classList.remove('hidden');
            document.body.style.overflow = 'hidden';
        }
    }

    hideModal(modal) {
        if (typeof modal === 'string') {
            modal = document.getElementById(modal);
        }
        if (modal) {
            modal.classList.add('hidden');
            document.body.style.overflow = '';
        }
    }

    async logout() {
        try {
            if (this.token) {
                await this.apiRequest('/api/auth/logout', {
                    method: 'POST'
                });
            }
        } catch (error) {
            console.error('Logout error:', error);
        } finally {
            localStorage.removeItem('vaultusb_token');
            this.token = null;
            window.location.href = '/';
        }
    }

    formatFileSize(bytes) {
        if (bytes === 0) return '0 B';
        const k = 1024;
        const sizes = ['B', 'KB', 'MB', 'GB'];
        const i = Math.floor(Math.log(bytes) / Math.log(k));
        return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
    }

    formatDate(dateString) {
        return new Date(dateString).toLocaleDateString();
    }

    formatDateTime(dateString) {
        return new Date(dateString).toLocaleString();
    }

    formatUptime(seconds) {
        const days = Math.floor(seconds / 86400);
        const hours = Math.floor((seconds % 86400) / 3600);
        const minutes = Math.floor((seconds % 3600) / 60);
        
        if (days > 0) {
            return `${days}d ${hours}h ${minutes}m`;
        } else if (hours > 0) {
            return `${hours}h ${minutes}m`;
        } else {
            return `${minutes}m`;
        }
    }

    getFileIcon(mimeType) {
        if (mimeType.startsWith('image/')) return '🖼️';
        if (mimeType.startsWith('video/')) return '🎥';
        if (mimeType.startsWith('audio/')) return '🎵';
        if (mimeType.includes('pdf')) return '📄';
        if (mimeType.includes('text/')) return '📝';
        if (mimeType.includes('zip') || mimeType.includes('archive')) return '📦';
        return '📁';
    }

    debounce(func, wait) {
        let timeout;
        return function executedFunction(...args) {
            const later = () => {
                clearTimeout(timeout);
                func(...args);
            };
            clearTimeout(timeout);
            timeout = setTimeout(later, wait);
        };
    }

    throttle(func, limit) {
        let inThrottle;
        return function() {
            const args = arguments;
            const context = this;
            if (!inThrottle) {
                func.apply(context, args);
                inThrottle = true;
                setTimeout(() => inThrottle = false, limit);
            }
        };
    }
}

// Global functions for backward compatibility
let app;

document.addEventListener('DOMContentLoaded', function() {
    app = new VaultUSB();
});

// Global API request function
async function apiRequest(endpoint, options = {}) {
    if (app) {
        return await app.apiRequest(endpoint, options);
    } else {
        throw new Error('App not initialized');
    }
}

// Global utility functions
function showNotification(message, type = 'info') {
    if (app) {
        app.showNotification(message, type);
    }
}

function hideNotification() {
    if (app) {
        app.hideNotification();
    }
}

function showLoading() {
    if (app) {
        app.showLoading();
    }
}

function hideLoading() {
    if (app) {
        app.hideLoading();
    }
}

function showModal(modalId) {
    if (app) {
        app.showModal(modalId);
    }
}

function hideModal(modal) {
    if (app) {
        app.hideModal(modal);
    }
}

// File upload helper
function createFileUploadHandler(inputElement, onProgress, onComplete, onError) {
    inputElement.addEventListener('change', async function(e) {
        const files = Array.from(e.target.files);
        if (files.length === 0) return;

        for (let i = 0; i < files.length; i++) {
            const file = files[i];
            const formData = new FormData();
            formData.append('file', file);

            try {
                if (onProgress) {
                    onProgress(file, i, files.length);
                }

                const response = await apiRequest('/api/files/upload', {
                    method: 'POST',
                    body: formData
                });

                if (response.success) {
                    if (onComplete) {
                        onComplete(file, response);
                    }
                } else {
                    if (onError) {
                        onError(file, response.message);
                    }
                }
            } catch (error) {
                if (onError) {
                    onError(file, error.message);
                }
            }
        }
    });
}

// Drag and drop helper
function setupDragAndDrop(element, onDrop) {
    element.addEventListener('dragover', function(e) {
        e.preventDefault();
        e.currentTarget.classList.add('drag-over');
    });

    element.addEventListener('dragleave', function(e) {
        e.preventDefault();
        e.currentTarget.classList.remove('drag-over');
    });

    element.addEventListener('drop', function(e) {
        e.preventDefault();
        e.currentTarget.classList.remove('drag-over');
        const files = Array.from(e.dataTransfer.files);
        if (files.length > 0 && onDrop) {
            onDrop(files);
        }
    });
}

// Form validation helper
function validateForm(formElement) {
    const inputs = formElement.querySelectorAll('input[required], select[required], textarea[required]');
    let isValid = true;

    inputs.forEach(input => {
        if (!input.value.trim()) {
            input.classList.add('error');
            isValid = false;
        } else {
            input.classList.remove('error');
        }
    });

    return isValid;
}

// Clear form helper
function clearForm(formElement) {
    formElement.reset();
    const inputs = formElement.querySelectorAll('input, select, textarea');
    inputs.forEach(input => {
        input.classList.remove('error');
    });
}

// Error handling
window.addEventListener('error', function(e) {
    console.error('Global error:', e.error);
    if (app) {
        app.showNotification('An unexpected error occurred', 'error');
    }
});

window.addEventListener('unhandledrejection', function(e) {
    console.error('Unhandled promise rejection:', e.reason);
    if (app) {
        app.showNotification('An unexpected error occurred', 'error');
    }
});

// Export for use in other scripts
if (typeof module !== 'undefined' && module.exports) {
    module.exports = { VaultUSB, apiRequest, showNotification, hideNotification };
}
