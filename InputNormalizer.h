#ifndef INPUT_NORMALIZER_H
#define INPUT_NORMALIZER_H

#pragma once
#include <cstdint>
#include <vector>

// Representing the state of the Wii Remote at a given time
struct WiiRemoteState {
    // Buttons
    bool a_pressed{false};
    bool b_pressed{false};
    bool dpad_up{false};
    bool dpad_down{false};
    bool dpad_left{false};
    bool dpad_right{false};
    bool plus_pressed{false};
    bool minus_pressed{false};
    bool home_pressed{false};
    bool one_pressed{false};
    bool two_pressed{false};

    // Edge detection tracking
    bool a_just_pressed{false};
    bool b_just_pressed{false};
    bool dpad_up_just_pressed{false};
    bool dpad_down_just_pressed{false};
    bool dpad_left_just_pressed{false};
    bool dpad_right_just_pressed{false};
    bool plus_just_pressed{false};
    bool minus_just_pressed{false};
    bool home_just_pressed{false};
    bool one_just_pressed{false};
    bool two_just_pressed{false};

    // Accelerometer
    int accel_x{0};
    int accel_y{0};
    int accel_z{0};

    // Battery Life (0-100)
    int batteryLevel{0};

    // IR Camera dots (up to 4)
    struct IRDot {
        bool visible{false};
        int x{0};
        int y{0};
        int size{0};
    };
    IRDot ir_dots[4];
};

class InputNormalizer {
public:
    InputNormalizer();
    ~InputNormalizer() = default;

    bool ParseReport(const uint8_t* reportBuffer, size_t size);
    const WiiRemoteState& GetState() const;

private:
    void ParseCoreButtons(const uint8_t* data);
    void ComputeJustPressed();
    
    WiiRemoteState m_currentState;
    WiiRemoteState m_previousState;
};

#endif // INPUT_NORMALIZER_H
