# WiimoteKeyMapApp

**WiimoteKeyMapApp** is a Windows utility that connects a Wii Remote via Bluetooth and maps its buttons to keyboard and mouse inputs. It features continuous automatic scanning, system tray integration, and a modern ImGui-based dashboard.

---

## Key Features

- **Continuous Auto-Scanning**: Automatically and continuously searches for Wii Remotes in the background — no manual triggering required. Just press SYNC and the app picks it up.
- **Reliable Connection Detection**:
  - Stale HID entry detection via a liveness write check before declaring a connection live
  - 500 ms stabilization period — requires continuous HID reports before confirming a connection
  - Fast disconnect detection via I/O error signaling from the read thread (Dolphin-style)
  - 1-second watchdog timeout for silent disconnects
- **Dynamic Key Combinations**: Map buttons to complex keyboard combos (e.g., `Ctrl + Shift + T`)
- **Mouse Button Emulation**: Dedicated dropdown for Left, Right, or Middle mouse clicks
- **System Tray Integration**:
  - Closing or minimizing the window hides it to the system tray
  - Double-click the tray icon to restore the window
  - Right-click tray context menu (Open / Exit)
  - Balloon notifications on connect/disconnect, auto-dismissed after 3 seconds
- **Multi-Profile Management**: Load, export, and delete custom mapping templates stored as `.json` files
- **Auto-Save**: All changes are instantly persisted to `active_config.json`
- **Sleek UI**: Premium dark-mode theme built with Dear ImGui + DirectX 11

---

## How to Use

### 1. Connecting
- The app scans for Wii Remotes **automatically** in the background.
- Press the **SYNC** button on your Wii Remote.
- The status indicator turns **green** (CONNECTED) once a stable connection is confirmed.
- While scanning, the status shows **yellow** (SCANNING...).
- The **"Search & Connect"** button triggers an immediate manual scan if needed.

### 2. Mapping Buttons
- **Keyboard**: Click an "Assignment" button in the table. While it shows `[ Recording... ]`, hold your desired keys and release them all to save.
- **Mouse**: Select a click action from the "Action Mode" dropdown (`M-Left`, `M-Right`, `M-Mid`).
- **Clear**: Click the **"X"** button on the left of any row.

### 3. System Tray
- Closing (`X`) or minimizing the window hides it to the system tray — the app keeps running.
- **Double-click** the tray icon to restore the window.
- **Right-click** the tray icon for Open / Exit options.
- "Minimize to System Tray" behavior can be toggled under **Settings**.

### 4. Profiles
- Type a name in "Save Current As" and click **"Export Profile"** to save the current mappings as a template.
- Select a profile from the "Active Template" dropdown to load it.
- Templates are stored as `.json` files in the `profiles/` directory.

---

## Technical Details

- **Language**: C++20
- **Build System**: CMake (FetchContent for dependencies)
- **GUI Framework**: Dear ImGui (v1.90.4) + DirectX 11
- **Configuration**: JSON via `nlohmann/json`
- **APIs**: Windows Bluetooth HID (`bluetoothapis.h`, `hidsdi.h`, `setupapi.h`), `SendInput` for input emulation

### Connection Architecture

Two dedicated background threads handle connection management independently:

| Thread | Responsibility |
|--------|----------------|
| `ScanTask` | Continuous BT inquiry loop (~2.56 s per cycle via `BluetoothFindFirstDevice` with `issueInquiry`). Removes stale BT pairings once per disconnect session so fresh SYNC presses are visible as unknown devices. |
| `BackgroundTask` | Input routing via `SendInput` + passive HID reconnection polling every 500 ms. Picks up a device as soon as `ScanTask` enables its HID service. |

---

## Project Structure

```
main.cpp              — Application loop, UI rendering, system tray, background threads
Connection.cpp/h      — Bluetooth HID discovery, overlapped read loop, connection lifecycle
InputNormalizer.cpp/h — Parses raw HID reports into structured WiiRemoteState
OutputRouter.cpp/h    — Translates WiiRemoteState into SendInput keyboard/mouse events
ConfigManager.cpp/h   — Profile storage, key mapping persistence, VK↔string conversion
```

---

## Build Instructions

**Requirements**: Windows 10/11, Visual Studio 2022 (C++20), CMake 3.20+

```bash
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

The executable is placed in `build/Release/WiimoteKeyMapApp.exe`. Run it directly — no installer required.

---

## Acknowledgements

- [Dear ImGui](https://github.com/ocornut/imgui) — Immediate-mode UI framework
- [nlohmann/json](https://github.com/nlohmann/json) — JSON library

---

*Designed to make the Wii Remote a reliable input device for modern Windows workflows.*
