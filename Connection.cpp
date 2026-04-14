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

  // Reset the report watchdog early to avoid raced timeouts during Sleep
  m_lastReportTime = std::chrono::steady_clock::now();

  // Liveness check: attempt a write before declaring the connection live.
  // When Windows retains a stale HID entry after the remote powers off,
  // CreateFileW still succeeds but WriteFile fails immediately with
  // ERROR_DEVICE_NOT_CONNECTED.  Catching that here prevents a false
  // "connected" state that would only be corrected seconds later by the
  // watchdog or ReadLoop exit.
  if (!SetLEDsAndRumble(true, true)) {
    std::cerr << "Device path found but write failed (stale HID entry). "
                 "Error: " << GetLastError() << std::endl;
    CloseHandle(m_deviceHandle);
    m_deviceHandle = INVALID_HANDLE_VALUE;
    return false;
  }
  Sleep(150);
  SetLEDsAndRumble(true, false); // LED1 On, Rumble Off

  std::cout << "Successfully connected to Wii Remote." << std::endl;

  UCHAR reportMode[22] = {0};
  reportMode[0] = 0x12; // Report mode config
  reportMode[1] = 0x04; // Continuous reporting bit (Bit 2) enabled
  reportMode[2] = 0x31; // Mode 0x31
  WriteReport(reportMode, sizeof(reportMode));

  // Now declare the connection fully initialized and running
  m_readLoopExited = false;
  m_connectionConfirmed = false;
  m_connectionStartTime = std::chrono::steady_clock::now();
  m_connected = true;
  m_shouldStop = false;

  // Start read thread.
  // m_onConnected is NOT called here — it fires from ReadLoop after
  // kStabilizationMs of continuous incoming reports (see ReadLoop).
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

bool Connection::SetLEDsAndRumble(bool led1, bool rumble) {
  if (m_deviceHandle == INVALID_HANDLE_VALUE)
    return false;

  UCHAR buf[22] = {0}; // Wiimote output reports must be exactly 22 bytes
  buf[0] = 0x11;       // Report ID 0x11: Player LEDs

  UCHAR data = 0x00;
  if (rumble)
    data |= 0x01; // Bit 0: Rumble
  if (led1)
    data |= 0x10; // Bit 4: LED 1

  buf[1] = data;

  return WriteReport(buf, sizeof(buf));
}

void Connection::Disconnect() {
  std::lock_guard<std::recursive_mutex> lock(m_connectionMutex);
  bool expected = true;
  if (m_connected.compare_exchange_strong(expected, false)) {
    // Capture confirmation state before resetting so we can decide whether
    // to fire m_onDisconnected below.
    const bool wasConfirmed = m_connectionConfirmed.exchange(false);

    m_shouldStop = true;
    m_readLoopExited = false; // Reset for the next connection

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

    // Only notify if the connection was actually confirmed as stable.
    // A stale HID entry that never delivered reports never set wasConfirmed,
    // so it produces neither a connected nor a disconnected notification.
    if (wasConfirmed && m_onDisconnected) m_onDisconnected();
  }
}

bool Connection::IsConnected() const {
  if (m_connected) {
    // Fast path: the read loop exited due to an I/O error (not a graceful stop).
    // Dolphin-style: detect failure immediately from the I/O thread signal
    // rather than waiting for the watchdog timer to expire.
    if (m_readLoopExited) {
      const_cast<Connection *>(this)->Disconnect();
      return false;
    }

    // Watchdog: if no HID report has arrived for 1 seconds the device has
    // powered off or gone out of range.  1s is generous enough to avoid
    // false-positives during report-mode transitions while still catching a
    // switched-off remote quickly.
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(
        now - m_lastReportTime.load());
    if (duration.count() > 1) {
      const_cast<Connection *>(this)->Disconnect();
      return false;
    }
  }
  return m_connected;
}

const WiiRemoteState &Connection::GetState() const {
  return m_normalizer.GetState();
}

void Connection::RemoveStalePairings() {
  // Query the BT database (no inquiry) for remembered/authenticated Wii
  // Remotes and remove them so that a fresh SYNC press is seen as unknown.
  BLUETOOTH_DEVICE_SEARCH_PARAMS sp = {
      sizeof(BLUETOOTH_DEVICE_SEARCH_PARAMS),
      1, 1, 0, 1, // returnAuthenticated, returnRemembered, returnUnknown=0, returnConnected
      0,          // issueInquiry = false (fast, no radio scan)
      2, NULL
  };
  BLUETOOTH_DEVICE_INFO di = {sizeof(BLUETOOTH_DEVICE_INFO)};
  HBLUETOOTH_DEVICE_FIND hFind = BluetoothFindFirstDevice(&sp, &di);
  if (hFind) {
    do {
      if (std::wstring(di.szName).find(L"Nintendo RVL-CNT-01") != std::wstring::npos) {
        std::wcout << L"Removing stale Wii Remote profile: " << di.szName << std::endl;
        BluetoothRemoveDevice(&di.Address);
      }
    } while (BluetoothFindNextDevice(hFind, &di));
    BluetoothFindDeviceClose(hFind);
  }
}

bool Connection::ScanOnce() {
  // Single BT inquiry cycle (~2.56 s).  Only looks for unknown (unpaired)
  // devices — the opposite of PassiveConnect which relies on the HID list.
  // Safe to call from any thread without holding m_connectionMutex.
  BLUETOOTH_DEVICE_SEARCH_PARAMS sp = {
      sizeof(BLUETOOTH_DEVICE_SEARCH_PARAMS),
      0, 0, 1, 0, // returnUnknown only
      1,          // issueInquiry
      2,          // cTimeoutMultiplier — 2 × 1.28 s ≈ 2.56 s per cycle
      NULL
  };
  BLUETOOTH_DEVICE_INFO di = {sizeof(BLUETOOTH_DEVICE_INFO)};
  HBLUETOOTH_DEVICE_FIND hFind = BluetoothFindFirstDevice(&sp, &di);
  if (!hFind)
    return false;

  bool found = false;
  do {
    if (std::wstring(di.szName).find(L"Nintendo RVL-CNT-01") != std::wstring::npos) {
      std::wcout << L"Wii Remote found via continuous scan: " << di.szName << std::endl;
      GUID hidGuid = HumanInterfaceDeviceServiceClass_UUID;
      if (BluetoothSetServiceState(NULL, &di, &hidGuid, BLUETOOTH_SERVICE_ENABLE) == ERROR_SUCCESS) {
        std::cout << "HID service enabled." << std::endl;
        found = true;
      }
      break;
    }
  } while (BluetoothFindNextDevice(hFind, &di));

  BluetoothFindDeviceClose(hFind);
  return found;
}

bool Connection::AutoConnectBluetooth() {
  // Used by the manual "Search & Connect" path: clean up stale pairings
  // first, then run a single (longer) inquiry cycle.
  RemoveStalePairings();
  return ScanOnce();
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

      // Stabilization check: promote to "confirmed" once kStabilizationMs of
      // continuous reports have arrived.  Stale HID entries never reach this
      // point because their ReadFile fails before any report is delivered.
      if (!m_connectionConfirmed) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            m_lastReportTime.load() - m_connectionStartTime.load()).count();
        if (elapsed >= kStabilizationMs) {
          m_connectionConfirmed = true;
          if (m_onConnected) m_onConnected();
        }
      }

      m_normalizer.ParseReport(reportBuffer, bytesRead);
    }
  }

  CloseHandle(overlapped.hEvent);

  // If we exited due to an unexpected I/O error (not because Disconnect() set
  // m_shouldStop), signal IsConnected() so it can call Disconnect() immediately
  // without waiting for the watchdog timeout.
  if (!m_shouldStop) {
    m_readLoopExited = true;
  }
}
