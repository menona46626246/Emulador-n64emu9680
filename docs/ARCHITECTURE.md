# Arquitectura — n64emu

## Principios

1. **Módulos con interfaces claras** — cada bloque de hardware es una clase con `reset()`, lecturas/escrituras y step opcional.
2. **Intérprete primero** — CPU y RSP empiezan como intérpretes; dynarec es opcional (Fase 11).
3. **Sin blobs propietarios** — PIF/IPL vía archivo legal del usuario o HLE documentado.
4. **Determinismo de logs** — traces reproducibles para golden tests.
5. **Fases verificables** — cada fase tiene criterio de aceptación y tests.

## Diagrama lógico

```
┌──────────────────────────────────────────────────────────┐
│                     frontend (SDL2)                      │
│   Window · Input · ROM load · Pause/Reset · Overlay      │
└──────────────────────────┬───────────────────────────────┘
                           │
                     Emulator (core)
                           │
        ┌──────────────────┼──────────────────┐
        ▼                  ▼                  ▼
      Cpu               Scheduler            Bus
   (VR4300)          (event queue)     (mmap + MMIO)
        │                                     │
        │              ┌──────────────────────┤
        │              ▼          ▼           ▼
        │             VI         AI          PIF
        │         (video)     (audio)     (joybus)
        │              │
        │         ┌────┴────┐
        │         ▼         ▼
        │        RSP       RDP
        │      (signal)  (display)
        └─────────┴─────────┘
```

## Capas

| Capa | Responsabilidad |
|------|-----------------|
| `frontend` | OS: ventana, teclado/gamepad, menú, carga de archivos |
| `core` | Orquestación, reset, HLE boot, bucle de ciclos, scheduler |
| `cpu` | ISA MIPS III intérprete, COP0, COP1/FPU, excepciones y TLB de 32 entradas; regiones virtuales de 64 bits pendientes |
| `bus` | Mapa físico, RDRAM/cart/SP mem, MMIO (MI/PI/SI/RI/SP/DP/VI/AI), DMA, IRQ MI→CPU |
| `cart` | Header .z64, detección CIC, entrypoint fixup |
| `rcp/*` | RSP (microcódigo) + RDP (raster) |
| `vi/ai/pif` | Video, audio, PIF RAM + HLE IPL3 (`pif/boot`) |
| `common` | Tipos, logging (spdlog), versión |

## Flujo de un frame (objetivo Fase 9)

1. Frontend procesa input → actualiza `Pif` controller state.
2. `Emulator::run_cycles(budget)` ejecuta CPU.
3. CPU load/store → `Bus` → RDRAM / MMIO.
4. Writes a SP/DP lanzan tareas RSP/RDP.
5. Scheduler dispara VI interrupt → CPU toma excepción.
6. Frontend lee origen VI / framebuffer y lo presenta.

## Convenciones de código

- C++20, sin excepciones en el hot path (errores vía `bool` / códigos).
- Direcciones físicas enmascaradas a 29 bits (`Bus::physical`).
- Memoria N64 vista como **big-endian**; hosts LE hacen swap en load/store.
- Namespaces: `n64`, `n64::frontend`, `n64::log`.
- Headers públicos en `include/n64/...`, fuentes en `src/...`.

## Testing

- **Unitarios:** CPU opcodes/TLB/COP1, bus endianness, DMA, scheduler.
- **Integración:** smoke generado y homebrew legal en `roms/` (no en git), mediante manifiestos de conformidad.
- **Golden logs:** traces de PC/registros comparados byte-a-byte (Fase 1+).
