# WiimoteKeyMapApp

**WiimoteKeyMapApp** is a high-performance, professional utility for Windows that translates Wii Remote (Wiimote) inputs into precise Keyboard and Mouse actions. It features a modern, consolidated dashboard and an advanced mapping engine designed for responsiveness and reliability.

---

## 🚀 Key Features

- **Consolidated Dashboard (620x750)**: A unique, vertically stacked interface that provides a stable, "console-like" utility experience. 
- **Dynamic Key Combinations**: Map single buttons to complex keyboard combinations (e.g., `Ctrl + Shift + T`).
- **Mouse Button Emulation**: Dedicated dropdown for mapping buttons to Left, Right, or Middle mouse clicks.
- **Auto-Save Active Session**: All changes are instantly persisted to `active_config.json`.
- **Multi-Profile Management**: 
  - Load, Save (Export), and Delete custom mapping templates.
  - Separates your **Live Session** from your **Templates** to prevent accidental overrides.
- **Sleek UI**: Built with a premium dark-mode theme utilizing **Dear ImGui** and **DirectX 11**.

---

## 🛠️ How to Use

### 1. Connecting
- Click the **"Search & Connect"** button. 
- Hold the **SYNC** button on your Wii Remote.
- The **Status LED** will turn Green when connected.

### 2. Mapping Buttons
- **Keyboard**: Click an "Assignment" button in the table. While it says "Recording...", hold down your desired keys. Release them all at once to save.
- **Mouse**: Select a click action from the "Action Mode" dropdown.
- **Clear**: Click the small **"X"** on the far-left of any row.

### 3. Profiles
- **Exporting**: Type a name in the "Save Current As" field and click **"Export Profile"** to create a template.
- **Internal Storage**: Templates are stored as `.json` files in the `profiles/` directory for easy sharing.

---

## 💻 Technical details

- **Language**: C++20
- **Build System**: CMake (FetchContent used for dependencies)
- **GUI Framework**: Dear ImGui (v1.90.4)
- **Rendering**: DirectX 11
- **Configuration**: JSON (powered by `nlohmann/json`)
- **API**: Windows Bluetooth HID and `SendInput` for low-level emulation.

---

## 📂 Project Structure

- `main.cpp`: Core application loop and UI logic.
- `ConfigManager.cpp/h`: Manages profile loading, persistent storage, and key conversions.
- `Connection.cpp/h`: Handles Bluetooth HID device discovery and report handling.
- `InputNormalizer.cpp/h`: Parses raw HID reports into structured state data.
- `OutputRouter.cpp/h`: Translates state data into physical Windows input events.

---

## 🏗️ Build Instructions

1. **Requirements**: 
   - Windows 10/11.
   - Visual Studio 2022 (with C++20 support).
   - CMake 3.20+.
2. **Steps**:
   ```bash
   mkdir build
   cd build
   cmake ..
   cmake --build . --config Release
   ```

---

## 🌟 Acknowledgements

- [Dear ImGui](https://github.com/ocornut/imgui) for the incredible UI framework.
- [nlohmann/json](https://github.com/nlohmann/json) for the elegant JSON library.

---
*Created with the focus on enhancing the Wii Remote's utility in modern desktop workflows.*
