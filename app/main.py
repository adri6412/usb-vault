"""Main FastAPI application for VaultUSB."""

import os
import asyncio
from datetime import datetime, timedelta
from typing import Optional
from fastapi import FastAPI, Request, Response, HTTPException, Depends, status, UploadFile, File, Form
from fastapi.responses import HTMLResponse, FileResponse, StreamingResponse
from fastapi.staticfiles import StaticFiles
from fastapi.templating import Jinja2Templates
from fastapi.middleware.cors import CORSMiddleware
from fastapi.security import HTTPBearer, HTTPAuthorizationCredentials
import uvicorn
from pathlib import Path

from app.config import config
from app.db import init_database, cleanup_expired_sessions, log_system_event
from app.auth import auth_manager
from app.crypto import crypto_manager
from app.storage import storage_manager
from app.wifi import wifi_manager
from app.system import system_manager
from app.schemas import (
    LoginRequest, LoginResponse, ChangePasswordRequest, TOTPSetupRequest, TOTPVerifyRequest,
    FileUploadResponse, FileListResponse, WiFiConnectRequest, WiFiStatus, SystemStatus,
    UpdateCheckResponse, UpgradeResponse, SuccessResponse, ErrorResponse
)

# Initialize FastAPI app
app = FastAPI(
    title="VaultUSB",
    description="Raspberry Pi Zero USB Vault with Wi-Fi Management",
    version=config.app_version
)

# Add CORS middleware
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# Security
security = HTTPBearer()

# Templates and static files
templates = Jinja2Templates(directory="app/templates")
app.mount("/static", StaticFiles(directory="app/static"), name="static")

# Global state
vault_unlocked = False
last_activity = datetime.utcnow()

# Dependency functions
async def get_current_user(credentials: HTTPAuthorizationCredentials = Depends(security)):
    """Get current authenticated user."""
    token = credentials.credentials
    user = auth_manager.verify_session(token)
    
    if not user:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Invalid authentication credentials",
            headers={"WWW-Authenticate": "Bearer"},
        )
    
    return user

async def check_vault_unlocked():
    """Check if vault is unlocked."""
    global vault_unlocked
    if not vault_unlocked or not crypto_manager.is_unlocked():
        raise HTTPException(
            status_code=status.HTTP_423_LOCKED,
            detail="Vault is locked. Please unlock with your password."
        )

async def update_activity():
    """Update last activity timestamp."""
    global last_activity
    last_activity = datetime.utcnow()

# Startup and shutdown events
@app.on_event("startup")
async def startup_event():
    """Initialize application on startup."""
    await init_database()
    log_system_event("INFO", "VaultUSB application started", "app")

@app.on_event("shutdown")
async def shutdown_event():
    """Cleanup on shutdown."""
    log_system_event("INFO", "VaultUSB application stopped", "app")

# Background task for session cleanup
async def cleanup_sessions_task():
    """Background task to clean up expired sessions."""
    while True:
        await asyncio.sleep(300)  # Run every 5 minutes
        await cleanup_expired_sessions()

# Start background task
asyncio.create_task(cleanup_sessions_task())

# Root endpoint
@app.get("/", response_class=HTMLResponse)
async def root(request: Request):
    """Root endpoint - redirect to login."""
    return templates.TemplateResponse("login.html", {"request": request})

# Authentication endpoints
@app.post("/api/auth/login", response_model=LoginResponse)
async def login(request: LoginRequest, http_request: Request):
    """Login endpoint."""
    user = auth_manager.authenticate_user(request.username, request.password)
    
    if not user:
        log_system_event("WARNING", f"Failed login attempt for user: {request.username}", "auth")
        return LoginResponse(
            success=False,
            message="Invalid username or password"
        )
    
    # Check TOTP if enabled
    if user.totp_enabled:
        # For now, we'll require TOTP in a separate step
        # In a real implementation, you'd include TOTP in the login request
        pass
    
    session_token = auth_manager.create_session(
        user,
        ip_address=http_request.client.host,
        user_agent=http_request.headers.get("user-agent")
    )
    
    return LoginResponse(
        success=True,
        message="Login successful",
        session_id=session_token
    )

@app.post("/api/auth/logout", response_model=SuccessResponse)
async def logout(token: str = Depends(get_current_user)):
    """Logout endpoint."""
    # In a real implementation, you'd invalidate the session token
    return SuccessResponse(success=True, message="Logged out successfully")

@app.post("/api/auth/change-password", response_model=SuccessResponse)
async def change_password(
    request: ChangePasswordRequest,
    user = Depends(get_current_user)
):
    """Change user password."""
    success = auth_manager.change_password(user, request.current_password, request.new_password)
    
    if success:
        return SuccessResponse(success=True, message="Password changed successfully")
    else:
        return SuccessResponse(success=False, message="Current password is incorrect")

@app.post("/api/auth/setup-totp", response_model=dict)
async def setup_totp(
    request: TOTPSetupRequest,
    user = Depends(get_current_user)
):
    """Setup TOTP for user."""
    qr_url = auth_manager.setup_totp(user, request.password)
    
    if qr_url:
        return {"success": True, "qr_url": qr_url}
    else:
        return {"success": False, "message": "Invalid password"}

@app.post("/api/auth/verify-totp", response_model=SuccessResponse)
async def verify_totp(
    request: TOTPVerifyRequest,
    user = Depends(get_current_user)
):
    """Verify and enable TOTP."""
    success = auth_manager.enable_totp(user, request.token)
    
    if success:
        return SuccessResponse(success=True, message="TOTP enabled successfully")
    else:
        return SuccessResponse(success=False, message="Invalid TOTP token")

# Vault management endpoints
@app.post("/api/vault/unlock", response_model=SuccessResponse)
async def unlock_vault(password: str = Form(...), user = Depends(get_current_user)):
    """Unlock the vault with master password."""
    global vault_unlocked
    
    success = crypto_manager.load_master_key(password)
    
    if success:
        vault_unlocked = True
        await update_activity()
        log_system_event("INFO", f"Vault unlocked by user {user.username}", "vault", user.id)
        return SuccessResponse(success=True, message="Vault unlocked successfully")
    else:
        log_system_event("WARNING", f"Failed vault unlock attempt by user {user.username}", "vault", user.id)
        return SuccessResponse(success=False, message="Invalid master password")

@app.post("/api/vault/lock", response_model=SuccessResponse)
async def lock_vault(user = Depends(get_current_user)):
    """Lock the vault."""
    global vault_unlocked
    
    crypto_manager.lock()
    vault_unlocked = False
    log_system_event("INFO", f"Vault locked by user {user.username}", "vault", user.id)
    return SuccessResponse(success=True, message="Vault locked successfully")

@app.get("/api/vault/status", response_model=dict)
async def vault_status(user = Depends(get_current_user)):
    """Get vault status."""
    global vault_unlocked
    return {
        "unlocked": vault_unlocked and crypto_manager.is_unlocked(),
        "last_activity": last_activity.isoformat()
    }

# File management endpoints
@app.get("/api/files", response_model=FileListResponse)
async def list_files(
    limit: int = 100,
    offset: int = 0,
    user = Depends(get_current_user)
):
    """List user's files."""
    await check_vault_unlocked()
    await update_activity()
    
    files, total = storage_manager.list_files(user, limit, offset)
    
    file_infos = []
    for file in files:
        file_infos.append({
            "id": file.id,
            "original_name": file.original_name,
            "size": file.size,
            "mime_type": file.mime_type,
            "created_at": file.created_at,
            "modified_at": file.modified_at
        })
    
    return FileListResponse(files=file_infos, total=total)

@app.post("/api/files/upload", response_model=FileUploadResponse)
async def upload_file(
    file: UploadFile = File(...),
    user = Depends(get_current_user)
):
    """Upload a file to the vault."""
    await check_vault_unlocked()
    await update_activity()
    
    # Read file data
    file_data = await file.read()
    
    # Store file
    file_id = storage_manager.store_file(file_data, file.filename, user)
    
    if file_id:
        return FileUploadResponse(
            success=True,
            message="File uploaded successfully",
            file_id=file_id
        )
    else:
        return FileUploadResponse(
            success=False,
            message="Failed to upload file"
        )

@app.get("/api/files/{file_id}/download")
async def download_file(
    file_id: str,
    user = Depends(get_current_user)
):
    """Download a file from the vault."""
    await check_vault_unlocked()
    await update_activity()
    
    file_info = storage_manager.get_file_info(file_id, user)
    if not file_info:
        raise HTTPException(status_code=404, detail="File not found")
    
    file_data = storage_manager.retrieve_file(file_id, user)
    if not file_data:
        raise HTTPException(status_code=500, detail="Failed to retrieve file")
    
    return StreamingResponse(
        iter([file_data]),
        media_type=file_info.mime_type,
        headers={
            "Content-Disposition": f"attachment; filename={file_info.original_name}"
        }
    )

@app.get("/api/files/{file_id}/preview")
async def preview_file(
    file_id: str,
    user = Depends(get_current_user)
):
    """Preview a file from the vault."""
    await check_vault_unlocked()
    await update_activity()
    
    file_info = storage_manager.get_file_info(file_id, user)
    if not file_info:
        raise HTTPException(status_code=404, detail="File not found")
    
    # Only allow preview of text files and images
    if not file_info.mime_type.startswith(('text/', 'image/')):
        raise HTTPException(status_code=400, detail="File type not supported for preview")
    
    file_data = storage_manager.retrieve_file(file_id, user)
    if not file_data:
        raise HTTPException(status_code=500, detail="Failed to retrieve file")
    
    return StreamingResponse(
        iter([file_data]),
        media_type=file_info.mime_type
    )

@app.delete("/api/files/{file_id}", response_model=SuccessResponse)
async def delete_file(
    file_id: str,
    user = Depends(get_current_user)
):
    """Delete a file from the vault."""
    await check_vault_unlocked()
    await update_activity()
    
    success = storage_manager.delete_file(file_id, user)
    
    if success:
        return SuccessResponse(success=True, message="File deleted successfully")
    else:
        return SuccessResponse(success=False, message="Failed to delete file")

# Wi-Fi management endpoints
@app.get("/api/wifi/networks", response_model=list)
async def scan_wifi_networks(user = Depends(get_current_user)):
    """Scan for Wi-Fi networks."""
    await update_activity()
    networks = wifi_manager.scan_networks()
    return [network.dict() for network in networks]

@app.get("/api/wifi/status", response_model=WiFiStatus)
async def get_wifi_status(user = Depends(get_current_user)):
    """Get current Wi-Fi status."""
    await update_activity()
    return wifi_manager.get_status()

@app.post("/api/wifi/connect", response_model=SuccessResponse)
async def connect_wifi(
    request: WiFiConnectRequest,
    user = Depends(get_current_user)
):
    """Connect to a Wi-Fi network."""
    await update_activity()
    
    success, message = wifi_manager.connect(request)
    
    return SuccessResponse(success=success, message=message)

@app.post("/api/wifi/disconnect", response_model=SuccessResponse)
async def disconnect_wifi(user = Depends(get_current_user)):
    """Disconnect from current Wi-Fi network."""
    await update_activity()
    
    success, message = wifi_manager.disconnect()
    
    return SuccessResponse(success=success, message=message)

@app.delete("/api/wifi/networks/{ssid}", response_model=SuccessResponse)
async def forget_wifi_network(
    ssid: str,
    user = Depends(get_current_user)
):
    """Forget a saved Wi-Fi network."""
    await update_activity()
    
    success, message = wifi_manager.forget_network(ssid)
    
    return SuccessResponse(success=success, message=message)

# System management endpoints
@app.get("/api/system/status", response_model=SystemStatus)
async def get_system_status(user = Depends(get_current_user)):
    """Get system status."""
    await update_activity()
    return system_manager.get_system_status()

@app.get("/api/system/updates", response_model=UpdateCheckResponse)
async def check_updates(user = Depends(get_current_user)):
    """Check for system updates."""
    await update_activity()
    return system_manager.check_updates()

@app.post("/api/system/upgrade", response_model=UpgradeResponse)
async def upgrade_system(user = Depends(get_current_user)):
    """Upgrade the system."""
    await update_activity()
    return system_manager.upgrade_system()

@app.post("/api/system/reboot", response_model=SuccessResponse)
async def reboot_system(user = Depends(get_current_user)):
    """Reboot the system."""
    await update_activity()
    
    success, message = system_manager.reboot_system()
    
    return SuccessResponse(success=success, message=message)

# Web UI endpoints
@app.get("/dashboard", response_class=HTMLResponse)
async def dashboard(request: Request):
    """Dashboard page."""
    return templates.TemplateResponse("dashboard.html", {"request": request})

@app.get("/files", response_class=HTMLResponse)
async def files_page(request: Request):
    """Files management page."""
    return templates.TemplateResponse("files.html", {"request": request})

@app.get("/wifi", response_class=HTMLResponse)
async def wifi_page(request: Request):
    """Wi-Fi management page."""
    return templates.TemplateResponse("wifi.html", {"request": request})

@app.get("/system", response_class=HTMLResponse)
async def system_page(request: Request):
    """System management page."""
    return templates.TemplateResponse("system.html", {"request": request})

# Health check endpoint
@app.get("/health")
async def health_check():
    """Health check endpoint."""
    return {"status": "healthy", "timestamp": datetime.utcnow().isoformat()}

if __name__ == "__main__":
    uvicorn.run(
        "app.main:app",
        host=config.host,
        port=config.port,
        reload=config.debug
    )
