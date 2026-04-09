# Venom Launcher

A professional, macOS Launchpad-style application launcher for the Venom Desktop Environment. Built with **C11 + GTK3** using clean architecture and optimized for performance.

## Features

- **Fullscreen overlay** with dark glassmorphism background
- **7-column icon grid** with 96×96px icons — matching macOS Launchpad proportions
- **Async icon loading** — thread pool (4 threads) + LRU cache (256 entries)
- **Debounced search** — filters by name, comment, and categories in real-time
- **Pagination** — dots indicator + prev/next navigation for large app lists
- **Keyboard-first** — any key press focuses search; `Escape` closes launcher
- **Clean architecture** — Core layer is GTK-free and independently testable

## Project Structure

```
launcher/
├── src/
│   ├── main.c
│   ├── core/           # Business logic (no GTK)
│   │   ├── app_entry
│   │   ├── desktop_reader
│   │   └── icon_loader (LRU cache + async)
│   ├── ui/             # GTK3 widgets
│   │   ├── launcher_window
│   │   ├── app_grid (FlowBox + pagination)
│   │   ├── app_icon
│   │   └── search_bar (debounce)
│   └── utils/
│       └── string_utils
├── data/
│   ├── style/launcher.css
│   └── launcher.desktop
└── meson.build
```

## Building

```bash
meson setup build
ninja -C build
```

## Running

```bash
./build/launcher
```

Or after install:

```bash
sudo ninja -C build install
launcher
```

## Dependencies

- GTK+ 3.22+
- GLib 2.x
- pthreads (POSIX)

## Keybindings

| Key      | Action              |
|----------|---------------------|
| `Escape` | Close launcher      |
| Any char | Focus search & type |
