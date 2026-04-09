#!/bin/bash

# AetherShell - Master Installer
# This script installs all components of AetherShell from .deb packages.

set -e

# check for sudo
if [[ $EUID -ne 0 ]]; then
   echo "Error: This script must be run as root (use sudo)." 
   exit 1
fi

PKG_DIR="./packages"

if [[ ! -d "$PKG_DIR" ]]; then
    echo "Error: 'packages' directory not found. Please run ./package_all.sh first."
    exit 1
fi

echo "--- Installing AetherShell Components ---"
apt update || true
apt install -y "$PKG_DIR"/*.deb

echo ""
echo "--- Installation Complete ---"
echo "Note: All applications have been hidden from launchers (NoDisplay=true)."
echo "You can start them via aether.ini [autostart] or keybindings."
