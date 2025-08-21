#include <vector>
#include <cstdint>

// Same content you had in main.cpp earlier, just moved here:
std::vector<uint8_t> demo_program() {
    std::vector<uint8_t> p;
    auto emit16=[&](uint16_t v){ p.push_back(v & 0xFF); p.push_back(v>>8); };
    p.push_back(0x10); p.push_back(0x00);                 // LDA #0
    p.push_back(0x11); p.push_back(0x01);                 // LDB #1 (increment)
    // loop:
    p.push_back(0x13); emit16(0xFF00);                    // STA [OUT0]
    p.push_back(0x20);                                    // ADD B  (A++)
    p.push_back(0x11); p.push_back(0x0A);                 // LDB #10
    p.push_back(0x24);                                    // XOR B (twiddle flags)
    p.push_back(0x24);                                    // XOR B (undo)
    p.push_back(0x33); p.push_back(0x0A);                 // LDX #10
    p.push_back(0x21);                                    // SUB B (simple flag play)
    p.push_back(0x30); emit16(0x0004);                    // JMP back to STA
    p.push_back(0xFF);                                    // HLT (won't be reached here)
    return p;
}
