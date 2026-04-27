#!/bin/bash
# ═══════════════════════════════════════════════════════════════════════════
# 🔍 Venom Basilisk - Installer
# ═══════════════════════════════════════════════════════════════════════════

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DAEMON_NAME="basilisk"
SERVICE_NAME="basilisk.service"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

echo -e "${CYAN}"
echo "╔════════════════════════════════════════════════════════════╗"
echo "║   🔍 Basilisk - Spotlight Launcher Installer         ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo -e "${NC}"

# Check root
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}❌ Please run as root (sudo)${NC}"
    exit 1
fi

# Stop and remove old service
echo -e "${YELLOW}🛑 Stopping and removing old service...${NC}"
REAL_USER="${SUDO_USER:-$USER}"
USER_ID=$(id -u "$REAL_USER")

# Stop service if running
su - "$REAL_USER" -c "export XDG_RUNTIME_DIR=/run/user/$USER_ID; systemctl --user stop $SERVICE_NAME 2>/dev/null" || true
echo -e "  ${GREEN}✓${NC} Service stopped"

# Disable service if enabled
su - "$REAL_USER" -c "export XDG_RUNTIME_DIR=/run/user/$USER_ID; systemctl --user disable $SERVICE_NAME 2>/dev/null" || true
echo -e "  ${GREEN}✓${NC} Service disabled"

# Kill any running processes
pkill -9 $DAEMON_NAME 2>/dev/null || true

# Remove old files
rm -f /usr/bin/$DAEMON_NAME 2>/dev/null || true
rm -f /usr/lib/systemd/user/$SERVICE_NAME 2>/dev/null || true
echo -e "  ${GREEN}✓${NC} Old files removed"

# Install binary
echo -e "${YELLOW}📦 Installing binary...${NC}"
install -Dm755 "$SCRIPT_DIR/$DAEMON_NAME" "/usr/bin/$DAEMON_NAME"
echo -e "  ${GREEN}✓${NC} /usr/bin/$DAEMON_NAME"

# Install service
echo -e "${YELLOW}📄 Installing service file...${NC}"
install -Dm644 "$SCRIPT_DIR/$SERVICE_NAME" "/usr/lib/systemd/user/$SERVICE_NAME"
echo -e "  ${GREEN}✓${NC} /usr/lib/systemd/user/$SERVICE_NAME"

# Reload systemd (session manager handles enable/start)
echo -e "${YELLOW}🔄 Reloading systemd for user: ${REAL_USER}${NC}"
export XDG_RUNTIME_DIR="/run/user/$USER_ID"
su - "$REAL_USER" -c "export XDG_RUNTIME_DIR=/run/user/$USER_ID; systemctl --user daemon-reload"
echo -e "  ${GREEN}✓${NC} Service registered (venom-session will manage activation)"

echo ""
echo -e "${GREEN}✅ Basilisk installed successfully!${NC}"
echo ""
echo -e "   ${CYAN}Status:${NC} systemctl --user status $SERVICE_NAME"
echo ""
echo -e "${CYAN}📋 Usage:${NC}"
echo "   Super+Space  - Toggle launcher (add to shortcuts)"
echo "   (text)       - Search apps"
echo "   vater:cmd    - Run terminal command"
echo "   !:expr       - Calculate math"
echo "   vafile:name  - Search files"
echo "   g:query      - Search GitHub"
echo "   s:query      - Search Google"
echo "   ai:question  - Ask Admiral AI"
echo ""
echo -e "${CYAN}📋 D-Bus Control:${NC}"
echo "   dbus-send --session --dest=org.basilisk.Basilisk /org/basilisk/Basilisk org.basilisk.Basilisk.Toggle"
