"""Cryptographic functions for VaultUSB."""

import os
import secrets
from pathlib import Path
from typing import Tuple, Optional
import base64
from cryptography.hazmat.primitives.kdf.hkdf import HKDF
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305
from argon2 import PasswordHasher
from argon2.exceptions import VerifyMismatch
import json
from app.config import config

class CryptoManager:
    """Manages encryption/decryption for the vault."""
    
    def __init__(self):
        self.master_key_file = Path(config.master_key_file)
        self.vault_dir = Path(config.vault_dir)
        self.argon2_hasher = PasswordHasher(
            time_cost=config.argon2_time_cost,
            memory_cost=config.argon2_memory_cost,
            parallelism=config.argon2_parallelism
        )
        self._master_key: Optional[bytes] = None
        self._is_unlocked = False
    
    def generate_master_key(self) -> bytes:
        """Generate a new random master key."""
        return secrets.token_bytes(32)
    
    def derive_key_from_password(self, password: str, salt: bytes) -> bytes:
        """Derive a key from password using Argon2id."""
        return self.argon2_hasher.hash(password.encode()).encode()
    
    def seal_master_key(self, master_key: bytes, password: str) -> str:
        """Seal the master key with a password."""
        salt = secrets.token_bytes(32)
        derived_key = self.derive_key_from_password(password, salt)
        
        # Use first 32 bytes of derived key for encryption
        key = derived_key[:32]
        cipher = ChaCha20Poly1305(key)
        nonce = secrets.token_bytes(12)
        
        sealed_data = cipher.encrypt(nonce, master_key, None)
        
        sealed_info = {
            "salt": base64.b64encode(salt).decode(),
            "nonce": base64.b64encode(nonce).decode(),
            "sealed_data": base64.b64encode(sealed_data).decode()
        }
        
        return json.dumps(sealed_info)
    
    def unseal_master_key(self, sealed_data: str, password: str) -> bytes:
        """Unseal the master key with a password."""
        try:
            sealed_info = json.loads(sealed_data)
            salt = base64.b64decode(sealed_info["salt"])
            nonce = base64.b64decode(sealed_info["nonce"])
            sealed_key = base64.b64decode(sealed_info["sealed_data"])
            
            derived_key = self.derive_key_from_password(password, salt)
            key = derived_key[:32]
            
            cipher = ChaCha20Poly1305(key)
            master_key = cipher.decrypt(nonce, sealed_key, None)
            
            return master_key
        except Exception as e:
            raise ValueError(f"Failed to unseal master key: {e}")
    
    def load_master_key(self, password: str) -> bool:
        """Load and unseal the master key from file."""
        if not self.master_key_file.exists():
            return False
        
        try:
            with open(self.master_key_file, 'r') as f:
                sealed_data = f.read()
            
            self._master_key = self.unseal_master_key(sealed_data, password)
            self._is_unlocked = True
            return True
        except Exception as e:
            print(f"Failed to load master key: {e}")
            return False
    
    def save_master_key(self, master_key: bytes, password: str) -> bool:
        """Save and seal the master key to file."""
        try:
            sealed_data = self.seal_master_key(master_key, password)
            
            # Ensure directory exists
            self.master_key_file.parent.mkdir(parents=True, exist_ok=True)
            
            with open(self.master_key_file, 'w') as f:
                f.write(sealed_data)
            
            return True
        except Exception as e:
            print(f"Failed to save master key: {e}")
            return False
    
    def derive_file_key(self, file_id: str) -> bytes:
        """Derive a file-specific key from master key."""
        if not self._is_unlocked or not self._master_key:
            raise RuntimeError("Master key not unlocked")
        
        hkdf = HKDF(
            algorithm=hashes.SHA256(),
            length=config.file_key_size,
            salt=b"vaultusb_file_key",
            info=file_id.encode()
        )
        return hkdf.derive(self._master_key)
    
    def encrypt_file(self, file_path: Path, file_id: str) -> bool:
        """Encrypt a file in place."""
        if not self._is_unlocked or not self._master_key:
            raise RuntimeError("Master key not unlocked")
        
        try:
            file_key = self.derive_file_key(file_id)
            cipher = ChaCha20Poly1305(file_key)
            
            # Read original file
            with open(file_path, 'rb') as f:
                plaintext = f.read()
            
            # Encrypt
            nonce = secrets.token_bytes(12)
            ciphertext = cipher.encrypt(nonce, plaintext, None)
            
            # Write encrypted file
            with open(file_path, 'wb') as f:
                f.write(nonce + ciphertext)
            
            return True
        except Exception as e:
            print(f"Failed to encrypt file {file_path}: {e}")
            return False
    
    def decrypt_file(self, file_path: Path, file_id: str) -> bytes:
        """Decrypt a file and return plaintext."""
        if not self._is_unlocked or not self._master_key:
            raise RuntimeError("Master key not unlocked")
        
        try:
            file_key = self.derive_file_key(file_id)
            cipher = ChaCha20Poly1305(file_key)
            
            # Read encrypted file
            with open(file_path, 'rb') as f:
                encrypted_data = f.read()
            
            # Split nonce and ciphertext
            nonce = encrypted_data[:12]
            ciphertext = encrypted_data[12:]
            
            # Decrypt
            plaintext = cipher.decrypt(nonce, ciphertext, None)
            
            return plaintext
        except Exception as e:
            raise ValueError(f"Failed to decrypt file {file_path}: {e}")
    
    def secure_delete(self, file_path: Path) -> bool:
        """Securely delete a file by overwriting it with random data."""
        try:
            if not file_path.exists():
                return True
            
            # Get file size
            file_size = file_path.stat().st_size
            
            # Overwrite with random data multiple times
            with open(file_path, 'r+b') as f:
                for _ in range(3):
                    f.seek(0)
                    f.write(secrets.token_bytes(file_size))
                    f.flush()
                    os.fsync(f.fileno())
            
            # Delete the file
            file_path.unlink()
            return True
        except Exception as e:
            print(f"Failed to securely delete {file_path}: {e}")
            return False
    
    def is_unlocked(self) -> bool:
        """Check if the vault is unlocked."""
        return self._is_unlocked and self._master_key is not None
    
    def lock(self):
        """Lock the vault by clearing the master key from memory."""
        self._master_key = None
        self._is_unlocked = False

# Global crypto manager instance
crypto_manager = CryptoManager()
