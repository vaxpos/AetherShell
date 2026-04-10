# 🐍 Venom Basilisk Project Analysis

Venom Basilisk is a lightweight, Venom Basilisk application launcher for the Venom Desktop Environment. It is written in C using GTK+ 3.0 and GIO/GDBus.

## 📂 Project Structure

The project has been refactored into a clean structure:

-   `src/`: Contains all source code (`.c`) files.
-   `include/`: Contains all header (`.h`) files.
-   `obj/`: Contains compiled object (`.o`) files.
-   `venom_basilisk`: The compiled binary executable.
-   `Makefile`: Build configuration script.

## 🛠️ Build & Installation

To build the project, simply run:

```bash
make
```

To install it to `/usr/bin/venom_basilisk`:

```bash
sudo make install
```

## 📡 D-Bus Interface

The application exposes a D-Bus interface to allow external control (showing, hiding, toggling). This is useful for binding the launcher to a keyboard shortcut (e.g., Super+Space).

-   **Service Name**: `org.venom.Basilisk`
-   **Object Path**: `/org/venom/Basilisk`
-   **Interface**: `org.venom.Basilisk`

### Methods

| Method | Signature | Description |
| :--- | :--- | :--- |
| `Show` | `()` | Shows the launcher window and focuses the search bar. |
| `Hide` | `()` | Hides the launcher window. |
| `Toggle` | `()` | Toggles visibility (Shows if hidden, Hides if visible). |
| `Search` | `(s)` | Shows the launcher and pre-fills the search query. |

### ⌨️ Control Commands

You can use `dbus-send` or `gdbus` to control the launcher from the terminal or your window manager configuration.

#### Using `gdbus` (Recommended)

**Toggle Visibility:**
```bash
gdbus call --session --dest org.venom.Basilisk --object-path /org/venom/Basilisk --method org.venom.Basilisk.Toggle
```

**Show Launcher:**
```bash
gdbus call --session --dest org.venom.Basilisk --object-path /org/venom/Basilisk --method org.venom.Basilisk.Show
```

**Hide Launcher:**
```bash
gdbus call --session --dest org.venom.Basilisk --object-path /org/venom/Basilisk --method org.venom.Basilisk.Hide
```

**Search for "Firefox":**
```bash
gdbus call --session --dest org.venom.Basilisk --object-path /org/venom/Basilisk --method org.venom.Basilisk.Search "Firefox"
```

#### Using `dbus-send`

**Toggle Visibility:**
```bash
dbus-send --session --type=method_call --dest=org.venom.Basilisk /org/venom/Basilisk org.venom.Basilisk.Toggle
```

## 🚀 Usage

Run the daemon in the background (usually effectively by your session manager or systemd):
```bash
./venom_basilisk &
```

Or show it immediately on startup:
```bash
./venom_basilisk --show
```
