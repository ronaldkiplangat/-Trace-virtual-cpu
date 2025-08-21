#include "cpu.hpp"
#include <cassert>

static constexpr uint8_t F_C = 1u<<0; // Carry
static constexpr uint8_t F_Z = 1u<<1; // Zero
static constexpr uint8_t F_N = 1u<<2; // Negative (bit7)
static constexpr uint8_t F_V = 1u<<3; // Overflow

void CPU::reset(uint16_t pc_init) {
    A=B=X=0; FLAGS=0; SP=0x01FF; // simple stack top
    PC = pc_init;
    halted = false;
    cycles = 0;
    ustate = MicroState::FetchOp;
    timeline.clear();
}

void CPU::load_program(const std::vector<uint8_t>& bytes, uint16_t origin) {
    for (size_t i=0;i<bytes.size();++i) mem[origin + i] = bytes[i];
}

void CPU::write16(uint16_t addr, uint16_t value){
    mem[addr] = static_cast<uint8_t>(value & 0xFF);
    mem[addr+1] = static_cast<uint8_t>(value >> 8);
}

void CPU::push_event(std::vector<BusEvent>& ev, BusDir dir, uint16_t addr, uint8_t data, const char* note){
    ev.push_back(BusEvent{cycles, ustate, dir, addr, data, note});
}

uint8_t CPU::read(uint16_t addr, std::vector<BusEvent>& ev, const char* note){
    uint8_t v = mem[addr];
    push_event(ev, BusDir::Read, addr, v, note);
    return v;
}

void CPU::write(uint16_t addr, uint8_t data, std::vector<BusEvent>& ev, const char* note){
    mem[addr] = data;
    push_event(ev, BusDir::Write, addr, data, note);
}

void CPU::setZN(uint8_t v){
    if (v==0) FLAGS |= F_Z; else FLAGS &= ~F_Z;
    if (v & 0x80) FLAGS |= F_N; else FLAGS &= ~F_N;
}

void CPU::setAddFlags(uint16_t res, uint8_t a, uint8_t b){
    // Carry: result exceeds 0xFF
    if (res & 0x100) FLAGS |= F_C; else FLAGS &= ~F_C;
    uint8_t r = static_cast<uint8_t>(res);
    setZN(r);
    // Overflow: sign of a == sign of b but sign of r != sign of a
    bool ov = ((a ^ b) & 0x80) == 0 && ((a ^ r) & 0x80) != 0;
    if (ov) FLAGS |= F_V; else FLAGS &= ~F_V;
}

void CPU::step_instr(){
    if (halted) return;
    MicroState start = MicroState::FetchOp;
    if (ustate != start){
        // Run cycles until next instruction boundary
        do { step_cycle(); } while (ustate != start && !halted);
    }
    // Execute one full instruction
    do { step_cycle(); } while (ustate != MicroState::FetchOp && !halted);
}

void CPU::step_cycle(){
    if (halted) return;

    std::vector<BusEvent> ev; // events emitted this micro-step

    switch(ustate){
        case MicroState::FetchOp: {
            opcode = read(PC++, ev, "opcode fetch");
            ustate = MicroState::Decode;
            break;
        }
        case MicroState::Decode: {
            // For operand fetches we reuse FetchOpLo/Hi
            switch(opcode){
                case 0x00: ustate = MicroState::Execute; break; // NOP
                case 0xFF: ustate = MicroState::Execute; break; // HLT

                case 0x10: // LDA #imm
                case 0x11: // LDB #imm
                case 0x33: // LDX #imm
                    ustate = MicroState::FetchOpLo; break;

                case 0x12: // LDA [abs]
                case 0x13: // STA [abs]
                case 0x30: // JMP abs
                case 0x31: // JZ abs
                case 0x32: // JNZ abs
                case 0x34: // LDA [abs+X]
                case 0x35: // STA [abs+X]
                    ustate = MicroState::FetchOpLo; break;

                default:
                    ustate = MicroState::Execute; // implied ops like ADD B
            }
            break;
        }
        case MicroState::FetchOpLo: {
            uint8_t lo = read(PC++, ev, "operand lo");
            opaddr = lo;
            // Which op determines whether we need hi
            switch(opcode){
                case 0x10: case 0x11: case 0x33:
                    // immediate operand captured in opaddr low
                    ustate = MicroState::Execute; break;
                default:
                    ustate = MicroState::FetchOpHi; break;
            }
            break;
        }
        case MicroState::FetchOpHi: {
            uint8_t hi = read(PC++, ev, "operand hi");
            opaddr |= static_cast<uint16_t>(hi) << 8;
            ustate = MicroState::Execute;
            break;
        }
        case MicroState::Execute: {
            switch(opcode){
                case 0x00: /*NOP*/ break;
                case 0xFF: halted = true; ustate = MicroState::Halted; break;

                case 0x10: A = static_cast<uint8_t>(opaddr & 0xFF); setZN(A); break; // LDA #imm
                case 0x11: B = static_cast<uint8_t>(opaddr & 0xFF); setZN(B); break; // LDB #imm
                case 0x33: X = static_cast<uint8_t>(opaddr & 0xFF); setZN(X); break; // LDX #imm

                case 0x12: { // LDA [abs]
                    uint8_t v = read(opaddr, ev, "LDA mem");
                    A = v; setZN(A);
                    break;
                }
                case 0x13: { // STA [abs]
                    write(opaddr, A, ev, "STA mem");
                    break;
                }
                case 0x20: { // ADD B
                    uint16_t r = static_cast<uint16_t>(A) + static_cast<uint16_t>(B);
                    setAddFlags(r, A, B);
                    A = static_cast<uint8_t>(r);
                    break;
                }
                case 0x21: { // SUB B => A + (~B + 1)
                    uint16_t r = static_cast<uint16_t>(A) + static_cast<uint16_t>(~B) + 1;
                    // For SUB, carry means no borrow. Here treat carry as inverse borrow
                    if (r & 0x100) FLAGS |= F_C; else FLAGS &= ~F_C;
                    uint8_t rr = static_cast<uint8_t>(r);
                    setZN(rr);
                    bool ov = ((A ^ B) & 0x80) && ((A ^ rr) & 0x80); // signed overflow
                    if (ov) FLAGS |= F_V; else FLAGS &= ~F_V;
                    A = rr;
                    break;
                }
                case 0x22: A &= B; setZN(A); break; // AND B
                case 0x23: A |= B; setZN(A); break; // OR  B
                case 0x24: A ^= B; setZN(A); break; // XOR B
                case 0x25: A = static_cast<uint8_t>(A+1); setZN(A); break; // INC A
                case 0x26: A = static_cast<uint8_t>(A-1); setZN(A); break; // DEC A

                case 0x30: PC = opaddr; break; // JMP
                case 0x31: if (FLAGS & F_Z) PC = opaddr; break; // JZ
                case 0x32: if (!(FLAGS & F_Z)) PC = opaddr; break; // JNZ

                case 0x34: { // LDA [abs+X]
                    uint16_t ea = static_cast<uint16_t>(opaddr + X);
                    uint8_t v = read(ea, ev, "LDA [abs+X]");
                    A = v; setZN(A);
                    break;
                }
                case 0x35: { // STA [abs+X]
                    uint16_t ea = static_cast<uint16_t>(opaddr + X);
                    write(ea, A, ev, "STA [abs+X]");
                    break;
                }
                default:
                    // Unknown opcode -> treat as HLT for safety
                    halted = true; ustate = MicroState::Halted; break;
            }
            if (!halted) ustate = MicroState::WriteBack;
            break;
        }
        case MicroState::WriteBack: {
            // For this simple core, most writeback happened in Execute; this state is just a boundary.
            ustate = MicroState::FetchOp;
            break;
        }
        case MicroState::MemRead:
        case MicroState::MemWrite:
        case MicroState::Halted:
            // Not used separately in this compact model
            break;
    }

    // Snapshot after this micro-step
    TraceFrame tf{
        cycles, PC, A, B, X, static_cast<uint8_t>(SP & 0xFF), FLAGS, opcode, ustate, ev
    };
    timeline.push_back(std::move(tf));
    cycles++;
}