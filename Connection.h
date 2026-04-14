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
#include <functional>
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
    bool SetLEDsAndRumble(bool led1, bool rumble);

    // Bluetooth helpers — safe to call from any thread (no m_connectionMutex).
    // Remove remembered/authenticated Wii Remote BT profiles so a fresh SYNC
    // can be detected as an unknown device.  Fast (no inquiry issued).
    void RemoveStalePairings();
    // Run a single BT inquiry cycle (~2.5 s, cTimeoutMultiplier = 2).
    // Enables HID service if a Wii Remote is found.  Returns true on success.
    bool ScanOnce();

    // Event callbacks (invoked from background threads — must be thread-safe)
    void SetOnConnected(std::function<void()> cb)    { m_onConnected    = std::move(cb); }
    void SetOnDisconnected(std::function<void()> cb) { m_onDisconnected = std::move(cb); }

private:
    bool AutoConnectBluetooth();
    std::wstring FindWiimoteDevicePath();
    void ReadLoop();
    bool WriteReport(UCHAR* buffer, size_t size);

    HANDLE m_deviceHandle;
    std::atomic<bool> m_connected;
    std::atomic<bool> m_shouldStop;
    // Set by ReadLoop when it exits due to an I/O error (not a graceful stop).
    // Allows IsConnected() to detect disconnect immediately without waiting for
    // the 1-second watchdog.
    std::atomic<bool> m_readLoopExited{false};

    // A connection is "confirmed" only after kStabilizationMs of continuous
    // incoming reports.  This prevents stale Windows HID entries (whose
    // ReadLoop exits immediately with no reports) from triggering the
    // connected/disconnected callbacks.
    static constexpr int kStabilizationMs = 500;
    std::atomic<bool> m_connectionConfirmed{false};
    std::atomic<std::chrono::steady_clock::time_point> m_connectionStartTime;
    std::thread m_readThread;
    InputNormalizer m_normalizer;

    std::atomic<std::chrono::steady_clock::time_point> m_lastReportTime;
    std::recursive_mutex m_connectionMutex;

    std::function<void()> m_onConnected;
    std::function<void()> m_onDisconnected;
};
