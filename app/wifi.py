"""Wi-Fi management module."""

from typing import List, Dict, Tuple
from app.wifi_helper import wifi_helper
from app.schemas import WiFiNetwork, WiFiStatus, WiFiConnectRequest
from app.db import log_system_event

class WiFiManager:
    """Manages Wi-Fi operations."""
    
    def __init__(self):
        self.helper = wifi_helper
    
    def scan_networks(self) -> List[WiFiNetwork]:
        """Scan for available Wi-Fi networks."""
        try:
            networks = self.helper.scan_networks()
            log_system_event("INFO", f"Scanned for Wi-Fi networks, found {len(networks)}", "wifi")
            return networks
        except Exception as e:
            log_system_event("ERROR", f"Wi-Fi scan failed: {str(e)}", "wifi")
            return []
    
    def get_status(self) -> WiFiStatus:
        """Get current Wi-Fi status."""
        try:
            return self.helper.get_status()
        except Exception as e:
            log_system_event("ERROR", f"Failed to get Wi-Fi status: {str(e)}", "wifi")
            return WiFiStatus(interface="wlan0", status="error")
    
    def connect(self, request: WiFiConnectRequest) -> Tuple[bool, str]:
        """Connect to a Wi-Fi network."""
        try:
            success, message = self.helper.connect_to_network(
                request.ssid,
                request.password,
                request.security
            )
            
            if success:
                log_system_event("INFO", f"Successfully connected to {request.ssid}", "wifi")
            else:
                log_system_event("WARNING", f"Failed to connect to {request.ssid}: {message}", "wifi")
            
            return success, message
        except Exception as e:
            error_msg = f"Connection error: {str(e)}"
            log_system_event("ERROR", error_msg, "wifi")
            return False, error_msg
    
    def disconnect(self) -> Tuple[bool, str]:
        """Disconnect from current network."""
        try:
            success, message = self.helper.disconnect()
            return success, message
        except Exception as e:
            error_msg = f"Disconnect error: {str(e)}"
            log_system_event("ERROR", error_msg, "wifi")
            return False, error_msg
    
    def forget_network(self, ssid: str) -> Tuple[bool, str]:
        """Forget a saved network."""
        try:
            success, message = self.helper.forget_network(ssid)
            return success, message
        except Exception as e:
            error_msg = f"Forget network error: {str(e)}"
            log_system_event("ERROR", error_msg, "wifi")
            return False, error_msg
    
    def get_saved_networks(self) -> List[Dict[str, str]]:
        """Get list of saved networks."""
        try:
            return self.helper.get_saved_networks()
        except Exception as e:
            log_system_event("ERROR", f"Failed to get saved networks: {str(e)}", "wifi")
            return []

# Global Wi-Fi manager instance
wifi_manager = WiFiManager()
