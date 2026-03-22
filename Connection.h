#pragma once

#include <windows.h>
#include <hidsdi.h>
#include <setupapi.h>
#include <bluetoothapis.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include "InputNormalizer.h"

class Connection {
public:
    Connection();
    ~Connection();

    // Connection modes
    bool PassiveConnect();
    bool ActiveSearchConnect();

    void Disconnect();
    bool IsConnected() const;
    const WiiRemoteState& GetState() const;
    void SetLEDsAndRumble(bool led1, bool rumble);

private:
    bool AutoConnectBluetooth();
    std::wstring FindWiimoteDevicePath();
    void ReadLoop();
    bool WriteReport(UCHAR* buffer, size_t size);

    HANDLE m_deviceHandle;
    std::atomic<bool> m_connected;
    std::atomic<bool> m_shouldStop;
    std::thread m_readThread;
    InputNormalizer m_normalizer;
    
    std::atomic<std::chrono::steady_clock::time_point> m_lastReportTime;
    std::recursive_mutex m_connectionMutex;
};
