#include "Connection.h"
#include <iostream>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")
#pragma comment(lib, "Bthprops.lib")

Connection::Connection()
    : m_deviceHandle(INVALID_HANDLE_VALUE), m_connected(false),
      m_shouldStop(false) {
  m_lastReportTime = std::chrono::steady_clock::now();
}

Connection::~Connection() { Disconnect(); }

bool Connection::PassiveConnect() {
  std::lock_guard<std::recursive_mutex> lock(m_connectionMutex);
  if (m_connected)
    return true;

  std::wstring devicePath = FindWiimoteDevicePath();
  if (devicePath.empty()) {
    return false;
  }

  m_deviceHandle = CreateFileW(devicePath.c_str(), GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);

  if (m_deviceHandle == INVALID_HANDLE_VALUE) {
    std::cerr << "Failed to open Wii Remote device. Error: " << GetLastError()
              << std::endl;
    return false;
  }

  std::cout << "Successfully connected passively to Wii Remote." << std::endl;
  
  // Reset the report watchdog early to avoid raced timeouts during Sleep
  m_lastReportTime = std::chrono::steady_clock::now();

  // Trigger LED and Haptic Feedback
  SetLEDsAndRumble(true, true);  // LED1 On, Rumble On
  Sleep(150);                    // Wait 150ms
  SetLEDsAndRumble(true, false); // LED1 On, Rumble Off

  UCHAR reportMode[22] = {0};
  reportMode[0] = 0x12; // Report mode config
  reportMode[1] = 0x04; // Continuous reporting bit (Bit 2) enabled
  reportMode[2] = 0x31; // Mode 0x31
  WriteReport(reportMode, sizeof(reportMode));

  // Now declare the connection fully initialized and running
  m_connected = true;
  m_shouldStop = false;

  // Start read thread
  m_readThread = std::thread(&Connection::ReadLoop, this);

  return true;
}

bool Connection::ActiveSearchConnect() {
  std::lock_guard<std::recursive_mutex> lock(m_connectionMutex);
  if (m_connected)
    return true;

  if (AutoConnectBluetooth()) {
    std::cout << "AutoConnect successful, polling for HID device path..."
              << std::endl;
    // Instead of a static 2s sleep, poll for the device path to appear.
    // Windows can take a moment to load the HID driver after pairing.
    for (int i = 0; i < 10; ++i) { // 10 * 500ms = 5 seconds max wait
      if (!FindWiimoteDevicePath().empty()) {
        break;
      }
      Sleep(500);
    }
  }

  return PassiveConnect();
}

bool Connection::WriteReport(UCHAR *buffer, size_t size) {
  if (m_deviceHandle == INVALID_HANDLE_VALUE)
    return false;

  OVERLAPPED overlapped = {0};
  overlapped.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
  if (!overlapped.hEvent)
    return false;

  DWORD bytesWritten = 0;
  BOOL res = WriteFile(m_deviceHandle, buffer, (DWORD)size, &bytesWritten,
                       &overlapped);
  if (!res) {
    if (GetLastError() == ERROR_IO_PENDING) {
      if (WaitForSingleObject(overlapped.hEvent, 1000) == WAIT_OBJECT_0) {
        GetOverlappedResult(m_deviceHandle, &overlapped, &bytesWritten, FALSE);
        res = TRUE;
      } else {
        CancelIo(m_deviceHandle);
      }
    }
  }

  CloseHandle(overlapped.hEvent);
  return res == TRUE;
}

void Connection::SetLEDsAndRumble(bool led1, bool rumble) {
  if (m_deviceHandle == INVALID_HANDLE_VALUE)
    return;

  UCHAR buf[22] = {0}; // Wiimote output reports must be exactly 22 bytes
  buf[0] = 0x11;       // Report ID 0x11: Player LEDs

  UCHAR data = 0x00;
  if (rumble)
    data |= 0x01; // Bit 0: Rumble
  if (led1)
    data |= 0x10; // Bit 4: LED 1

  buf[1] = data;

  WriteReport(buf, sizeof(buf));
}

void Connection::Disconnect() {
  std::lock_guard<std::recursive_mutex> lock(m_connectionMutex);
  bool expected = true;
  if (m_connected.compare_exchange_strong(expected, false)) {
    m_shouldStop = true;

    // Unblock any pending I/O by cancelling it or closing handle
    if (m_deviceHandle != INVALID_HANDLE_VALUE) {
      CancelIo(m_deviceHandle);
    }

    // Only join if we are not the read thread itself
    if (m_readThread.joinable() &&
        std::this_thread::get_id() != m_readThread.get_id()) {
      m_readThread.join();
    }

    if (m_deviceHandle != INVALID_HANDLE_VALUE) {
      CloseHandle(m_deviceHandle);
      m_deviceHandle = INVALID_HANDLE_VALUE;
    }

    std::cout << "Disconnected from Wii Remote." << std::endl;
  }
}

bool Connection::IsConnected() const {
  if (m_connected) {
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(
        now - m_lastReportTime.load());

    // If we haven't received a report in 1 second, assume the device powered
    // off.
    if (duration.count() > 1) {
      // Can't call Disconnect() directly from const method easily without
      // mutable trickery, but returning false alerts the external loops to
      // trigger reconnect. We cast away const specifically to drop the dead
      // connection safely via Disconnect.
      const_cast<Connection *>(this)->Disconnect();
      return false;
    }
  }
  return m_connected;
}

const WiiRemoteState &Connection::GetState() const {
  return m_normalizer.GetState();
}

bool Connection::AutoConnectBluetooth() {
  // 1. Clear any remembered Wii Remotes to ensure a fresh connection state
  BLUETOOTH_DEVICE_SEARCH_PARAMS searchParams = {
      sizeof(BLUETOOTH_DEVICE_SEARCH_PARAMS),
      1,   // returnAuthenticated
      1,   // returnRemembered
      0,   // returnUnknown
      1,   // returnConnected
      0,   // issueInquiry
      2,   // timeoutMultiplier
      NULL // hRadio
  };

  BLUETOOTH_DEVICE_INFO deviceInfo = {sizeof(BLUETOOTH_DEVICE_INFO)};
  HBLUETOOTH_DEVICE_FIND hFind =
      BluetoothFindFirstDevice(&searchParams, &deviceInfo);
  if (hFind) {
    do {
      std::wstring name = deviceInfo.szName;
      if (name.find(L"Nintendo RVL-CNT-01") != std::wstring::npos) {
        std::wcout << L"Removing stale Wii Remote profile: " << name
                   << std::endl;
        BluetoothRemoveDevice(&deviceInfo.Address);
      }
    } while (BluetoothFindNextDevice(hFind, &deviceInfo));
    BluetoothFindDeviceClose(hFind);
  }

  // 2. Search for unknown/new Wii Remotes exactly like the legacy
  // implementation
  searchParams.fReturnAuthenticated = 0;
  searchParams.fReturnRemembered = 0;
  searchParams.fReturnConnected = 0;
  searchParams.fReturnUnknown = 1;
  searchParams.fIssueInquiry = 1;
  searchParams.cTimeoutMultiplier = 4; // ~5.12 seconds timeout (was 19s)

  std::cout << ">>> PRESS THE RED SYNC BUTTON ON THE WII REMOTE NOW <<<"
            << std::endl;
  std::cout << "Scanning for unknown Wii Remotes over Bluetooth (Timeout: ~5 "
               "seconds)..."
            << std::endl;

  hFind = BluetoothFindFirstDevice(&searchParams, &deviceInfo);
  if (!hFind) {
    std::cout << "No Wii Remote found during scan." << std::endl;
    return false;
  }

  bool found = false;
  do {
    std::wstring name = deviceInfo.szName;
    if (name.find(L"Nintendo RVL-CNT-01") != std::wstring::npos) {
      std::wcout << L"Found Wii Remote via Bluetooth: " << name << std::endl;

      // 3. Connect by enabling HID service directly, bypassing explicit PIN
      // prompts (matches legacy DLL)
      GUID hidGuid = HumanInterfaceDeviceServiceClass_UUID;
      std::cout << "Enabling HID Service (this bypasses manual PIN pairing)..."
                << std::endl;
      DWORD serviceResult = BluetoothSetServiceState(
          NULL, &deviceInfo, &hidGuid, BLUETOOTH_SERVICE_ENABLE);

      if (serviceResult == ERROR_SUCCESS) {
        std::cout << "HID service enabled successfully. Device is paired!"
                  << std::endl;
        found = true;
      } else {
        std::cerr << "Failed to enable HID service manually. Error Code: "
                  << serviceResult << std::endl;
      }
      break;
    }
  } while (BluetoothFindNextDevice(hFind, &deviceInfo));

  BluetoothFindDeviceClose(hFind);
  return found;
}

std::wstring Connection::FindWiimoteDevicePath() {
  GUID hidGuid;
  HidD_GetHidGuid(&hidGuid);

  HDEVINFO deviceInfoSet = SetupDiGetClassDevsW(
      &hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

  if (deviceInfoSet == INVALID_HANDLE_VALUE) {
    return L"";
  }

  SP_DEVICE_INTERFACE_DATA deviceInterfaceData = {0};
  deviceInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

  DWORD deviceIndex = 0;
  std::wstring foundPath = L"";

  while (SetupDiEnumDeviceInterfaces(deviceInfoSet, nullptr, &hidGuid,
                                     deviceIndex, &deviceInterfaceData)) {
    DWORD requiredSize = 0;
    SetupDiGetDeviceInterfaceDetailW(deviceInfoSet, &deviceInterfaceData,
                                     nullptr, 0, &requiredSize, nullptr);

    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
      auto detailDataBuffer = std::make_unique<BYTE[]>(requiredSize);
      auto detailData = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(
          detailDataBuffer.get());
      detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

      if (SetupDiGetDeviceInterfaceDetailW(deviceInfoSet, &deviceInterfaceData,
                                           detailData, requiredSize, nullptr,
                                           nullptr)) {

        HANDLE fileHandle = CreateFileW(detailData->DevicePath,
                                        0, // Query only
                                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                                        nullptr, OPEN_EXISTING, 0, nullptr);

        if (fileHandle != INVALID_HANDLE_VALUE) {
          HIDD_ATTRIBUTES attributes = {0};
          attributes.Size = sizeof(HIDD_ATTRIBUTES);

          if (HidD_GetAttributes(fileHandle, &attributes)) {
            // Nintendo VID = 0x057E
            // Wiimote PID = 0x0306
            // Wiimote Plus PID = 0x0330
            if (attributes.VendorID == 0x057E &&
                (attributes.ProductID == 0x0306 ||
                 attributes.ProductID == 0x0330)) {
              foundPath = detailData->DevicePath;
              CloseHandle(fileHandle);
              break;
            }
          }
          CloseHandle(fileHandle);
        }
      }
    }
    deviceIndex++;
  }

  SetupDiDestroyDeviceInfoList(deviceInfoSet);
  return foundPath;
}

void Connection::ReadLoop() {
  // Basic read loop logic to repeatedly grab reports from the Wiimote
  const size_t REPORT_LENGTH = 22; // Common max size for Wiimote input reports
  uint8_t reportBuffer[REPORT_LENGTH];

  OVERLAPPED overlapped = {0};
  overlapped.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
  if (!overlapped.hEvent) {
    std::cerr << "Failed to create overlapped event." << std::endl;
    m_shouldStop = true;
    return;
  }

  while (!m_shouldStop) {
    memset(reportBuffer, 0, REPORT_LENGTH);
    DWORD bytesRead = 0;

    BOOL readResult = ReadFile(m_deviceHandle, reportBuffer, REPORT_LENGTH,
                               &bytesRead, &overlapped);

    if (!readResult) {
      if (GetLastError() == ERROR_IO_PENDING) {
        // Wait for the I/O to complete or the stop event
        HANDLE waitObjects[] = {overlapped.hEvent};
        DWORD waitResult = WaitForMultipleObjects(1, waitObjects, FALSE, 100);

        if (waitResult == WAIT_OBJECT_0) {
          GetOverlappedResult(m_deviceHandle, &overlapped, &bytesRead, FALSE);
          ResetEvent(overlapped.hEvent);
        } else if (waitResult == WAIT_TIMEOUT) {
          continue; // Loop and check m_shouldStop
        } else {
          // Error or cancel
          break;
        }
      } else {
        std::cerr << "Read error: " << GetLastError() << std::endl;
        break;
      }
    }

    if (bytesRead > 0) {
      m_lastReportTime = std::chrono::steady_clock::now();
      if (m_normalizer.ParseReport(reportBuffer, bytesRead)) {
        // State successfully updated
      }
    }
  }

  CloseHandle(overlapped.hEvent);
  // Explicitly do not touch m_connected here, let Disconnect handle it
  // perfectly
}
