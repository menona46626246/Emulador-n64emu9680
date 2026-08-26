# Homebrew probados

Solo se documentan imágenes **legales** (homebrew, demos open-source, o ROMs construidas por el usuario).

| Nombre | Fuente | Fase mínima | Resultado | Notas |
|--------|--------|-------------|-----------|-------|
| `hello_hle.z64` | Generado en tests / `roms/` (sintético) | 3 | Boot OK, ejecuta payload, escribe RDRAM | Sin IPL3 propietario (stub cero → CIC 6102 HLE) |
| Suite `BootHle.*` | `tests/core/test_boot.cpp` | 3 | 7/7 pass | ROMs construidas en memoria |
| `vi_solid_red.z64` | `roms/` (sintético) | 4 | Configura VI, rellena FB 320×240 rojo 16-bit, presenta | CPU loop fill + VI scanout |
| Suite `Vi*.*` | `tests/vi/test_vi.cpp` | 4 | 10/10 pass | 5551/8888, checkerboard, timing, IRQ |
| Suite `Joybus*.*` | `tests/pif/test_input.cpp` | 5 | 8/8 pass | Status/Info/SI DMA/CPU poll |
| `rdp_rects.z64` | `roms/` (sintético) | 6 | FillRect azul/rojo/verde + VI present | DP command list + CPU kick |
| `rdp_texrect.z64` | `roms/` (sintético) | 6 | Checker textura 16×16 escalada 4× sobre fondo | LoadTile + TexRect |
| Suite `Rdp*.*` / `RdpTex.*` | `tests/rdp/test_rdp.cpp` | 6 | 11/11 pass | FillRect, scissor, TexRect, LoadBlock/Tile |
| Suite `Rsp*.*` | `tests/rsp/test_rsp.cpp` | 7 | 9/9 pass | scalar, BREAK, DMA task, sum DMEM |
| `ai_beep.z64` | `roms/` (sintético) | 8 | DMA AI 440 Hz square ~16 kHz | 0.25 s buffer |
| Suite `Ai*.*` | `tests/ai/test_ai.cpp` | 8 | 9/9 pass | rate, DMA, queue, tick, pull |
| Suite `Debugger.*` | `tests/debug/test_debugger.cpp` | 10 | 9/9 pass | BP, step, watch, trace dump |
| `n64emu_smoke.z64` | Generada por `scripts/generate_smoke_rom.py` | 13 | Pass headless | Hash fijo y ejecución automática en CI |
| Suite `CpuTlb.*` | `tests/cpu/test_tlb.cpp` | 13 | 16/16 pass | traducción, ASID, PageMask, excepciones y caché |
| Suite `CpuCop1.*` | `tests/cpu/test_cop1.cpp` | 14 | 28/28 pass | FGR/FCR, S/D/W/L, ramas, redondeo y excepciones |

## Cómo añadir una entrada

1. Construir o descargar homebrew desde su autor (licencia permisiva).
2. Colocar el `.z64` en `roms/` (gitignored).
3. Añadir un caso a un manifiesto local siguiendo `docs/CONFORMANCE.md`.
4. Ejecutar `scripts/run_conformance.py` y revisar su informe JSON.
5. Anotar comportamiento y actualizar esta tabla solo si la prueba es reproducible.

## Fuentes sugeridas (para fases futuras)

- Plantillas libdragon / libnustd de la comunidad
- Demos de homebrew publicados por sus autores con permiso de redistribución
- Roms de prueba **generadas por nuestros propios tests** (ensamblador MIPS en `tests/`)
