#!/bin/bash
# Generate self-signed TLS certificate for VaultUSB

set -e

echo "Generating self-signed TLS certificate..."

# Create certificates directory
mkdir -p /opt/vaultusb/certs
cd /opt/vaultusb/certs

# Generate private key
openssl genrsa -out vaultusb.key 2048

# Generate certificate signing request
openssl req -new -key vaultusb.key -out vaultusb.csr -subj "/C=US/ST=State/L=City/O=VaultUSB/OU=IT/CN=vaultusb.local"

# Generate self-signed certificate
openssl x509 -req -days 365 -in vaultusb.csr -signkey vaultusb.key -out vaultusb.crt

# Set permissions
chmod 600 vaultusb.key
chmod 644 vaultusb.crt

# Copy to application directory
cp vaultusb.crt /opt/vaultusb/cert.pem
cp vaultusb.key /opt/vaultusb/key.pem

# Set ownership
sudo chown vaultusb:vaultusb /opt/vaultusb/cert.pem /opt/vaultusb/key.pem

# Clean up
rm vaultusb.csr

echo "TLS certificate generated successfully!"
echo "Certificate: /opt/vaultusb/cert.pem"
echo "Private key: /opt/vaultusb/key.pem"
echo ""
echo "To enable TLS, update config.toml:"
echo "  [tls]"
echo "  enabled = true"
echo "  cert_file = \"/opt/vaultusb/cert.pem\""
echo "  key_file = \"/opt/vaultusb/key.pem\""
