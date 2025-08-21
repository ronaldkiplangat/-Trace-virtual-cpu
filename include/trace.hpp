// include/trace.hpp
#pragma once
#include <cstdint>
#include <string>
#include <vector>

enum class MicroState {
    FetchOp, FetchOpLo, FetchOpHi, Decode, MemRead, MemWrite, Execute, WriteBack, Halted
};

enum class BusDir { Read, Write, None };

struct BusEvent {
    uint64_t cycle;        // global cycle number
    MicroState state;      // micro-step
    BusDir dir;            // memory direction
    uint16_t address;      // memory address (if any)
    uint8_t data;          // byte transferred (if any)
    std::string note;      // e.g., "PC fetch", "write A", "set Z"
};

struct TraceFrame {
    // Snapshot after each micro-step
    uint64_t cycle;
    uint16_t pc;
    uint8_t a, b, x, sp, flags;
    uint8_t opcode;
    MicroState state;
    std::vector<BusEvent> events; // events emitted during this step
};