"""Wi-Fi helper functions using wpa_cli."""

import subprocess
import re
import time
from typing import List, Dict, Optional, Tuple
from app.schemas import WiFiNetwork, WiFiStatus
from app.db import log_system_event

class WiFiHelper:
    """Helper class for Wi-Fi operations using wpa_cli."""
    
    def __init__(self):
        self.wpa_cli = "/sbin/wpa_cli"
        self.interface = "wlan0"
    
    def _run_wpa_cli(self, command: str, interface: str = None) -> Tuple[bool, str]:
        """Run a wpa_cli command."""
        if interface is None:
            interface = self.interface
        
        try:
            cmd = [self.wpa_cli, "-i", interface] + command.split()
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
            return result.returncode == 0, result.stdout
        except subprocess.TimeoutExpired:
            return False, "Command timed out"
        except Exception as e:
            return False, str(e)
    
    def scan_networks(self) -> List[WiFiNetwork]:
        """Scan for available Wi-Fi networks."""
        success, output = self._run_wpa_cli("scan")
        if not success:
            return []
        
        # Wait for scan to complete
        time.sleep(5)
        
        success, output = self._run_wpa_cli("scan_results")
        if not success:
            return []
        
        networks = []
        lines = output.strip().split('\n')
        
        for line in lines[1:]:  # Skip header
            parts = line.split('\t')
            if len(parts) >= 5:
                bssid = parts[0]
                frequency = int(parts[1]) if parts[1].isdigit() else 0
                signal_level = int(parts[2]) if parts[2].lstrip('-').isdigit() else -100
                flags = parts[3].split(' ')
                ssid = parts[4] if len(parts) > 4 else ""
                
                # Determine security type
                security = "Open"
                if "WPA2" in flags:
                    security = "WPA2"
                elif "WPA" in flags:
                    security = "WPA"
                elif "WEP" in flags:
                    security = "WEP"
                
                # Filter out empty SSIDs
                if ssid:
                    networks.append(WiFiNetwork(
                        ssid=ssid,
                        bssid=bssid,
                        frequency=frequency,
                        signal_level=signal_level,
                        security=security,
                        flags=flags
                    ))
        
        # Remove duplicates and sort by signal strength
        unique_networks = {}
        for network in networks:
            if network.ssid not in unique_networks or network.signal_level > unique_networks[network.ssid].signal_level:
                unique_networks[network.ssid] = network
        
        return sorted(unique_networks.values(), key=lambda x: x.signal_level, reverse=True)
    
    def get_status(self) -> WiFiStatus:
        """Get current Wi-Fi status."""
        success, output = self._run_wpa_cli("status")
        if not success:
            return WiFiStatus(interface=self.interface, status="disconnected")
        
        status_data = {}
        for line in output.strip().split('\n'):
            if '=' in line:
                key, value = line.split('=', 1)
                status_data[key] = value
        
        status = "disconnected"
        ssid = None
        ip_address = None
        signal_level = None
        
        if status_data.get('wpa_state') == 'COMPLETED':
            status = "connected"
            ssid = status_data.get('ssid')
            ip_address = status_data.get('ip_address')
            
            # Get signal level
            success, signal_output = self._run_wpa_cli("signal_poll")
            if success:
                for line in signal_output.strip().split('\n'):
                    if 'RSSI' in line:
                        try:
                            signal_level = int(re.search(r'RSSI=(-?\d+)', line).group(1))
                        except:
                            pass
        
        return WiFiStatus(
            interface=self.interface,
            status=status,
            ssid=ssid,
            ip_address=ip_address,
            signal_level=signal_level
        )
    
    def connect_to_network(self, ssid: str, password: str = None, security: str = "WPA2") -> Tuple[bool, str]:
        """Connect to a Wi-Fi network."""
        # Sanitize inputs
        if len(ssid) > 32:
            return False, "SSID too long"
        
        if password and len(password) < 8:
            return False, "Password too short"
        
        try:
            # Disconnect first
            self._run_wpa_cli("disconnect")
            time.sleep(1)
            
            # Remove all networks
            success, output = self._run_wpa_cli("list_networks")
            if success:
                for line in output.strip().split('\n')[1:]:  # Skip header
                    if line.strip():
                        network_id = line.split()[0]
                        self._run_wpa_cli(f"remove_network {network_id}")
            
            # Add new network
            success, output = self._run_wpa_cli("add_network")
            if not success:
                return False, "Failed to add network"
            
            network_id = output.strip()
            
            # Set SSID
            success, output = self._run_wpa_cli(f'set_network {network_id} ssid \'"{ssid}"\'')
            if not success:
                return False, "Failed to set SSID"
            
            # Set security
            if security == "Open":
                self._run_wpa_cli(f"set_network {network_id} key_mgmt NONE")
            elif security in ["WPA", "WPA2"]:
                self._run_wpa_cli(f"set_network {network_id} key_mgmt WPA-PSK")
                if password:
                    self._run_wpa_cli(f'set_network {network_id} psk \'"{password}"\'')
                else:
                    return False, "Password required for WPA/WPA2"
            elif security == "WEP":
                self._run_wpa_cli(f"set_network {network_id} key_mgmt NONE")
                if password:
                    self._run_wpa_cli(f'set_network {network_id} wep_key0 \'"{password}"\'')
                else:
                    return False, "Password required for WEP"
            
            # Enable network
            success, output = self._run_wpa_cli(f"enable_network {network_id}")
            if not success:
                return False, "Failed to enable network"
            
            # Select network
            success, output = self._run_wpa_cli(f"select_network {network_id}")
            if not success:
                return False, "Failed to select network"
            
            # Save configuration
            self._run_wpa_cli("save_config")
            
            # Wait for connection
            for _ in range(30):  # Wait up to 30 seconds
                time.sleep(1)
                status = self.get_status()
                if status.status == "connected":
                    log_system_event("INFO", f"Connected to Wi-Fi network: {ssid}", "wifi")
                    return True, "Connected successfully"
                elif status.status == "disconnected":
                    # Check if there was an error
                    success, output = self._run_wpa_cli("status")
                    if "FAILED" in output or "DISCONNECTED" in output:
                        return False, "Connection failed"
            
            return False, "Connection timeout"
            
        except Exception as e:
            return False, f"Connection error: {str(e)}"
    
    def disconnect(self) -> Tuple[bool, str]:
        """Disconnect from current network."""
        success, output = self._run_wpa_cli("disconnect")
        if success:
            log_system_event("INFO", "Disconnected from Wi-Fi network", "wifi")
            return True, "Disconnected successfully"
        else:
            return False, "Failed to disconnect"
    
    def forget_network(self, ssid: str) -> Tuple[bool, str]:
        """Forget a saved network."""
        success, output = self._run_wpa_cli("list_networks")
        if not success:
            return False, "Failed to list networks"
        
        network_id = None
        for line in output.strip().split('\n')[1:]:  # Skip header
            if line.strip():
                parts = line.split()
                if len(parts) >= 2 and parts[1] == ssid:
                    network_id = parts[0]
                    break
        
        if network_id is None:
            return False, "Network not found"
        
        success, output = self._run_wpa_cli(f"remove_network {network_id}")
        if success:
            self._run_wpa_cli("save_config")
            log_system_event("INFO", f"Forgot Wi-Fi network: {ssid}", "wifi")
            return True, "Network forgotten"
        else:
            return False, "Failed to forget network"
    
    def get_saved_networks(self) -> List[Dict[str, str]]:
        """Get list of saved networks."""
        success, output = self._run_wpa_cli("list_networks")
        if not success:
            return []
        
        networks = []
        for line in output.strip().split('\n')[1:]:  # Skip header
            if line.strip():
                parts = line.split()
                if len(parts) >= 2:
                    networks.append({
                        "id": parts[0],
                        "ssid": parts[1],
                        "flags": parts[2] if len(parts) > 2 else ""
                    })
        
        return networks

# Global Wi-Fi helper instance
wifi_helper = WiFiHelper()
