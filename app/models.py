"""Database models for VaultUSB."""

from sqlmodel import SQLModel, Field
from datetime import datetime
from typing import Optional
import uuid

class User(SQLModel, table=True):
    """User model for authentication."""
    id: Optional[int] = Field(default=None, primary_key=True)
    username: str = Field(unique=True, index=True)
    password_hash: str
    totp_secret: Optional[str] = None
    totp_enabled: bool = False
    created_at: datetime = Field(default_factory=datetime.utcnow)
    last_login: Optional[datetime] = None
    is_active: bool = True

class File(SQLModel, table=True):
    """File model for encrypted file storage."""
    id: str = Field(default_factory=lambda: str(uuid.uuid4()), primary_key=True)
    original_name: str
    encrypted_name: str
    size: int
    mime_type: str
    created_at: datetime = Field(default_factory=datetime.utcnow)
    modified_at: datetime = Field(default_factory=datetime.utcnow)
    user_id: int = Field(foreign_key="user.id")
    is_deleted: bool = False

class Session(SQLModel, table=True):
    """Session model for user sessions."""
    id: str = Field(default_factory=lambda: str(uuid.uuid4()), primary_key=True)
    user_id: int = Field(foreign_key="user.id")
    created_at: datetime = Field(default_factory=datetime.utcnow)
    last_activity: datetime = Field(default_factory=datetime.utcnow)
    is_active: bool = True
    ip_address: Optional[str] = None
    user_agent: Optional[str] = None

class WiFiNetwork(SQLModel, table=True):
    """WiFi network model for saved networks."""
    id: Optional[int] = Field(default=None, primary_key=True)
    ssid: str = Field(index=True)
    security: str  # WPA, WEP, Open, etc.
    priority: int = Field(default=0)
    created_at: datetime = Field(default_factory=datetime.utcnow)
    is_active: bool = True
    # Note: We don't store passwords in DB, only in wpa_supplicant

class SystemLog(SQLModel, table=True):
    """System log model for tracking operations."""
    id: Optional[int] = Field(default=None, primary_key=True)
    level: str  # INFO, WARNING, ERROR
    message: str
    component: str  # wifi, system, auth, etc.
    created_at: datetime = Field(default_factory=datetime.utcnow)
    user_id: Optional[int] = Field(foreign_key="user.id", default=None)
