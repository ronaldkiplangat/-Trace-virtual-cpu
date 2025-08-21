// gui/app.cpp
// Minimal SDL2 + Dear ImGui viewer for the CPU core (SDL_Renderer2 backend)
#include <filesystem>
#include <cstdio>
#include <vector>
#include <string>
#include <cstdint>
#include <algorithm>

#include <SDL.h>

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"  // SDL2 renderer v2 backend

#include "cpu.hpp"   // your core header

// Forward declaration — implemented in src/demo_program.cpp
extern std::vector<uint8_t> demo_program();

static const char* microStateName(MicroState s) {
    switch (s) {
        case MicroState::FetchOp:   return "FetchOp";
        case MicroState::FetchOpLo: return "FetchOpLo";
        case MicroState::FetchOpHi: return "FetchOpHi";
        case MicroState::Decode:    return "Decode";
        case MicroState::Execute:   return "Execute";
        case MicroState::WriteBack: return "WriteBack";
        case MicroState::MemRead:   return "MemRead";
        case MicroState::MemWrite:  return "MemWrite";
        case MicroState::Halted:    return "Halted";
    }
    return "?";
}

// Helper: give windows an initial position/size (first run only).
static inline void PlaceFirstUse(const ImVec2& pos, const ImVec2& size) {
    ImGui::SetNextWindowPos(pos, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(size, ImGuiCond_FirstUseEver);
}

static void memoryHexView(uint8_t* mem, size_t size, uint16_t start, int rows = 16, int cols = 16) {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 2));
    for (int r = 0; r < rows; ++r) {
        uint32_t base = static_cast<uint32_t>(start) + static_cast<uint32_t>(r) * cols;
        if (base >= size) break;
        ImGui::Text("%04X:", (unsigned)base);
        ImGui::SameLine();
        for (int c = 0; c < cols; ++c) {
            uint32_t addr = base + (uint32_t)c;
            if (addr >= size) break;
            ImGui::Text("%02X", mem[addr]);
            if (c != cols - 1) ImGui::SameLine();
        }
    }
    ImGui::PopStyleVar();
}

int main(int, char**) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0) {
        std::fprintf(stderr, "SDL Error: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Minimal CPU Simulator",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1800, 1100,   // big, readable default; you can tweak
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) { std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError()); SDL_Quit(); return 1; }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) { std::fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError()); SDL_DestroyWindow(window); SDL_Quit(); return 1; }

    // --- ImGui init ---
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    // HiDPI / Retina scaling
    io.FontGlobalScale = 2.0f;     // adjust (1.5f–2.5f) to taste

    // Try to load a local font next to the exe; otherwise use a larger default font
    {
        const char* kFont = "Roboto-Medium.ttf"; // copied via CMake (optional)
        if (std::filesystem::exists(kFont)) {
            io.Fonts->AddFontFromFileTTF(kFont, 24.0f);
        } else {
            ImFontConfig cfg; cfg.SizePixels = 22.0f;
            io.Fonts->AddFontDefault(&cfg);
        }
    }

    ImGui::StyleColorsDark();
    ImGui::GetStyle().ScaleAllSizes(1.2f); // spacing feels better with big fonts

    if (!ImGui_ImplSDL2_InitForSDLRenderer(window, renderer)) {
        std::fprintf(stderr, "ImGui_ImplSDL2_InitForSDLRenderer failed\n");
        SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit(); return 1;
    }
    if (!ImGui_ImplSDLRenderer2_Init(renderer)) {
        std::fprintf(stderr, "ImGui_ImplSDLRenderer2_Init failed\n");
        ImGui_ImplSDL2_Shutdown(); SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit(); return 1;
    }

    // --- CPU init ---
    CPU cpu;
    cpu.write16(0xFFFC, 0x0000);
    cpu.load_program(demo_program(), 0x0000);
    cpu.reset(0x0000);

    bool running = true;
    bool autoRun = false;
    int  instrPerFrame = 1;
    uint16_t memBase = 0x0000, ioBase = 0xFF00;

    std::vector<uint8_t> outLog; outLog.reserve(1024);
    auto stepAuto = [&](CPU& c){ for (int i=0; i<instrPerFrame && !c.halted; ++i) c.step_instr(); };

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_WINDOWEVENT &&
                event.window.event == SDL_WINDOWEVENT_CLOSE &&
                event.window.windowID == SDL_GetWindowID(window)) running = false;
        }

        if (autoRun && !cpu.halted) stepAuto(cpu);

        // Capture OUT0 writes
        if (!cpu.timeline.empty()) {
            const auto& tf = cpu.timeline.back();
            for (const auto& e : tf.events)
                if (e.dir == BusDir::Write && e.address == 0xFF00) outLog.push_back(e.data);
        }

        ImGui_ImplSDL2_NewFrame();
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui::NewFrame();

        // ---- Controls ----
        PlaceFirstUse({20,20},   {1000,180});
        ImGui::Begin("Controls");
        ImGui::Text("PC:%04X  A:%02X B:%02X X:%02X  F:%02X  cyc:%llu",
                    cpu.PC, cpu.A, cpu.B, cpu.X, cpu.FLAGS, (unsigned long long)cpu.cycles);
        ImGui::Text("u-state: %s", microStateName(cpu.ustate));
        if (ImGui::Button("Step Cycle")) { cpu.step_cycle(); } ImGui::SameLine();
        if (ImGui::Button("Step Instr")) { cpu.step_instr(); } ImGui::SameLine();
        ImGui::Checkbox("Run", &autoRun); ImGui::SameLine();
        ImGui::SetNextItemWidth(140); ImGui::InputInt("instr/frame", &instrPerFrame);
        if (instrPerFrame < 1) instrPerFrame = 1;
        ImGui::SameLine();
        if (ImGui::Button("Reset")) { cpu.reset(0x0000); outLog.clear(); }
        ImGui::End();

        // ---- Registers & Flags ----
        PlaceFirstUse({20,220},  {1000,220});
        ImGui::Begin("Registers & Flags");
        ImGui::Text("A:%02X  B:%02X  X:%02X", cpu.A, cpu.B, cpu.X);
        ImGui::Text("PC:%04X  SP:%04X", cpu.PC, cpu.SP);
        bool C = cpu.FLAGS & 0x01, Z = cpu.FLAGS & 0x02, N = cpu.FLAGS & 0x04, V = cpu.FLAGS & 0x08;
        ImGui::Separator(); ImGui::Text("Flags (read-only)");
        ImGui::Checkbox("C", &C); ImGui::SameLine();
        ImGui::Checkbox("Z", &Z); ImGui::SameLine();
        ImGui::Checkbox("N", &N); ImGui::SameLine();
        ImGui::Checkbox("V", &V);
        ImGui::End();

        // ---- Memory ----
        PlaceFirstUse({1040,20}, {720,720});
        ImGui::Begin("Memory");
        static char baseBuf[8] = "0000";
        ImGui::SetNextItemWidth(180);
        if (ImGui::InputText("Base (hex)", baseBuf, IM_ARRAYSIZE(baseBuf),
            ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsNoBlank)) {
            unsigned v = 0; if (std::sscanf(baseBuf, "%x", &v) == 1) memBase = static_cast<uint16_t>(v & 0xFFFF);
        }
        ImGui::BeginChild("hex", ImVec2(0, 420), true);
        memoryHexView(cpu.mem.data(), cpu.mem.size(), memBase, 16, 16);
        ImGui::EndChild();
        ImGui::Separator(); ImGui::Text("I/O (0xFF00..)");
        ImGui::BeginChild("iohex", ImVec2(0, 180), true);
        memoryHexView(cpu.mem.data(), cpu.mem.size(), ioBase, 2, 16);
        ImGui::EndChild();
        ImGui::End();

        PlaceFirstUse({1040,760}, {720,280});
        ImGui::Begin("Timeline");
        static int maxRows = 256; ImGui::SliderInt("Rows", &maxRows, 64, 2000);
        int total = static_cast<int>(cpu.timeline.size());
        int start = std::max(0, total - maxRows);
        ImGui::BeginChild("tl", ImVec2(0, 220), true);
        for (int i = start; i < total; ++i) {
            const auto& t = cpu.timeline[i];
            ImGui::Text("#%llu PC=%04X OP=%02X A=%02X B=%02X X=%02X F=%02X %s ev=%zu",
                (unsigned long long)t.cycle, t.pc, t.opcode, t.a, t.b, t.x, t.flags,
                microStateName(t.state), t.events.size());
            if (ImGui::IsItemClicked())
                for (const auto& e : t.events)
                    ImGui::BulletText("%s [%04X] = %02X  %s",
                        (e.dir == BusDir::Read ? "RD" : "WR"), e.address, e.data, e.note.c_str());
        }
        ImGui::EndChild();
        ImGui::End();

        // ---- OUT0 log ----
        PlaceFirstUse({20,460},  {1000,320});
        ImGui::Begin("OUT0 Log (writes to 0xFF00)");
        ImGui::BeginChild("out", ImVec2(0, 220), true);
        for (size_t i = 0; i < outLog.size(); ++i) {
            ImGui::Text("%02X", outLog[i]);
            if (i + 1 < outLog.size()) ImGui::SameLine();
        }
        ImGui::EndChild();
        if (ImGui::Button("Clear")) outLog.clear();
        ImGui::End();

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 25, 25, 25, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
