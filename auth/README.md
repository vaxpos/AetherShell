## Venom Auth

`auth` is a focused Polkit authentication agent.

What it does:
- registers a `PolkitAgentListener`
- shows a local password dialog
- returns the password to Polkit for authorization

What it no longer does:
- no generic D-Bus authentication service
- no standalone PAM username/password API
- no system-bus policy file exposing an auth endpoint

Current architecture:
- [auth.c](/home/x/Desktop/WayfireDE/venom-auth/auth.c): Polkit agent lifecycle
- [auth_ui.c](/home/x/Desktop/WayfireDE/venom-auth/auth_ui.c): local password dialog on Wayland/GTK

This keeps the project limited to a single responsibility: desktop authentication prompts.
