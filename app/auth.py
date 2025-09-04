"""Authentication and authorization functions."""

import secrets
import pyotp
from datetime import datetime, timedelta
from typing import Optional
from argon2 import PasswordHasher
from argon2.exceptions import VerifyMismatch
from itsdangerous import TimedJSONWebSignatureSerializer
from app.config import config
from app.db import get_session, log_system_event
from app.models import User, Session

class AuthManager:
    """Manages user authentication and sessions."""
    
    def __init__(self):
        self.argon2_hasher = PasswordHasher(
            time_cost=config.argon2_time_cost,
            memory_cost=config.argon2_memory_cost,
            parallelism=config.argon2_parallelism
        )
        self.serializer = TimedJSONWebSignatureSerializer(
            config.secret_key,
            expires_in=config.idle_timeout
        )
    
    def hash_password(self, password: str) -> str:
        """Hash a password using Argon2id."""
        return self.argon2_hasher.hash(password)
    
    def verify_password(self, password: str, password_hash: str) -> bool:
        """Verify a password against its hash."""
        try:
            self.argon2_hasher.verify(password_hash, password)
            return True
        except VerifyMismatch:
            return False
    
    def authenticate_user(self, username: str, password: str) -> Optional[User]:
        """Authenticate a user with username and password."""
        with get_session() as session:
            user = session.query(User).filter(
                User.username == username,
                User.is_active == True
            ).first()
            
            if user and self.verify_password(password, user.password_hash):
                # Update last login
                user.last_login = datetime.utcnow()
                session.commit()
                return user
        
        return None
    
    def create_session(self, user: User, ip_address: str = None, user_agent: str = None) -> str:
        """Create a new user session."""
        with get_session() as session:
            # Create session record
            session_record = Session(
                user_id=user.id,
                ip_address=ip_address,
                user_agent=user_agent
            )
            session.add(session_record)
            session.commit()
            
            # Create session token
            session_data = {
                'user_id': user.id,
                'username': user.username,
                'session_id': session_record.id
            }
            
            token = self.serializer.dumps(session_data).decode('utf-8')
            
            log_system_event("INFO", f"User {user.username} logged in", "auth", user.id)
            return token
    
    def verify_session(self, token: str) -> Optional[User]:
        """Verify a session token and return the user."""
        try:
            session_data = self.serializer.loads(token)
            session_id = session_data.get('session_id')
            user_id = session_data.get('user_id')
            
            if not session_id or not user_id:
                return None
            
            with get_session() as session:
                # Check if session exists and is active
                session_record = session.query(Session).filter(
                    Session.id == session_id,
                    Session.user_id == user_id,
                    Session.is_active == True
                ).first()
                
                if not session_record:
                    return None
                
                # Update last activity
                session_record.last_activity = datetime.utcnow()
                session.commit()
                
                # Get user
                user = session.query(User).filter(User.id == user_id).first()
                return user
                
        except Exception as e:
            print(f"Session verification failed: {e}")
            return None
    
    def invalidate_session(self, token: str) -> bool:
        """Invalidate a session token."""
        try:
            session_data = self.serializer.loads(token)
            session_id = session_data.get('session_id')
            
            if session_id:
                with get_session() as session:
                    session_record = session.query(Session).filter(
                        Session.id == session_id
                    ).first()
                    
                    if session_record:
                        session_record.is_active = False
                        session.commit()
                        return True
            
            return False
        except Exception:
            return False
    
    def change_password(self, user: User, current_password: str, new_password: str) -> bool:
        """Change a user's password."""
        if not self.verify_password(current_password, user.password_hash):
            return False
        
        with get_session() as session:
            user.password_hash = self.hash_password(new_password)
            session.commit()
            
            log_system_event("INFO", f"User {user.username} changed password", "auth", user.id)
            return True
    
    def setup_totp(self, user: User, password: str) -> Optional[str]:
        """Setup TOTP for a user."""
        if not self.verify_password(password, user.password_hash):
            return None
        
        # Generate new TOTP secret
        totp_secret = pyotp.random_base32()
        
        with get_session() as session:
            user.totp_secret = totp_secret
            user.totp_enabled = False  # Will be enabled after verification
            session.commit()
        
        # Generate QR code URL
        totp = pyotp.TOTP(totp_secret)
        qr_url = totp.provisioning_uri(
            name=user.username,
            issuer_name=config.app_name
        )
        
        return qr_url
    
    def verify_totp(self, user: User, token: str) -> bool:
        """Verify a TOTP token."""
        if not user.totp_secret or not user.totp_enabled:
            return False
        
        totp = pyotp.TOTP(user.totp_secret)
        return totp.verify(token, valid_window=1)
    
    def enable_totp(self, user: User, token: str) -> bool:
        """Enable TOTP after verification."""
        if not self.verify_totp(user, token):
            return False
        
        with get_session() as session:
            user.totp_enabled = True
            session.commit()
        
        log_system_event("INFO", f"User {user.username} enabled TOTP", "auth", user.id)
        return True
    
    def disable_totp(self, user: User, password: str) -> bool:
        """Disable TOTP for a user."""
        if not self.verify_password(password, user.password_hash):
            return False
        
        with get_session() as session:
            user.totp_secret = None
            user.totp_enabled = False
            session.commit()
        
        log_system_event("INFO", f"User {user.username} disabled TOTP", "auth", user.id)
        return True
    
    def cleanup_expired_sessions(self):
        """Clean up expired sessions."""
        with get_session() as session:
            cutoff_time = datetime.utcnow() - timedelta(seconds=config.idle_timeout)
            expired_sessions = session.query(Session).filter(
                Session.last_activity < cutoff_time
            ).all()
            
            for expired_session in expired_sessions:
                expired_session.is_active = False
            
            session.commit()

# Global auth manager instance
auth_manager = AuthManager()

# Convenience functions
def hash_password(password: str) -> str:
    """Hash a password using Argon2id."""
    return auth_manager.hash_password(password)

def verify_password(password: str, password_hash: str) -> bool:
    """Verify a password against its hash."""
    return auth_manager.verify_password(password, password_hash)
