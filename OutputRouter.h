#pragma once

#include "InputNormalizer.h"
#include <windows.h>
#include <chrono>

class OutputRouter {
public:
    OutputRouter();
    ~OutputRouter() = default;

    // Call this inside the main loop to process states and hit SendInput
    void Route(const WiiRemoteState& state);
    
    // Release all keys
    void Reset();

private:
    void SendKey(WORD vKey, bool down);
    
    struct RepeatState {
        bool isHeld{false};
        std::chrono::steady_clock::time_point nextRepeat;
    };

    // Minimal mapping state
    WiiRemoteState m_previousState;
    
    // Auto-repeat tracking for each button by name
    struct {
        RepeatState a, b, up, down, left, right, plus, minus, home, one, two;
    } m_repeatStates;
};
