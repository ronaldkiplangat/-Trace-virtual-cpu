// include/cpu.hpp
#pragma once
#include <cstdint>
#include <array>
#include <vector>
#include "trace.hpp"

struct CPU {
    // Registers
    uint8_t  A{0}, B{0}, X{0};
    uint16_t PC{0};
    uint16_t SP{0xFF};      // simple descending stack if you extend with PUSH/POP
    uint8_t  FLAGS{0};      // bit0 C, bit1 Z, bit2 N, bit3 V

    // Memory
    static constexpr size_t MEM_SIZE = 65536;
    std::array<uint8_t, MEM_SIZE> mem{};

    // Control / internal state
    bool halted{false};
    uint64_t   cycles{0};
    MicroState ustate{MicroState::FetchOp};
    uint8_t    opcode{0};
    uint16_t   opaddr{0};   // decoded absolute address or imm

    // Visual trace timeline (one frame per micro-step)
    std::vector<TraceFrame> timeline;

    // API
    void reset(uint16_t pc_init);
    void load_program(const std::vector<uint8_t>& bytes, uint16_t origin);
    void write16(uint16_t addr, uint16_t value);

    // Run controls
    void step_cycle();   // advance one micro-step (records a TraceFrame)
    void step_instr();   // execute until next FetchOp boundary (one instruction)

private:
    // Helpers used by implementation
    void    push_event(std::vector<BusEvent>& ev, BusDir dir, uint16_t addr, uint8_t data, const char* note);
    uint8_t read(uint16_t addr, std::vector<BusEvent>& ev, const char* note = "");
    void    write(uint16_t addr, uint8_t data, std::vector<BusEvent>& ev, const char* note = "");
    void    setZN(uint8_t v);                         // update Z and N flags
    void    setAddFlags(uint16_t result, uint8_t a, uint8_t b); // C,Z,N,V for ADD
};
