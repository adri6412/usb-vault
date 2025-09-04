"""System management module for updates and monitoring."""

import subprocess
import psutil
import os
import json
import re
from typing import List, Dict, Tuple, Optional
from datetime import datetime
from app.schemas import PackageUpdate, SystemStatus, UpdateCheckResponse, UpgradeResponse
from app.db import log_system_event

class SystemManager:
    """Manages system operations and updates."""
    
    def __init__(self):
        self.sudoers_file = "/etc/sudoers.d/vaultusb"
        self.is_dietpi = self._check_dietpi()
        self._setup_sudoers()
    
    def _check_dietpi(self):
        """Check if running on DietPi."""
        return os.path.exists('/boot/dietpi/.dietpi_version')
    
    def _setup_sudoers(self):
        """Setup sudoers file for vaultusb user."""
        if self.is_dietpi:
            sudoers_content = """# VaultUSB sudoers configuration for DietPi
vaultusb ALL=(ALL) NOPASSWD: /usr/bin/apt-get, /usr/bin/rpi-update, /usr/bin/raspi-config, /sbin/reboot, /usr/sbin/iw, /usr/sbin/wpa_cli, /usr/sbin/hostapd, /usr/sbin/dnsmasq, /usr/bin/dietpi-config
"""
        else:
            sudoers_content = """# VaultUSB sudoers configuration
vaultusb ALL=(ALL) NOPASSWD: /usr/bin/apt-get, /usr/bin/rpi-update, /usr/bin/raspi-config, /sbin/reboot
"""
        try:
            with open(self.sudoers_file, 'w') as f:
                f.write(sudoers_content)
            os.chmod(self.sudoers_file, 0o440)
            log_system_event("INFO", "Sudoers file configured", "system")
        except Exception as e:
            log_system_event("ERROR", f"Failed to setup sudoers: {str(e)}", "system")
    
    def get_system_status(self) -> SystemStatus:
        """Get current system status."""
        try:
            # Get uptime
            uptime_seconds = int(psutil.boot_time())
            current_time = int(datetime.now().timestamp())
            uptime = current_time - uptime_seconds
            
            # Get memory usage
            memory = psutil.virtual_memory()
            memory_usage = memory.percent
            
            # Get disk usage
            disk = psutil.disk_usage('/')
            disk_usage = (disk.used / disk.total) * 100
            
            # Get CPU usage
            cpu_usage = psutil.cpu_percent(interval=1)
            
            # Check if reboot is required
            reboot_required = os.path.exists('/var/run/reboot-required')
            
            return SystemStatus(
                uptime=uptime,
                memory_usage=round(memory_usage, 2),
                disk_usage=round(disk_usage, 2),
                cpu_usage=round(cpu_usage, 2),
                reboot_required=reboot_required
            )
        except Exception as e:
            log_system_event("ERROR", f"Failed to get system status: {str(e)}", "system")
            return SystemStatus(
                uptime=0,
                memory_usage=0.0,
                disk_usage=0.0,
                cpu_usage=0.0,
                reboot_required=False
            )
    
    def check_updates(self) -> UpdateCheckResponse:
        """Check for available system updates."""
        try:
            # Update package lists
            result = subprocess.run(
                ["sudo", "apt-get", "update"],
                capture_output=True,
                text=True,
                timeout=300
            )
            
            if result.returncode != 0:
                log_system_event("ERROR", f"apt-get update failed: {result.stderr}", "system")
                return UpdateCheckResponse(
                    updates_available=False,
                    packages=[],
                    total_packages=0
                )
            
            # Check for upgradable packages
            result = subprocess.run(
                ["apt-get", "--just-print", "dist-upgrade"],
                capture_output=True,
                text=True,
                timeout=300
            )
            
            packages = []
            if result.returncode == 0:
                packages = self._parse_upgrade_output(result.stdout)
            
            log_system_event("INFO", f"Checked for updates, found {len(packages)} packages", "system")
            
            return UpdateCheckResponse(
                updates_available=len(packages) > 0,
                packages=packages,
                total_packages=len(packages)
            )
            
        except subprocess.TimeoutExpired:
            log_system_event("ERROR", "Update check timed out", "system")
            return UpdateCheckResponse(
                updates_available=False,
                packages=[],
                total_packages=0
            )
        except Exception as e:
            log_system_event("ERROR", f"Update check failed: {str(e)}", "system")
            return UpdateCheckResponse(
                updates_available=False,
                packages=[],
                total_packages=0
            )
    
    def _parse_upgrade_output(self, output: str) -> List[PackageUpdate]:
        """Parse apt-get upgrade output to extract package information."""
        packages = []
        lines = output.split('\n')
        
        for line in lines:
            # Look for lines like: "Inst package [version] (version)"
            if 'Inst ' in line and '[' in line and ']' in line:
                try:
                    # Extract package name
                    package_match = re.search(r'Inst (\S+)', line)
                    if not package_match:
                        continue
                    
                    package_name = package_match.group(1)
                    
                    # Extract current and available versions
                    version_match = re.search(r'\[(\S+)\] \((\S+)\)', line)
                    if not version_match:
                        continue
                    
                    current_version = version_match.group(1)
                    available_version = version_match.group(2)
                    
                    # Extract priority (if available)
                    priority = "normal"
                    if 'security' in line.lower():
                        priority = "security"
                    elif 'important' in line.lower():
                        priority = "important"
                    
                    packages.append(PackageUpdate(
                        package=package_name,
                        current_version=current_version,
                        available_version=available_version,
                        priority=priority
                    ))
                except Exception:
                    continue
        
        return packages
    
    def upgrade_system(self) -> UpgradeResponse:
        """Perform system upgrade."""
        try:
            log_system_event("INFO", "Starting system upgrade", "system")
            
            # Update package lists
            result = subprocess.run(
                ["sudo", "apt-get", "update"],
                capture_output=True,
                text=True,
                timeout=600
            )
            
            if result.returncode != 0:
                return UpgradeResponse(
                    success=False,
                    message=f"Failed to update package lists: {result.stderr}",
                    log=result.stderr
                )
            
            # Perform full upgrade
            result = subprocess.run(
                ["sudo", "apt-get", "-y", "full-upgrade"],
                capture_output=True,
                text=True,
                timeout=1800
            )
            
            upgrade_log = result.stdout + "\n" + result.stderr
            
            if result.returncode != 0:
                return UpgradeResponse(
                    success=False,
                    message=f"Upgrade failed: {result.stderr}",
                    log=upgrade_log
                )
            
            # Clean up
            subprocess.run(
                ["sudo", "apt-get", "-y", "autoremove", "--purge"],
                capture_output=True,
                text=True,
                timeout=300
            )
            
            subprocess.run(
                ["sudo", "apt-get", "-y", "clean"],
                capture_output=True,
                text=True,
                timeout=300
            )
            
            # Update Raspberry Pi firmware (if enabled)
            if os.path.exists('/usr/bin/rpi-update'):
                log_system_event("INFO", "Updating Raspberry Pi firmware", "system")
                result = subprocess.run(
                    ["sudo", "rpi-update"],
                    capture_output=True,
                    text=True,
                    timeout=1800
                )
                upgrade_log += "\n\nFirmware update:\n" + result.stdout + result.stderr
            
            # Update Raspberry Pi configuration
            if os.path.exists('/usr/bin/raspi-config'):
                log_system_event("INFO", "Updating Raspberry Pi configuration", "system")
                result = subprocess.run(
                    ["sudo", "raspi-config", "nonint", "do_update"],
                    capture_output=True,
                    text=True,
                    timeout=300
                )
                upgrade_log += "\n\nConfig update:\n" + result.stdout + result.stderr
            
            log_system_event("INFO", "System upgrade completed successfully", "system")
            
            return UpgradeResponse(
                success=True,
                message="System upgrade completed successfully",
                log=upgrade_log
            )
            
        except subprocess.TimeoutExpired:
            log_system_event("ERROR", "System upgrade timed out", "system")
            return UpgradeResponse(
                success=False,
                message="System upgrade timed out",
                log="Upgrade process exceeded time limit"
            )
        except Exception as e:
            log_system_event("ERROR", f"System upgrade failed: {str(e)}", "system")
            return UpgradeResponse(
                success=False,
                message=f"Upgrade error: {str(e)}",
                log=str(e)
            )
    
    def reboot_system(self) -> Tuple[bool, str]:
        """Reboot the system."""
        try:
            log_system_event("INFO", "System reboot requested", "system")
            
            # Schedule reboot in 10 seconds
            subprocess.run(
                ["sudo", "shutdown", "-r", "+1"],
                capture_output=True,
                text=True,
                timeout=30
            )
            
            return True, "System will reboot in 1 minute"
            
        except Exception as e:
            log_system_event("ERROR", f"Failed to reboot system: {str(e)}", "system")
            return False, f"Reboot failed: {str(e)}"
    
    def get_system_info(self) -> Dict[str, str]:
        """Get basic system information."""
        try:
            info = {}
            
            # OS information
            with open('/etc/os-release', 'r') as f:
                for line in f:
                    if '=' in line:
                        key, value = line.strip().split('=', 1)
                        info[key] = value.strip('"')
            
            # DietPi specific information
            if self.is_dietpi:
                try:
                    with open('/boot/dietpi/.dietpi_version', 'r') as f:
                        info['dietpi_version'] = f.read().strip()
                except:
                    pass
                
                try:
                    with open('/boot/dietpi/.hw_model', 'r') as f:
                        info['dietpi_hw_model'] = f.read().strip()
                except:
                    pass
            
            # Hardware information
            try:
                with open('/proc/cpuinfo', 'r') as f:
                    for line in f:
                        if line.startswith('Model'):
                            info['model'] = line.split(':', 1)[1].strip()
                            break
            except:
                pass
            
            # Kernel version
            try:
                with open('/proc/version', 'r') as f:
                    info['kernel'] = f.read().strip()
            except:
                pass
            
            return info
            
        except Exception as e:
            log_system_event("ERROR", f"Failed to get system info: {str(e)}", "system")
            return {}

# Global system manager instance
system_manager = SystemManager()
