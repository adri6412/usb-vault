"""Configuration management for VaultUSB."""

import os
import tomli
from pathlib import Path
from typing import Any, Dict

class Config:
    """Configuration class for VaultUSB application."""
    
    def __init__(self, config_file: str = "config.toml"):
        """Initialize configuration from TOML file."""
        self.config_file = config_file
        self._config = self._load_config()
    
    def _load_config(self) -> Dict[str, Any]:
        """Load configuration from TOML file."""
        config_path = Path(self.config_file)
        if not config_path.exists():
            raise FileNotFoundError(f"Configuration file {self.config_file} not found")
        
        with open(config_path, "rb") as f:
            return tomli.load(f)
    
    def get(self, key: str, default: Any = None) -> Any:
        """Get configuration value using dot notation."""
        keys = key.split(".")
        value = self._config
        
        for k in keys:
            if isinstance(value, dict) and k in value:
                value = value[k]
            else:
                return default
        
        return value
    
    @property
    def app_name(self) -> str:
        return self.get("app.name", "VaultUSB")
    
    @property
    def app_version(self) -> str:
        return self.get("app.version", "1.0.0")
    
    @property
    def debug(self) -> bool:
        return self.get("app.debug", False)
    
    @property
    def host(self) -> str:
        return self.get("app.host", "0.0.0.0")
    
    @property
    def port(self) -> int:
        return self.get("app.port", 8000)
    
    @property
    def secret_key(self) -> str:
        return self.get("app.secret_key", "vaultusb-secret-key")
    
    @property
    def usb0_ip(self) -> str:
        return self.get("networking.usb0_ip", "192.168.3.1")
    
    @property
    def usb0_netmask(self) -> str:
        return self.get("networking.usb0_netmask", "24")
    
    @property
    def usb0_dhcp_range(self) -> str:
        return self.get("networking.usb0_dhcp_range", "192.168.3.100,192.168.3.200")
    
    @property
    def uap0_ip(self) -> str:
        return self.get("networking.uap0_ip", "10.42.0.1")
    
    @property
    def uap0_netmask(self) -> str:
        return self.get("networking.uap0_netmask", "24")
    
    @property
    def uap0_dhcp_range(self) -> str:
        return self.get("networking.uap0_dhcp_range", "10.42.0.100,10.42.0.200")
    
    @property
    def ap_ssid(self) -> str:
        return self.get("networking.ap_ssid", "VaultUSB")
    
    @property
    def ap_password(self) -> str:
        return self.get("networking.ap_password", "ChangeMeVault!")
    
    @property
    def idle_timeout(self) -> int:
        return self.get("security.idle_timeout", 600)
    
    @property
    def master_key_file(self) -> str:
        return self.get("security.master_key_file", "/opt/vaultusb/master.key")
    
    @property
    def vault_dir(self) -> str:
        return self.get("security.vault_dir", "/opt/vaultusb/vault")
    
    @property
    def db_file(self) -> str:
        return self.get("security.db_file", "/opt/vaultusb/vault.db")
    
    @property
    def argon2_time_cost(self) -> int:
        return self.get("security.argon2_time_cost", 3)
    
    @property
    def argon2_memory_cost(self) -> int:
        return self.get("security.argon2_memory_cost", 65536)
    
    @property
    def argon2_parallelism(self) -> int:
        return self.get("security.argon2_parallelism", 1)
    
    @property
    def file_key_size(self) -> int:
        return self.get("security.file_key_size", 32)
    
    @property
    def tls_enabled(self) -> bool:
        return self.get("tls.enabled", False)
    
    @property
    def cert_file(self) -> str:
        return self.get("tls.cert_file", "/opt/vaultusb/cert.pem")
    
    @property
    def key_file(self) -> str:
        return self.get("tls.key_file", "/opt/vaultusb/key.pem")
    
    @property
    def sudoers_file(self) -> str:
        return self.get("system.sudoers_file", "/etc/sudoers.d/vaultusb")
    
    @property
    def rpi_update_enabled(self) -> bool:
        return self.get("system.rpi_update_enabled", True)
    
    @property
    def dietpi_optimized(self) -> bool:
        return self.get("system.dietpi_optimized", False)
    
    @property
    def dietpi_version(self) -> str:
        return self.get("dietpi.version", "unknown")
    
    @property
    def python_version(self) -> str:
        return self.get("dietpi.python_version", "3.11")
    
    @property
    def debian_version(self) -> str:
        return self.get("dietpi.debian_version", "bookworm")

# Global config instance
config = Config()
