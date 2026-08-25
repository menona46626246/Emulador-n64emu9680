# Roadmap

## Completado

### Fase 0 — Bootstrap
- [x] Estructura de módulos, CMake, logging, SDL2, tests

### Fase 1 — CPU VR4300 intérprete
- [x] ISA MIPS III base, delay slots, COP0, excepciones, traces

### Fase 2 — Memory map y bus
- [x] MMIO MI/PI/SI/RI/VI/AI/DP/SP, DMA, IRQ MI→CPU

### Fase 3 — Boot mínimo
- [x] Header/CIC/TV, HLE IPL3, user PIF/IPL3, LLE-PIF opcional

### Fase 4 — Video interface básico
- [x] VI timing, FB 16/32-bit → RGBA, present SDL

### Fase 5 — Input básico
- [x] Joybus HLE, controller 1, teclado/gamepad SDL

### Fase 6 — RDP básico
- [x] FillRect, TexRect, scissor, TMEM, DP command list

### Fase 7 — RSP básico
- [x] Scalar RSP, DMA IMEM/DMEM, tasks, BREAK

### Fase 8 — Audio básico
- [x] AI DMA + ring + SDL output

### Fase 9 — Timing
- [x] run_frame, NTSC/PAL budgets, host frame pacing

### Fase 10 — Compatibilidad / debug
- [x] Debugger: breakpoints PC, watchpoints R/W, single-step
- [x] CPU instruction trace ring + dump a archivo
- [x] Log de MMIO desconocido (agregado por dirección)
- [x] Overlay de texto en ventana (fuente 5×7) + título de estado
- [x] CLI: `--debug`, `--trace`, `--trace-dump`, `--break HEX`
- [x] Teclas F1–F9 (overlay, pause, step, resume, trace, dump)
- [x] Tests unitarios del debugger

### Fase 11 — Optimización
- [x] Bus fast-path: lecturas/escrituras 16/32/64-bit con bswap nativo a RDRAM/SP
- [x] VI LUT: tabla estática 65536 entradas RGBA5551 → RGBA8888 en `copy_framebuffer_rgba`
- [x] Audio SPSC lock-free: ring buffer sin mutex con `std::atomic` (productor/consumidor)
- [x] Tests unitarios de optimización (BusFastPath, ViLut, AiSpsc, benchmark)

### Fase 12 — Optimización avanzada
- [x] CPU Basic Block Cache: predecodificación y ejecución de bloques básicos en VR4300
- [x] Audio thread dedicado: SDL2 Audio Callback en hilo de alta prioridad con SPSC lock-free
- [x] Adaptive Frame Pacing: temporizador híbrido (sleep grueso + spin-wait fino submilisegundo)
- [x] Tests unitarios de Block Cache (compilación, paridad de ejecución, invalidación)

## No-objetivos (por ahora)

- Compatibilidad comercial completa
- Dynarec obligatorio
- Dear ImGui completo (overlay propio basta por ahora)
- Distribución de firmware propietario
