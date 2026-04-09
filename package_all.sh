#!/bin/bash

# Configuration
BASE_DIR="/home/x/Work/Aether/AetherShell"
PKG_DIR="${BASE_DIR}/packages"
BUILD_DIR="${BASE_DIR}/pkg_build"

mkdir -p "${PKG_DIR}"
mkdir -p "${BUILD_DIR}"

projects=("auth" "desktop" "AetherDock" "launcher" "osd-notify" "panel")

for proj in "${projects[@]}"; do
    echo "--- Packaging ${proj} ---"
    PROJ_PATH="${BASE_DIR}/${proj}"
    STAGING="${BUILD_DIR}/${proj}_pkg"
    
    # Cleanup staging
    rm -rf "${STAGING}"
    mkdir -p "${STAGING}/DEBIAN"
    mkdir -p "${STAGING}/usr/bin"
    
    # 1. Build
    cd "${PROJ_PATH}"
    if [ -f "Makefile" ]; then
        make clean && make
    elif [ -f "meson.build" ]; then
        rm -rf build
        meson build --prefix=/usr
        ninja -C build
    fi
    
    # 2. Install to staging
    if [ "${proj}" == "auth" ]; then
        cp "${PROJ_PATH}/auth" "${STAGING}/usr/bin/"
        mkdir -p "${STAGING}/usr/lib/systemd/user"
        cp "${PROJ_PATH}/auth.service" "${STAGING}/usr/lib/systemd/user/"
    elif [ "${proj}" == "launcher" ]; then
        cp "${PROJ_PATH}/build/launcher" "${STAGING}/usr/bin/aether-launcher"
        # Install CSS to PKGDATADIR = /usr/share/launcher/style/launcher.css
        mkdir -p "${STAGING}/usr/share/launcher/style"
        cp "${PROJ_PATH}/data/style/launcher.css" "${STAGING}/usr/share/launcher/style/launcher.css"
        mkdir -p "${STAGING}/usr/share/applications"
        cp "${PROJ_PATH}/data/launcher.desktop" "${STAGING}/usr/share/applications/aether-launcher.desktop"
        echo "NoDisplay=true" >> "${STAGING}/usr/share/applications/aether-launcher.desktop"
        # Correct Exec path in desktop file
        sed -i 's/^Exec=.*/Exec=aether-launcher/' "${STAGING}/usr/share/applications/aether-launcher.desktop"
    elif [ "${proj}" == "osd-notify" ]; then
        cp "${PROJ_PATH}/osd-notify" "${STAGING}/usr/bin/"
        mkdir -p "${STAGING}/usr/lib/systemd/user"
        cp "${PROJ_PATH}/osd-notify.service" "${STAGING}/usr/lib/systemd/user/"
    elif [ "${proj}" == "desktop" ]; then
        cp "${PROJ_PATH}/desktop" "${STAGING}/usr/bin/"
        mkdir -p "${STAGING}/usr/share/applications"
        cp "${PROJ_PATH}/desktop.desktop" "${STAGING}/usr/share/applications/aether-desktop.desktop"
        echo "NoDisplay=true" >> "${STAGING}/usr/share/applications/aether-desktop.desktop"
        sed -i 's/^Exec=.*/Exec=desktop/' "${STAGING}/usr/share/applications/aether-desktop.desktop"
    elif [ "${proj}" == "AetherDock" ]; then
        cp "${PROJ_PATH}/AetherDock" "${STAGING}/usr/bin/"
        # Copy theme files next to binary so resolve_resource_path() finds them
        cp "${PROJ_PATH}/style.css"     "${STAGING}/usr/bin/style.css"
        cp "${PROJ_PATH}/launcher.svg"  "${STAGING}/usr/bin/launcher.svg"
    elif [ "${proj}" == "panel" ]; then
        cp "${PROJ_PATH}/panel" "${STAGING}/usr/bin/"
        # Copy resources next to binary so panel_resource_path_in() finds them
        mkdir -p "${STAGING}/usr/bin/resources/images"
        cp "${PROJ_PATH}/style.css"        "${STAGING}/usr/bin/resources/style.css"
        cp "${PROJ_PATH}/images/"*         "${STAGING}/usr/bin/resources/images/"
    fi
    
    # 3. Create control file
    cat <<EOF > "${STAGING}/DEBIAN/control"
Package: aether-${proj}
Version: 0.1.0
Section: utils
Priority: optional
Architecture: amd64
Maintainer: Aether Developer <dev@aether.org>
Description: AetherShell component - ${proj}
EOF

    # 4. Build DEB
    dpkg-deb --build "${STAGING}" "${PKG_DIR}/aether-${proj}.deb"
done

echo "--- Finished packaging all projects ---"
ls -lh "${PKG_DIR}"
