# µTrace — A Virtual CPU for Learning

µTrace (Micro-Trace) is a **virtual CPU** with a step-by-step microinstruction tracer.  
It is designed as an educational tool to help understand **how CPUs work at a low level** — covering the fetch/decode/execute cycle, memory access, microstates, and even concepts like context switching.

---

## Features
- Simple **virtual CPU core** with registers (A, B, X, PC, SP, FLAGS).
- Implements **microstates** (`FetchOp`, `Decode`, `Execute`, etc.) for cycle-by-cycle tracing.
- Command-line interface (CLI) with an interactive monitor.
- Debug commands for inspecting memory, registers, breakpoints, and execution history.
- Tracer (`t`) command shows detailed micro-operations and bus events (reads/writes).
- Example demo program included (`demo_program.cpp`).

---

## Building

### Requirements
- C++17 or newer
- CMake ≥ 3.16
- SDL2 *(optional, only if you want the GUI build)*

### CLI-only build
```bash
./run_cli.sh
