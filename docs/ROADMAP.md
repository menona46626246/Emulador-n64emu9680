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

### Fase 13 — Confiabilidad, compatibilidad y memoria virtual
- [x] CI en Windows/MSVC y Linux/GCC, más ASan+UBSan con Clang
- [x] Banco headless reproducible con manifiesto, hash SHA-256 e informe JSON
- [x] ROM smoke original generada dentro de la CI; ninguna ROM se distribuye
- [x] TLB VR4300 de 32 entradas: ASID/global, PageMask, V/D y páginas pares/impares
- [x] Operaciones COP0 `TLBR`, `TLBWI`, `TLBWR` y `TLBP`
- [x] Excepciones refill/invalid/modified, actualización de `BadVAddr`, `Context` y `EntryHi`
- [x] Invalidación del Basic Block Cache ante cambios de traducción
- [ ] Ampliar el manifiesto local con homebrew externo que el usuario pueda usar legalmente

### Fase 14 — COP1/FPU
- [x] Registros FGR/FCR, modos `Status.FR=0/1` y transferencias/memoria COP1
- [x] Aritmética S/D, conversiones W/L/S/D, comparaciones y ramas `BC1*`
- [x] Excepciones IEEE-754, causa/enable/flags, redondeo y flush mediante `FCR31`
- [x] Pruebas diferenciales, casos extremos y paridad con Basic Block Cache

### Fase 15 — RSP vectorial
- [x] Registros COP2, acumuladores de 48 bits y banderas `VCO/VCC/VCE`
- [x] ALU, multiplicación/acumulación, clip, lógica y unidad recíproca
- [x] Memoria vectorial normal, packed y transpose
- [x] Microcódigo IMEM reproducible y validación con AddressSanitizer

## Próximas fases

### Fase 16 — RDP avanzado
- [ ] Triángulos, coverage, blender y filtrado bilinear
- [ ] Golden images deterministas por comando y escena

## No-objetivos (por ahora)

- Compatibilidad comercial completa
- Dynarec obligatorio
- Dear ImGui completo (overlay propio basta por ahora)
- Distribución de firmware propietario
