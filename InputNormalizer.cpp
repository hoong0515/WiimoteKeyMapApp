#include "InputNormalizer.h"

InputNormalizer::InputNormalizer() {
    // Initialize states
}

const WiiRemoteState& InputNormalizer::GetState() const {
    return m_currentState;
}

bool InputNormalizer::ParseReport(const uint8_t* reportBuffer, size_t size) {
    if (size < 3) return false;

    // Report ID determines payload format
    uint8_t reportId = reportBuffer[0];
    
    // Most standard incoming reports follow 
    // Data[1] and Data[2] = Core Buttons (always present from 0x30 to 0x3F)
    if (reportId >= 0x30 && reportId <= 0x3F) {
        
        m_previousState = m_currentState; // Save previous for edge detection
        
        ParseCoreButtons(&reportBuffer[1]);
        ComputeJustPressed();

        // Depending on report ID, there might be acc / IR as well
        // For example, 0x31: Core Buttons + Accelerometer
        // For example, 0x33: Core Buttons + Accel + 12 IR bytes
        return true;
    }

    return false;
}

void InputNormalizer::ParseCoreButtons(const uint8_t* data) {
    // Byte 1:
    // Bit 0: DPAD Left
    // Bit 1: DPAD Right
    // Bit 2: DPAD Down
    // Bit 3: DPAD Up
    // Bit 4: Plus
    // Bit 5: (Unused?) 
    // Bit 6: Two
    // Bit 7: One
    
    // Byte 2:
    // Bit 0: B
    // Bit 1: (Unused?)
    // Bit 2: B
    // Bit 3: A
    // Bit 4: Minus
    // Bit 5: (Unused?)
    // Bit 6: (Unused?)
    // Bit 7: Home

    m_currentState.dpad_left  = (data[0] & 0x01) != 0;
    m_currentState.dpad_right = (data[0] & 0x02) != 0;
    m_currentState.dpad_down  = (data[0] & 0x04) != 0;
    m_currentState.dpad_up    = (data[0] & 0x08) != 0;
    m_currentState.plus_pressed = (data[0] & 0x10) != 0;
    
    // By WiiBrew specifications:
    m_currentState.two_pressed  = (data[1] & 0x01) != 0;
    m_currentState.one_pressed  = (data[1] & 0x02) != 0;

    m_currentState.b_pressed    = (data[1] & 0x04) != 0;
    m_currentState.a_pressed    = (data[1] & 0x08) != 0;
    m_currentState.minus_pressed= (data[1] & 0x10) != 0;
    m_currentState.home_pressed = (data[1] & 0x80) != 0;
}

void InputNormalizer::ComputeJustPressed() {
    m_currentState.a_just_pressed = (m_currentState.a_pressed && !m_previousState.a_pressed);
    m_currentState.b_just_pressed = (m_currentState.b_pressed && !m_previousState.b_pressed);
    
    m_currentState.dpad_up_just_pressed = (m_currentState.dpad_up && !m_previousState.dpad_up);
    m_currentState.dpad_down_just_pressed = (m_currentState.dpad_down && !m_previousState.dpad_down);
    m_currentState.dpad_left_just_pressed = (m_currentState.dpad_left && !m_previousState.dpad_left);
    m_currentState.dpad_right_just_pressed = (m_currentState.dpad_right && !m_previousState.dpad_right);
    
    m_currentState.plus_just_pressed = (m_currentState.plus_pressed && !m_previousState.plus_pressed);
    m_currentState.minus_just_pressed = (m_currentState.minus_pressed && !m_previousState.minus_pressed);
    m_currentState.home_just_pressed = (m_currentState.home_pressed && !m_previousState.home_pressed);
    m_currentState.one_just_pressed = (m_currentState.one_pressed && !m_previousState.one_pressed);
    m_currentState.two_just_pressed = (m_currentState.two_pressed && !m_previousState.two_pressed);
}
