#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include <algorithm>
#include <atomic>
#include <d3d11.h>
#include <iostream>
#include <string>
#include <tchar.h>
#include <thread>
#include <vector>

#include "ConfigManager.h"
#include "Connection.h"
#include "OutputRouter.h"

// Data
static ID3D11Device *g_pd3dDevice = nullptr;
static ID3D11DeviceContext *g_pd3dDeviceContext = nullptr;
static IDXGISwapChain *g_pSwapChain = nullptr;
static UINT g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView *g_mainRenderTargetView = nullptr;

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// App State
std::atomic<bool> g_appRunning{true};
std::atomic<bool> g_isSearching{false};
Connection g_wiimote;
OutputRouter g_router;

void BackgroundTask() {
  // Background routing loop
  while (g_appRunning) {
    if (!g_isSearching) {
      if (g_wiimote.IsConnected()) {
        const WiiRemoteState &state = g_wiimote.GetState();
        g_router.Route(state);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      } else {
        g_router.Reset();
        g_wiimote.PassiveConnect();
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
      }
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                    PWSTR pCmdLine, int nCmdShow) {
  // ConfigManager Load (Loads the last active session)
  ConfigManager::GetInstance().LoadActiveConfig();

  // Spawn router thread which also handles background passive device mounting
  std::thread bgThread(BackgroundTask);

  // Automated Startup Flow:
  // 1. Check existing HID list (passive)
  // 2. If not found, start a full Bluetooth scan (active)
  std::thread([]() {
    if (!g_wiimote.PassiveConnect()) {
      g_isSearching.store(true);
      g_wiimote.ActiveSearchConnect();
      g_isSearching.store(false);
    }
  }).detach();

  // Create application window
  WNDCLASSEXW wc = {sizeof(wc), CS_CLASSDC, WndProc,       0L,
                    0L,         hInstance,  nullptr,       nullptr,
                    nullptr,    nullptr,    L"WiimoteKeyMap UI", nullptr};
  ::RegisterClassExW(&wc);
  HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"WiimoteKeyMapApp",
                              WS_OVERLAPPEDWINDOW, 100, 100, 620, 750, nullptr,
                              nullptr, wc.hInstance, nullptr);

  if (!CreateDeviceD3D(hwnd)) {
    CleanupDeviceD3D();
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 1;
  }

  ::ShowWindow(hwnd, SW_SHOWDEFAULT);
  ::UpdateWindow(hwnd);

  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  (void)io;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  // Setup Dear ImGui style (Premium Dark Mode)
  ImGui::StyleColorsDark();
  ImGuiStyle &style = ImGui::GetStyle();
  style.WindowRounding = 8.0f;
  style.FrameRounding = 5.0f;
  style.GrabRounding = 5.0f;
  style.PopupRounding = 5.0f;
  style.WindowBorderSize = 0.0f;
  style.ItemSpacing = ImVec2(10, 8);
  style.FramePadding = ImVec2(10, 6);

  style.Colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.06f, 0.06f, 1.0f);
  style.Colors[ImGuiCol_Header] = ImVec4(0.20f, 0.25f, 0.35f, 1.0f);
  style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.25f, 0.35f, 0.50f, 1.0f);
  style.Colors[ImGuiCol_Button] = ImVec4(0.12f, 0.25f, 0.45f, 1.0f);
  style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.18f, 0.35f, 0.65f, 1.0f);
  style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.10f, 0.20f, 0.40f, 1.0f);
  style.Colors[ImGuiCol_Separator] = ImVec4(0.20f, 0.20f, 0.20f, 1.0f);

  // Setup Platform/Renderer backends
  ImGui_ImplWin32_Init(hwnd);
  ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

  // Main loop
  bool done = false;
  while (!done) {
    MSG msg;
    while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
      ::TranslateMessage(&msg);
      ::DispatchMessage(&msg);
      if (msg.message == WM_QUIT)
        done = true;
    }
    if (done)
      break;

    if (g_ResizeWidth != 0 && g_ResizeHeight != 0) {
      CleanupRenderTarget();
      g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight,
                                  DXGI_FORMAT_UNKNOWN, 0);
      g_ResizeWidth = g_ResizeHeight = 0;
      CreateRenderTarget();
    }

    // Start the Dear ImGui frame
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // 1. Connection Panel
    ImGui::SetNextWindowPos(ImVec2(10, 10));
    ImGui::SetNextWindowSize(ImVec2(580, 140));
    ImGui::Begin("Device Connection", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove);

    ImGui::TextDisabled("Version: %s", APP_VERSION);
    ImGui::Separator();

    bool isConnected = g_wiimote.IsConnected();
    ImVec4 statusColor = isConnected
                             ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f)
                             : (g_isSearching ? ImVec4(1.0f, 0.8f, 0.0f, 1.0f)
                                              : ImVec4(1.0f, 0.2f, 0.2f, 1.0f));

    // Status LED
    ImGui::TextUnformatted("Status:");
    ImGui::SameLine();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddCircleFilled(
        ImVec2(p.x + 8, p.y + 8), 6.0f,
        ImGui::ColorConvertFloat4ToU32(statusColor));
    ImGui::Dummy(ImVec2(20, 16));
    ImGui::SameLine();

    if (g_isSearching) {
      ImGui::TextColored(statusColor, "SCANNING... (Hold SYNC on Wiimote)");
    } else if (isConnected) {
      ImGui::TextColored(statusColor, "CONNECTED");
    } else {
      ImGui::TextColored(statusColor, "DISCONNECTED");
    }

    ImGui::Separator();

    if (ImGui::Button("Search & Connect", ImVec2(-1, 30))) {
      if (!isConnected && !g_isSearching) {
        // Detached thread for non-blocking UI
        std::thread([]() {
          g_isSearching.store(true);
          g_wiimote.ActiveSearchConnect();
          g_isSearching.store(false);
        }).detach();
      }
    }
    ImGui::End();

    // 2. Mapping Panel
    ImGui::SetNextWindowPos(ImVec2(10, 160));
    ImGui::SetNextWindowSize(ImVec2(580, 570));
    ImGui::Begin("Keyboard Mappings", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove);

    ImGui::SeparatorText("Profile Management");

    // Profile management toolbar
    static char newProfileName[64] = "";
    auto profiles = ConfigManager::GetInstance().ListProfiles();
    std::string currentProfile =
        ConfigManager::GetInstance().GetCurrentProfileName();

    int currentIdx = 0;
    for (int i = 0; i < (int)profiles.size(); ++i) {
      if (profiles[i] == currentProfile) {
        currentIdx = i;
        break;
      }
    }

    // Line 1: Active Template Selection & Delete
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Active Template:");
    ImGui::SameLine();
    ImGui::PushItemWidth(180);
    if (ImGui::BeginCombo("##profiles", profiles[currentIdx].c_str())) {
      for (int n = 0; n < (int)profiles.size(); n++) {
        const bool is_selected = (currentIdx == n);
        if (ImGui::Selectable(profiles[n].c_str(), is_selected)) {
          ConfigManager::GetInstance().LoadProfile(profiles[n]);
        }
        if (is_selected)
          ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }
    ImGui::PopItemWidth();

    ImGui::SameLine();
    if (ImGui::Button("Delete Profile") && currentProfile != "default") {
      ConfigManager::GetInstance().DeleteProfile(currentProfile);
      ConfigManager::GetInstance().LoadProfile("default");
    }
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Delete the currently selected profile template");

    // Line 2: New Template Creation
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Save Current As:");
    ImGui::SameLine();
    ImGui::PushItemWidth(180);
    ImGui::InputText("##newname", newProfileName, IM_ARRAYSIZE(newProfileName));
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("Export Profile")) {
      if (strlen(newProfileName) > 0) {
        ConfigManager::GetInstance().SaveProfile(newProfileName);
        newProfileName[0] = '\0';
      }
    }
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip(
          "Export current mapping session to a new profile template");

    ImGui::SeparatorText("Button Mappings");

    static std::string recordingButton = "";
    static std::vector<WORD> recordedKeys;
    static std::chrono::steady_clock::time_point recordingStartTime;
    static bool wasAnyKeyDown = false;
    static bool isReleaseBufferActive = false;
    static std::vector<WORD> bufferedKeys;
    static std::chrono::steady_clock::time_point releaseStartTime;

    if (ImGui::BeginTable("MappingTable", 4,
                          ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_BordersInnerV)) {
      ImGui::TableSetupColumn("C", ImGuiTableColumnFlags_WidthFixed, 30.0f);
      ImGui::TableSetupColumn("Wii Button", ImGuiTableColumnFlags_WidthFixed,
                              100.0f);
      ImGui::TableSetupColumn("Assignment (Keyboard Combinations)",
                              ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn("Action Mode", ImGuiTableColumnFlags_WidthFixed,
                              100.0f);
      ImGui::TableHeadersRow();

      auto &mappings = ConfigManager::GetInstance().GetMappings();
      static std::vector<std::string> buttonOrder = {
          "UP",   "DOWN",  "LEFT", "RIGHT", "A",  "B",
          "PLUS", "MINUS", "HOME", "ONE",   "TWO"};

      for (const std::string &button : buttonOrder) {
        auto it = mappings.find(button);
        if (it == mappings.end())
          continue;
        auto &vtKeys = it->second;

        ImGui::TableNextRow(ImGuiTableRowFlags_None, 28.0f);
        ImGui::PushID(button.c_str());

        // Column 0: Clear
        ImGui::TableSetColumnIndex(0);
        if (ImGui::Button("X", ImVec2(24, 22))) {
          ConfigManager::GetInstance().SetMappedKeys(button, {});
        }
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Clear mapping");

        // Column 1: Label
        ImGui::TableSetColumnIndex(1);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(button.c_str());

        // Column 2: Mapping
        ImGui::TableSetColumnIndex(2);
        std::string mappingText = "";
        bool isThisRecording = (recordingButton == button);
        if (isThisRecording) {
          mappingText = "[ Recording... ]";
          ImGui::PushStyleColor(ImGuiCol_Button,
                                ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
        } else {
          for (size_t i = 0; i < vtKeys.size(); ++i) {
            if (i > 0)
              mappingText += " + ";
            mappingText += ConfigManager::GetInstance().VKToString(vtKeys[i]);
          }
          if (mappingText.empty())
            mappingText = "(None)";
        }

        if (ImGui::Button(mappingText.c_str(), ImVec2(-1, 0))) {
          if (isThisRecording) {
            recordingButton = "";
          } else {
            recordingButton = button;
            recordedKeys.clear();
            wasAnyKeyDown = false;
            isReleaseBufferActive = false;
            recordingStartTime = std::chrono::steady_clock::now();
          }
        }
        if (isThisRecording)
          ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip(
              "Click to start/stop recording. Mouse click cancels.");

        // Column 3: Mode
        ImGui::TableSetColumnIndex(3);
        const char *mouseActions[] = {"Kbd", "M-Left", "M-Right", "M-Mid"};
        int cmdIdx = 0;
        if (!vtKeys.empty()) {
          if (vtKeys[0] == VK_LBUTTON)
            cmdIdx = 1;
          else if (vtKeys[0] == VK_RBUTTON)
            cmdIdx = 2;
          else if (vtKeys[0] == VK_MBUTTON)
            cmdIdx = 3;
        }

        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##Mouse", &cmdIdx, mouseActions,
                         IM_ARRAYSIZE(mouseActions))) {
          if (cmdIdx == 0)
            ConfigManager::GetInstance().SetMappedKeys(button, {});
          else {
            WORD mouseVK = (cmdIdx == 1)
                               ? VK_LBUTTON
                               : (cmdIdx == 2 ? VK_RBUTTON : VK_MBUTTON);
            ConfigManager::GetInstance().SetMappedKeys(button, {mouseVK});
          }
        }

        ImGui::PopID();
      }
      ImGui::EndTable();
    }

    // Global key recording logic (Native Win32 Listener)
    if (!recordingButton.empty()) {
      static bool wasAnyKeyDown = false;
      static std::vector<WORD> bufferedKeys;
      static std::chrono::steady_clock::time_point releaseStartTime;
      static bool isReleaseBufferActive = false;

      std::vector<WORD> currentlyHeld;
      for (int i = 1; i < 256; ++i) {
        // Exclude mouse buttons
        if (i == VK_LBUTTON || i == VK_RBUTTON || i == VK_MBUTTON ||
            i == VK_XBUTTON1 || i == VK_XBUTTON2)
          continue;

        if (GetKeyState(i) & 0x8000) {
          if (i == VK_SHIFT && ((GetKeyState(VK_LSHIFT) & 0x8000) ||
                                (GetKeyState(VK_RSHIFT) & 0x8000)))
            continue;
          if (i == VK_CONTROL && ((GetKeyState(VK_LCONTROL) & 0x8000) ||
                                  (GetKeyState(VK_RCONTROL) & 0x8000)))
            continue;
          if (i == VK_MENU && ((GetKeyState(VK_LMENU) & 0x8000) ||
                               (GetKeyState(VK_RMENU) & 0x8000)))
            continue;
          currentlyHeld.push_back((WORD)i);
        }
      }

      auto now = std::chrono::steady_clock::now();
      bool isAnyKeyDownNow = !currentlyHeld.empty();

      // Cancel on Mouse Click during Keyboard Recording
      bool cancelledByMouse = false;
      // Only cancel if clicked OUTSIDE any ImGui window or specific mapping
      // buttons Here, we'll simplify: if clicking, check if we were clicking
      // the button we just started/stopped
      if (ImGui::IsMouseClicked(0) || ImGui::IsMouseClicked(1)) {
        // Ensure we don't cancel if we just clicked the recording button to
        // start/stop
        if (!wasAnyKeyDown &&
            (std::chrono::duration_cast<std::chrono::milliseconds>(
                 now - recordingStartTime)
                 .count() > 300)) {
          recordingButton = "";
          recordedKeys.clear();
          wasAnyKeyDown = false;
          isReleaseBufferActive = false;
          cancelledByMouse = true;
        }
      }

      if (isAnyKeyDownNow) {
        wasAnyKeyDown = true;
        if (currentlyHeld.size() < recordedKeys.size()) {
          // A key was released. Start buffer to see if it's a staggered combo
          // release.
          if (!isReleaseBufferActive) {
            isReleaseBufferActive = true;
            releaseStartTime = now;
            bufferedKeys = recordedKeys;
          } else if (std::chrono::duration_cast<std::chrono::milliseconds>(
                         now - releaseStartTime)
                         .count() > 150) {
            // Buffer expired, this is an intentional release to a smaller set
            recordedKeys = currentlyHeld;
            isReleaseBufferActive = false;
          }
        } else {
          // Keys were added or remained the same size - reset buffer
          recordedKeys = currentlyHeld;
          isReleaseBufferActive = false;
        }
      } else if (wasAnyKeyDown && !cancelledByMouse) {
        // Transition to ALL RELEASED - Save the last state seen
        if (!recordedKeys.empty()) {
          ConfigManager::GetInstance().SetMappedKeys(recordingButton,
                                                     recordedKeys);
          // SaveActiveConfig is called inside SetMappedKeys
        }
        recordingButton = "";
        recordedKeys.clear();
        bufferedKeys.clear();
        wasAnyKeyDown = false;
        isReleaseBufferActive = false;
      }
    }

    ImGui::Separator();
    if (ImGui::Button("Quick Save (Current Session)", ImVec2(-1, 30))) {
      ConfigManager::GetInstance().SaveActiveConfig();
    }
    ImGui::End();

    // Rendering
    ImGui::Render();
    const float clear_color_with_alpha[4] = {0.05f, 0.05f, 0.05f, 1.00f};
    g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView,
                                            nullptr);
    g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView,
                                               clear_color_with_alpha);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    g_pSwapChain->Present(1, 0); // Present with vsync
  }

  // Cleanup
  g_appRunning = false;
  bgThread.join();

  ImGui_ImplDX11_Shutdown();
  ImGui_ImplWin32_Shutdown();
  ImGui::DestroyContext();

  CleanupDeviceD3D();
  ::DestroyWindow(hwnd);
  ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

  return 0;
}

// DX11 Boilerplate
bool CreateDeviceD3D(HWND hWnd) {
  DXGI_SWAP_CHAIN_DESC sd;
  ZeroMemory(&sd, sizeof(sd));
  sd.BufferCount = 2;
  sd.BufferDesc.Width = 0;
  sd.BufferDesc.Height = 0;
  sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.BufferDesc.RefreshRate.Numerator = 60;
  sd.BufferDesc.RefreshRate.Denominator = 1;
  sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.OutputWindow = hWnd;
  sd.SampleDesc.Count = 1;
  sd.SampleDesc.Quality = 0;
  sd.Windowed = TRUE;
  sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

  UINT createDeviceFlags = 0;
  D3D_FEATURE_LEVEL featureLevel;
  const D3D_FEATURE_LEVEL featureLevelArray[2] = {
      D3D_FEATURE_LEVEL_11_0,
      D3D_FEATURE_LEVEL_10_0,
  };
  HRESULT res = D3D11CreateDeviceAndSwapChain(
      nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags,
      featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain,
      &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
  if (res == DXGI_ERROR_UNSUPPORTED)
    res = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags,
        featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain,
        &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
  if (res != S_OK)
    return false;

  CreateRenderTarget();
  return true;
}

void CleanupDeviceD3D() {
  CleanupRenderTarget();
  if (g_pSwapChain) {
    g_pSwapChain->Release();
    g_pSwapChain = nullptr;
  }
  if (g_pd3dDeviceContext) {
    g_pd3dDeviceContext->Release();
    g_pd3dDeviceContext = nullptr;
  }
  if (g_pd3dDevice) {
    g_pd3dDevice->Release();
    g_pd3dDevice = nullptr;
  }
}

void CreateRenderTarget() {
  ID3D11Texture2D *pBackBuffer;
  g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
  g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr,
                                       &g_mainRenderTargetView);
  pBackBuffer->Release();
}

void CleanupRenderTarget() {
  if (g_mainRenderTargetView) {
    g_mainRenderTargetView->Release();
    g_mainRenderTargetView = nullptr;
  }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd,
                                                             UINT msg,
                                                             WPARAM wParam,
                                                             LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
    return true;

  switch (msg) {
  case WM_SIZE:
    if (wParam != SIZE_MINIMIZED) {
      g_ResizeWidth = (UINT)LOWORD(lParam);
      g_ResizeHeight = (UINT)HIWORD(lParam);
    }
    return 0;
  case WM_SYSCOMMAND:
    if ((wParam & 0xfff0) == SC_KEYMENU)
      return 0; // Disable ALT app menu
    break;
  case WM_DESTROY:
    ::PostQuitMessage(0);
    return 0;
  }
  return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
