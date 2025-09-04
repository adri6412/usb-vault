"""File storage and management for VaultUSB."""

import os
import secrets
import uuid
from pathlib import Path
from typing import List, Optional, Tuple
from datetime import datetime
import mimetypes
from app.config import config
from app.db import get_session, log_system_event
from app.models import File, User
from app.crypto import crypto_manager

class StorageManager:
    """Manages encrypted file storage."""
    
    def __init__(self):
        self.vault_dir = Path(config.vault_dir)
        self.vault_dir.mkdir(parents=True, exist_ok=True)
    
    def generate_file_id(self) -> str:
        """Generate a unique file ID."""
        return str(uuid.uuid4())
    
    def generate_encrypted_filename(self) -> str:
        """Generate a random encrypted filename."""
        return secrets.token_urlsafe(32)
    
    def get_mime_type(self, filename: str) -> str:
        """Get MIME type for a file."""
        mime_type, _ = mimetypes.guess_type(filename)
        return mime_type or 'application/octet-stream'
    
    def store_file(self, file_data: bytes, original_name: str, user: User) -> Optional[str]:
        """Store an encrypted file in the vault."""
        if not crypto_manager.is_unlocked():
            raise RuntimeError("Vault is locked")
        
        try:
            # Generate file ID and encrypted filename
            file_id = self.generate_file_id()
            encrypted_filename = self.generate_encrypted_filename()
            encrypted_path = self.vault_dir / encrypted_filename
            
            # Write file to disk
            with open(encrypted_path, 'wb') as f:
                f.write(file_data)
            
            # Encrypt the file
            if not crypto_manager.encrypt_file(encrypted_path, file_id):
                # Clean up on failure
                encrypted_path.unlink()
                return None
            
            # Store file metadata in database
            with get_session() as session:
                file_record = File(
                    id=file_id,
                    original_name=original_name,
                    encrypted_name=encrypted_filename,
                    size=len(file_data),
                    mime_type=self.get_mime_type(original_name),
                    user_id=user.id
                )
                session.add(file_record)
                session.commit()
            
            log_system_event("INFO", f"File '{original_name}' stored by user {user.username}", "storage", user.id)
            return file_id
            
        except Exception as e:
            print(f"Failed to store file: {e}")
            return None
    
    def retrieve_file(self, file_id: str, user: User) -> Optional[bytes]:
        """Retrieve and decrypt a file from the vault."""
        if not crypto_manager.is_unlocked():
            raise RuntimeError("Vault is locked")
        
        try:
            with get_session() as session:
                file_record = session.query(File).filter(
                    File.id == file_id,
                    File.user_id == user.id,
                    File.is_deleted == False
                ).first()
                
                if not file_record:
                    return None
                
                encrypted_path = self.vault_dir / file_record.encrypted_name
                if not encrypted_path.exists():
                    return None
                
                # Decrypt the file
                plaintext = crypto_manager.decrypt_file(encrypted_path, file_id)
                return plaintext
                
        except Exception as e:
            print(f"Failed to retrieve file {file_id}: {e}")
            return None
    
    def delete_file(self, file_id: str, user: User) -> bool:
        """Delete a file from the vault."""
        if not crypto_manager.is_unlocked():
            raise RuntimeError("Vault is locked")
        
        try:
            with get_session() as session:
                file_record = session.query(File).filter(
                    File.id == file_id,
                    File.user_id == user.id,
                    File.is_deleted == False
                ).first()
                
                if not file_record:
                    return False
                
                # Mark as deleted in database
                file_record.is_deleted = True
                file_record.modified_at = datetime.utcnow()
                session.commit()
                
                # Securely delete the encrypted file
                encrypted_path = self.vault_dir / file_record.encrypted_name
                if encrypted_path.exists():
                    crypto_manager.secure_delete(encrypted_path)
                
                log_system_event("INFO", f"File '{file_record.original_name}' deleted by user {user.username}", "storage", user.id)
                return True
                
        except Exception as e:
            print(f"Failed to delete file {file_id}: {e}")
            return False
    
    def list_files(self, user: User, limit: int = 100, offset: int = 0) -> Tuple[List[File], int]:
        """List files for a user."""
        with get_session() as session:
            query = session.query(File).filter(
                File.user_id == user.id,
                File.is_deleted == False
            ).order_by(File.created_at.desc())
            
            total = query.count()
            files = query.offset(offset).limit(limit).all()
            
            return files, total
    
    def get_file_info(self, file_id: str, user: User) -> Optional[File]:
        """Get file information."""
        with get_session() as session:
            return session.query(File).filter(
                File.id == file_id,
                File.user_id == user.id,
                File.is_deleted == False
            ).first()
    
    def search_files(self, query: str, user: User, limit: int = 100) -> List[File]:
        """Search files by name."""
        with get_session() as session:
            return session.query(File).filter(
                File.user_id == user.id,
                File.is_deleted == False,
                File.original_name.contains(query)
            ).order_by(File.created_at.desc()).limit(limit).all()
    
    def get_storage_stats(self, user: User) -> dict:
        """Get storage statistics for a user."""
        with get_session() as session:
            files = session.query(File).filter(
                File.user_id == user.id,
                File.is_deleted == False
            ).all()
            
            total_size = sum(file.size for file in files)
            file_count = len(files)
            
            return {
                "total_size": total_size,
                "file_count": file_count,
                "total_size_mb": round(total_size / (1024 * 1024), 2)
            }
    
    def cleanup_deleted_files(self):
        """Clean up files marked as deleted."""
        with get_session() as session:
            deleted_files = session.query(File).filter(
                File.is_deleted == True
            ).all()
            
            for file_record in deleted_files:
                encrypted_path = self.vault_dir / file_record.encrypted_name
                if encrypted_path.exists():
                    crypto_manager.secure_delete(encrypted_path)
                
                session.delete(file_record)
            
            session.commit()

# Global storage manager instance
storage_manager = StorageManager()
