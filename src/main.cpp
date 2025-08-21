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
        else {
            std::cout<<"Unknown command. Type 'help'.\n";
        }
    }

    return 0;
}
