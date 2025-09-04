"""Database connection and session management."""

import asyncio
from sqlmodel import SQLModel, create_engine, Session as SQLSession
from app.config import config
from app.models import User, File, Session, WiFiNetwork, SystemLog
import aiosqlite
from pathlib import Path

# SQLite database URL
DATABASE_URL = f"sqlite:///{config.db_file}"

# Create engine
engine = create_engine(DATABASE_URL, echo=config.debug)

def create_tables():
    """Create all database tables."""
    SQLModel.metadata.create_all(engine)

def get_session():
    """Get a database session."""
    return SQLSession(engine)

async def init_database():
    """Initialize the database with tables and default user."""
    # Ensure vault directory exists
    vault_dir = Path(config.vault_dir)
    vault_dir.mkdir(parents=True, exist_ok=True)
    
    # Create tables
    create_tables()
    
    # Create default admin user if none exists
    with get_session() as session:
        admin_user = session.query(User).filter(User.username == "admin").first()
        if not admin_user:
            from app.auth import hash_password
            admin_user = User(
                username="admin",
                password_hash=hash_password("admin"),
                is_active=True
            )
            session.add(admin_user)
            session.commit()
            print("Created default admin user with password 'admin'")

async def cleanup_expired_sessions():
    """Clean up expired sessions."""
    from datetime import datetime, timedelta
    from app.config import config
    
    with get_session() as session:
        # Remove sessions older than idle timeout
        cutoff_time = datetime.utcnow() - timedelta(seconds=config.idle_timeout)
        expired_sessions = session.query(Session).filter(
            Session.last_activity < cutoff_time
        ).all()
        
        for expired_session in expired_sessions:
            session.delete(expired_session)
        
        session.commit()

async def log_system_event(level: str, message: str, component: str, user_id: int = None):
    """Log a system event."""
    with get_session() as session:
        log_entry = SystemLog(
            level=level,
            message=message,
            component=component,
            user_id=user_id
        )
        session.add(log_entry)
        session.commit()
