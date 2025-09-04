"""Pydantic schemas for API requests and responses."""

from pydantic import BaseModel, Field
from typing import Optional, List
from datetime import datetime

# Authentication schemas
class LoginRequest(BaseModel):
    username: str = Field(..., min_length=1, max_length=50)
    password: str = Field(..., min_length=1)

class LoginResponse(BaseModel):
    success: bool
    message: str
    session_id: Optional[str] = None

class ChangePasswordRequest(BaseModel):
    current_password: str
    new_password: str = Field(..., min_length=8)

class TOTPSetupRequest(BaseModel):
    password: str

class TOTPVerifyRequest(BaseModel):
    token: str = Field(..., min_length=6, max_length=6)

# File schemas
class FileInfo(BaseModel):
    id: str
    original_name: str
    size: int
    mime_type: str
    created_at: datetime
    modified_at: datetime

class FileUploadResponse(BaseModel):
    success: bool
    message: str
    file_id: Optional[str] = None

class FileListResponse(BaseModel):
    files: List[FileInfo]
    total: int

# WiFi schemas
class WiFiNetwork(BaseModel):
    ssid: str
    bssid: str
    frequency: int
    signal_level: int
    security: str
    flags: List[str]

class WiFiConnectRequest(BaseModel):
    ssid: str
    password: Optional[str] = None
    security: str = "WPA"

class WiFiStatus(BaseModel):
    interface: str
    status: str
    ssid: Optional[str] = None
    ip_address: Optional[str] = None
    signal_level: Optional[int] = None

# System schemas
class PackageUpdate(BaseModel):
    package: str
    current_version: str
    available_version: str
    priority: str

class SystemStatus(BaseModel):
    uptime: int
    memory_usage: float
    disk_usage: float
    cpu_usage: float
    reboot_required: bool

class UpdateCheckResponse(BaseModel):
    updates_available: bool
    packages: List[PackageUpdate]
    total_packages: int

class UpgradeResponse(BaseModel):
    success: bool
    message: str
    log: Optional[str] = None

# Generic response schemas
class SuccessResponse(BaseModel):
    success: bool
    message: str

class ErrorResponse(BaseModel):
    success: bool = False
    error: str
    details: Optional[str] = None
