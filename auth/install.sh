#!/bin/bash

# ==============================================
# Venom Auth Agent - Installer for Custom Distro
# ==============================================

set -e

# الألوان
GREEN='\033[0;32m'
BLUE='\033[0;34m'
RED='\033[0;31m'
NC='\033[0m'

# المسارات
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BINARY_NAME="auth"
SERVICE_NAME="auth.service"

# تحديد المستخدم (لأننا نشغل السكربت بـ sudo)
REAL_USER=$SUDO_USER
if [ -z "$REAL_USER" ]; then
    echo -e "${RED}❌ Error: Run with sudo!${NC}"
    exit 1
fi
USER_ID=$(id -u "$REAL_USER")

echo -e "${BLUE}╔════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║   🛡️ Venom Auth Installer (Polkit Agent)    ║${NC}"
echo -e "${BLUE}╚════════════════════════════════════════════╝${NC}"
echo ""

if [[ $EUID -ne 0 ]]; then
    echo -e "${RED}❌ Error: This script must be run as root${NC}"
    exit 1
fi

# 1. تثبيت الملف التنفيذي
echo -e "${BLUE}📦 Installing binary to /usr/bin/...${NC}"
if [[ ! -f "${SCRIPT_DIR}/${BINARY_NAME}" ]]; then
    echo -e "${RED}❌ Error: Binary not found! Compile it first.${NC}"
    exit 1
fi
cp "${SCRIPT_DIR}/${BINARY_NAME}" /usr/bin/
chmod +x /usr/bin/${BINARY_NAME}

# 2. تثبيت الخدمة (User Service)
echo -e "${BLUE}📄 Installing service file...${NC}"
mkdir -p /usr/lib/systemd/user/

# إنشاء ملف الخدمة مباشرة هنا لضمان صحة المحتوى
cat <<EOF > /usr/lib/systemd/user/${SERVICE_NAME}
[Unit]
Description=Venom Authentication Agent
Documentation=https://github.com/vaxp/auth
PartOf=graphical-session.target
After=graphical-session.target

[Service]
Type=simple
ExecStart=/usr/bin/${BINARY_NAME}
Restart=always
RestartSec=1

[Install]
WantedBy=graphical-session.target
EOF

# 3. التفعيل والتشغيل
echo -e "${BLUE}🔄 Enabling service for user: ${REAL_USER}...${NC}"

export XDG_RUNTIME_DIR="/run/user/$USER_ID"

su - "$REAL_USER" -c "export XDG_RUNTIME_DIR=/run/user/$USER_ID; systemctl --user daemon-reload"
su - "$REAL_USER" -c "export XDG_RUNTIME_DIR=/run/user/$USER_ID; systemctl --user enable --now ${SERVICE_NAME}"
su - "$REAL_USER" -c "export XDG_RUNTIME_DIR=/run/user/$USER_ID; systemctl --user restart ${SERVICE_NAME}"

echo ""
echo -e "${GREEN}✅ Venom Auth installed successfully!${NC}"
echo "   Polkit Agent:  Registered via Code"
echo "   Service:       Active"
