// src/main.cpp
#include <iostream>
#include <iomanip>
#include <sstream>
#include <vector>
#include <unordered_set>
#include <cstdint>
#include <algorithm>
#include <chrono>
#include <thread>

#include "cpu.hpp"

#include <fstream>

// --- file loaders ---
static bool read_file_binary(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if(!f) return false;
    f.seekg(0, std::ios::end);
    std::streamsize n = f.tellg();
    if(n < 0) return false;
    f.seekg(0, std::ios::beg);
    out.resize((size_t)n);
    if(n > 0) f.read(reinterpret_cast<char*>(out.data()), n);
    return (bool)f;
}

// Accepts text files containing hex bytes separated by spaces/newlines, e.g.:
//   10 2A 13 00 FF 20
static bool read_file_hexbytes(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path);
    if(!f) {
        std::cerr << "[loadhex] cannot open '" << path << "'\n";
        return false;
    }
    out.clear();
    std::string line;
    size_t lineno = 0;
    while (std::getline(f, line)) {
        ++lineno;
        // strip comment markers: # ... ; ... // ...
        auto cut = line.find_first_of("#;");
        if (cut != std::string::npos) line.resize(cut);
        cut = line.find("//");
        if (cut != std::string::npos) line.resize(cut);

        std::istringstream iss(line);
        std::string tok;
        while (iss >> tok) {
            // normalize token
            // remove commas/underscores
            tok.erase(std::remove(tok.begin(), tok.end(), ','), tok.end());
            tok.erase(std::remove(tok.begin(), tok.end(), '_'), tok.end());
            // allow 0x prefix
            if (tok.size() > 2 && (tok[0]=='0') && (tok[1]=='x' || tok[1]=='X')) {
                tok = tok.substr(2);
            }
            if (tok.empty()) continue;

            // ensure hex
            auto is_hex = [](char c){
                return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');
            };
            if (!std::all_of(tok.begin(), tok.end(), is_hex)) {
                std::cerr << "[loadhex] non-hex token '" << tok
                          << "' at line " << lineno << "\n";
                return false;
            }

            unsigned v = 0;
            try { v = std::stoul(tok, nullptr, 16); }
            catch(...) {
                std::cerr << "[loadhex] parse error for token '" << tok
                          << "' at line " << lineno << "\n";
                return false;
            }
            if (v > 0xFF) {
                std::cerr << "[loadhex] byte out of range '" << tok
                          << "' at line " << lineno << "\n";
                return false;
            }
            out.push_back(static_cast<uint8_t>(v));
        }
    }
    if (out.empty()) {
        std::cerr << "[loadhex] no bytes read from '" << path << "'\n";
        return false;
    }
    return true;
}



// ---- Disassembler with explicit addressing modes ----
static int instr_len(uint8_t op) {
    switch (op) {
        case 0x10: // LDA #imm
        case 0x11: // LDB #imm
        case 0x33: // LDX #imm
            return 2;
        case 0x13: // STA abs
        case 0x30: // JMP abs
            return 3;
        case 0x20: // ADD B
        case 0x21: // SUB B
        case 0x24: // XOR B
        case 0xFF: // HLT
            return 1;
        default:
            return 1; // treat as .DB
    }
}

static std::string hex8s(uint8_t v)  { std::ostringstream o; o<<std::hex<<std::setfill('0')<<std::setw(2)<<int(v); return o.str(); }
static std::string hex16s(uint16_t v){ std::ostringstream o; o<<std::hex<<std::setfill('0')<<std::setw(4)<<int(v); return o.str(); }

static std::string disasm_one(const CPU& c, uint16_t pc) {
    const uint8_t op = c.mem[pc];
    const int L = instr_len(op);

    const uint8_t lo = (L >= 2 ? c.mem[(uint16_t)(pc + 1)] : 0);
    const uint8_t hi = (L >= 3 ? c.mem[(uint16_t)(pc + 2)] : 0);
    const uint16_t abs = (uint16_t)(lo | (uint16_t(hi) << 8));

    auto b = [&](int i)->std::string{ return hex8s(c.mem[(uint16_t)(pc + i)]); };

    std::ostringstream out;

    // bytes column (up to 3 bytes)
    out << hex16s(pc) << ":  "
        << hex8s(op) << (L>=2 ? (" " + b(1)) : "   ")
        << (L>=3 ? (" " + b(2)) : "   ")
        << "   ";

    // mnemonic + operand + addressing-mode comment
    switch (op) {
        // Immediate addressing
        case 0x10: out << "LDA #$" << hex8s(lo) << "      ; A <- imm $" << hex8s(lo) << " (immediate)"; break;
        case 0x11: out << "LDB #$" << hex8s(lo) << "      ; B <- imm $" << hex8s(lo) << " (immediate)"; break;
        case 0x33: out << "LDX #$" << hex8s(lo) << "      ; X <- imm $" << hex8s(lo) << " (immediate)"; break;

        // Absolute / memory addressing
        case 0x13: out << "STA $"  << hex16s(abs) << "       ; [abs $" << hex16s(abs) << "] <- A (absolute/memory)"; break;
        case 0x30: out << "JMP $"  << hex16s(abs) << "       ; PC <- abs $" << hex16s(abs) << " (absolute)"; break;

        // Register ops (no explicit addressing)
        case 0x20: out << "ADD B           ; A <- A + B (register)"; break;
        case 0x21: out << "SUB B           ; A <- A - B (register)"; break;
        case 0x24: out << "XOR B           ; A <- A ^ B (register)"; break;

        case 0xFF: out << "HLT             ; halt"; break;

        default:
            out << ".DB $" << hex8s(op) << "         ; data (unknown opcode)";
            break;
    }
    return out.str();
}

static void disasm_range(const CPU& c, uint16_t start, int count_instrs) {
    uint16_t pc = start;
    for (int i = 0; i < count_instrs; ++i) {
        std::cout << disasm_one(c, pc) << "\n";
        pc = uint16_t(pc + instr_len(c.mem[pc]));
    }
}



static const char* microStateName(MicroState s){
    switch(s){
        case MicroState::FetchOp:   return "FET";
        case MicroState::FetchOpLo: return "FLO";
        case MicroState::FetchOpHi: return "FHI";
        case MicroState::Decode:    return "DEC";
        case MicroState::Execute:   return "EXE";
        case MicroState::WriteBack: return "WBK";
        case MicroState::MemRead:   return "MRD";
        case MicroState::MemWrite:  return "MWR";
        case MicroState::Halted:    return "HLT";
    }
    return "?";
}


extern std::vector<uint8_t> demo_program();

static std::string hex16(uint16_t v){ std::ostringstream o; o<<std::hex<<std::setw(4)<<std::setfill('0')<<int(v); return o.str(); }
static std::string hex8(uint8_t v){ std::ostringstream o; o<<std::hex<<std::setw(2)<<std::setfill('0')<<int(v); return o.str(); }

static void print_regs(const CPU& c){
    std::cout << "PC="<<hex16(c.PC)
              << "  A="<<hex8(c.A)
              << "  B="<<hex8(c.B)
              << "  X="<<hex8(c.X)
              << "  SP="<<hex16(c.SP)
              << "  F="<<hex8(c.FLAGS)
              << "  ustate="<< microStateName(c.ustate)
              << "  cycles="<<c.cycles << "\n";
}


static void dump_mem(const CPU& c, uint16_t base, int rows=8, int cols=16){
    for(int r=0;r<rows;r++){
        uint16_t addr = base + r*cols;
        std::cout<<hex16(addr)<<": ";
        for(int ccol=0;ccol<cols;ccol++){
            std::cout<<hex8(c.mem[addr+ccol])<<' ';
        }
        std::cout<<"\n";
    }
}

// Print the last K trace frames (cycle-by-cycle bus view)
static void print_trace(const CPU& c, int k){
    if(c.timeline.empty()){ std::cout<<"(no trace yet)\n"; return; }
    int start = (int)std::max(0, (int)c.timeline.size()-k);
    for(int i=start;i<(int)c.timeline.size();++i){
        const auto& t = c.timeline[i];
        std::cout<< std::dec << t.cycle << "  "
                 << hex16(t.pc) << "  "
                 << hex8(t.opcode) << "  "
                 << hex8(t.a) << " " << hex8(t.b) << " " << hex8(t.x) << " "
                 << hex8(t.flags) << "  "
                 << (int)t.state << "  events:" << t.events.size() << "\n";
        for(const auto& e: t.events){
            std::cout<<"    " << (e.dir==BusDir::Read? "RD":"WR")
                     <<" ["<<hex16(e.address)<<"] = "<<hex8(e.data)
                     <<"  " << e.note << "\n";
        }
    }
}

int main(){
    CPU cpu;
    cpu.write16(0xFFFC, 0x0000);
    cpu.load_program(demo_program(), 0x0000);
    cpu.reset(0x0000);

    std::unordered_set<uint16_t> breakpoints;

    std::cout << "Minimal CPU Simulator (CLI)\n";
    std::cout << "Type 'help' for commands.\n\n";
    print_regs(cpu);

    std::string line;
    while (true){
        std::cout << "\n> " << std::flush;
        if(!std::getline(std::cin, line)) break;

        std::istringstream iss(line);
        std::string cmd; iss >> cmd;
        if(cmd.empty()) continue;

        // normalize lowercase
        std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

        if(cmd=="q" || cmd=="quit" || cmd=="exit"){
            break;
        }
        else if(cmd=="help" || cmd=="h" || cmd=="?"){
            std::cout <<
R"(Commands:
  s                 step one instruction
  c                 step one cycle (micro-step)
  r N               run N instructions
  g                 run until halt or breakpoint
  p                 print registers
  m ADDR [ROWS]     dump memory from hex ADDR (default 8 rows of 16)
  w ADDR BYTE       write BYTE at ADDR (both hex)
  b ADDR            add breakpoint at PC==ADDR (hex)
  bl                list breakpoints
  bc [ADDR]         clear breakpoint at ADDR or all if none
  t [K]             show last K trace frames (default 20)
  reset             reset CPU to PC=0000 and clear trace
  sleep MS          sleep for MS milliseconds (useful while running)
  d ADDR [N-instr]  disassemble N instructions starting ate address ADDR   
  loadhex [HEXFILE] [ADDR]     Load program in HEX format FROM FILE [HEXFILE] into Address [ADDR] 
  loadbin [BINFILE] [ADDR]     Load program in binary format FROM FILE [BINFILE] into Address [ADDR]   
  help              this text
  quit              exit
)";
        }
        else if(cmd=="s"){
            if(!cpu.halted) cpu.step_instr();
            print_regs(cpu);
        }
        else if(cmd=="c"){
            if(!cpu.halted) cpu.step_cycle();
            print_regs(cpu);
        }
        else if(cmd=="r"){
            int n=0; iss>>n; if(n<=0) n=1;
            for(int i=0;i<n && !cpu.halted;i++){
                if(breakpoints.count(cpu.PC)) { std::cout<<"* Breakpoint hit at PC="<<hex16(cpu.PC)<<"\n"; break; }
                cpu.step_instr();
                if(breakpoints.count(cpu.PC)) { std::cout<<"* Breakpoint hit at PC="<<hex16(cpu.PC)<<"\n"; break; }
            }
            print_regs(cpu);
        }
        else if(cmd=="g"){
            int watchdog = 10'000'000; // safety
            while(!cpu.halted && watchdog--){
                if(breakpoints.count(cpu.PC)) { std::cout<<"* Breakpoint hit at PC="<<hex16(cpu.PC)<<"\n"; break; }
                cpu.step_instr();
            }
            print_regs(cpu);
        }
        else if(cmd=="p"){
            print_regs(cpu);
        }
        else if(cmd=="m"){
            std::string saddr; int rows=8; iss>>saddr>>rows;
            if(saddr.empty()){ std::cout<<"usage: m ADDR [ROWS]\n"; continue; }
            uint16_t addr = (uint16_t)std::stoul(saddr, nullptr, 16);
            dump_mem(cpu, addr, rows, 16);
        }
        else if(cmd=="w"){
            std::string saddr, sbyte; iss>>saddr>>sbyte;
            if(saddr.empty()||sbyte.empty()){ std::cout<<"usage: w ADDR BYTE\n"; continue; }
            uint16_t addr = (uint16_t)std::stoul(saddr, nullptr, 16);
            uint8_t  val  = (uint8_t) std::stoul(sbyte, nullptr, 16);
            std::vector<BusEvent> ev;
            cpu.mem[addr] = val;

            std::cout<<"Wrote "<<hex8(val)<<" to ["<<hex16(addr)<<"]\n";
        }
        else if(cmd=="b"){
            std::string saddr; iss>>saddr;
            if(saddr.empty()){ std::cout<<"usage: b ADDR\n"; continue; }
            uint16_t addr = (uint16_t)std::stoul(saddr, nullptr, 16);
            breakpoints.insert(addr);
            std::cout<<"Breakpoint added at PC="<<hex16(addr)<<"\n";
        }
        else if(cmd=="bl"){
            if(breakpoints.empty()) std::cout<<"(no breakpoints)\n";
            for(auto pc: breakpoints) std::cout<<" - "<<hex16(pc)<<"\n";
        }
        else if(cmd=="bc"){
            std::string saddr; iss>>saddr;
            if(saddr.empty()){ breakpoints.clear(); std::cout<<"Breakpoints cleared.\n"; }
            else {
                uint16_t addr = (uint16_t)std::stoul(saddr, nullptr, 16);
                breakpoints.erase(addr);
                std::cout<<"Cleared "<<hex16(addr)<<"\n";
            }
        }
        else if(cmd=="t"){
            int k=20; iss>>k; if(k<=0) k=20;
            print_trace(cpu, k);
        }
        else if(cmd=="reset"){
            cpu.reset(0x0000);
            cpu.timeline.clear();
            std::cout<<"Reset done.\n";
            print_regs(cpu);
        }
        else if(cmd=="sleep"){
            int ms=0; iss>>ms; if(ms>0) std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        }
        else if (cmd=="d" || cmd=="dis" || cmd=="disasm") {
            std::string saddr; int n = 16;
            iss >> saddr >> n;
            if (saddr.empty()) {
                std::cout << "usage: d <ADDR-hex> [N-instr]\n";
                continue;
            }
            uint16_t addr = (uint16_t)std::stoul(saddr, nullptr, 16);
            if (n <= 0) n = 16;
            disasm_range(cpu, addr, n);
        }        
        else if (cmd=="loadbin") {
            std::string path, saddr; iss >> path >> saddr;
            if(path.empty() || saddr.empty()){ std::cout<<"usage: loadbin <path> <addr-hex>\n"; continue; }
            uint16_t base = (uint16_t)std::stoul(saddr, nullptr, 16);
            std::vector<uint8_t> buf;
            if(!read_file_binary(path, buf)) { std::cout<<"[loadbin] failed to read '"<<path<<"'\n"; continue; }
            if(base + buf.size() > cpu.mem.size()) { std::cout<<"[loadbin] file too large for memory at "<<std::hex<<base<<"\n"; continue; }
            std::copy(buf.begin(), buf.end(), cpu.mem.begin() + base);
            std::cout<<"[loadbin] loaded "<<std::dec<<buf.size()<<" bytes at "<<std::hex<<std::setfill('0')<<std::setw(4)<<base<<"\n";
        }
        else if (cmd=="loadhex") {
            std::string path, saddr; iss >> path >> saddr;
            if(path.empty() || saddr.empty()){ std::cout<<"usage: loadhex <path> <addr-hex>\n"; continue; }
            uint16_t base = (uint16_t)std::stoul(saddr, nullptr, 16);
            std::vector<uint8_t> buf;
            if(!read_file_hexbytes(path, buf)) { std::cout<<"[loadhex] failed to parse '"<<path<<"'\n"; continue; }
            if(base + buf.size() > cpu.mem.size()) { std::cout<<"[loadhex] data too large for memory at "<<std::hex<<base<<"\n"; continue; }
            std::copy(buf.begin(), buf.end(), cpu.mem.begin() + base);
            std::cout<<"[loadhex] loaded "<<std::dec<<buf.size()<<" bytes at "<<std::hex<<std::setfill('0')<<std::setw(4)<<base<<"\n";
        }
        else if (cmd=="setrv") {
            // set reset vector (little-endian address stored at FFFC/FFFD)
            std::string saddr; iss >> saddr;
            if(saddr.empty()){ std::cout<<"usage: setrv <addr-hex>\n"; continue; }
            uint16_t start = (uint16_t)std::stoul(saddr, nullptr, 16);
            cpu.write16(0xFFFC, start);
            std::cout<<"[setrv] reset vector set to "<<std::hex<<std::setfill('0')<<std::setw(4)<<start<<"\n";
        }
        
        else {
            std::cout<<"Unknown command. Type 'help'.\n";
        }
    }

    return 0;
}
