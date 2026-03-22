# WiimoteKeyMapApp - Version Control & Release Guide

This guide explains how to manage versions, tags, and binary releases for **WiimoteKeyMapApp**.

## 1. Versioning Strategy
We use **Semantic Versioning** (vMajor.Minor.Patch):
- **Major**: Breaking changes or massive UI overhauls.
- **Minor**: New features (e.g., adding IR smoothing).
- **Patch**: Bug fixes and layout tweaks.

The version is defined in `CMakeLists.txt`:
```cmake
project(WiimoteKeyMapApp VERSION 1.0.0 LANGUAGES CXX)
```
Change this value before every official release. It will automatically update the "Version" text in the app's UI.

---

## 2. Git Tagging
When you are ready to "Freeze" a version for release, use a Git Tag:
```bash
# Tag the current commit
git tag -a v1.0.0 -m "First stable release with vertical dashboard"

# Push the tag to GitHub
git push origin v1.0.0
```

---

## 3. Creating a GitHub Release
1. Go to your repository on GitHub.
2. Click on **Releases** -> **Draft a new release**.
3. Select the tag you just pushed (`v1.0.0`).
4. Give it a title (e.g., `v1.0.0 - Initial Release`).
5. Describe the changes (you can copy-paste from `walkthrough.md`).

---

## 4. Packaging the Binary (.exe)
To let users use your app without installing Visual Studio or CMake:
1. Build the project in **Release** mode:
   ```bash
   cd build
   cmake --build . --config Release
   ```
2. Locate the generated executable: `build/Release/WiimoteKeyMapApp.exe`.
3. **Recommended**: Zip the `.exe` and the `profiles/default.json` (if you want them to have it immediately).
4. Upload this `.zip` to the GitHub Release "Assets" section.

---

## 5. Changelog Maintenance
It is good practice to keep a `CHANGELOG.md` or update the **Releases** description with every version so users know exactly what improved.
