# n64emu

Emulador de Nintendo 64 modular, orientado a **homebrew legal**, escrito en **C++20**.

> **Aviso legal:** este proyecto no incluye, no descarga y no distribuye ROMs comerciales ni firmware/PIF/IPL propietario de Nintendo. Solo usa documentación pública, ingeniería inversa limpia y pruebas propias. Coloca homebrew o dumps legales tuyos en `roms/` (ignorado por git).

## Estado

| Fase | Descripción                         | Estado      |
|------|-------------------------------------|-------------|
| 0    | Bootstrap, build, ventana, tests    | **Completada** |
| 1    | CPU VR4300 intérprete               | **Completada** |
| 2    | Memory map y bus                    | **Completada** |
| 3    | Boot mínimo / PIF HLE + user IPL    | **Completada** |
| 4    | Video Interface básico              | **Completada** |
| 5    | Input básico (SI/PIF/joybus)        | **Completada** |
| 6    | RDP (FillRect + TexRect/TMEM)       | **Completada** |
| 7    | RSP scalar + tasks                  | **Completada** |
| 8    | Audio básico (AI + SDL)             | **Completada** |
| 9    | Timing / frame pacing               | **Completada** |
| 10   | Debugger / overlay / traces         | **Completada** |
| 11   | Optimización                        | **Completada** |
| 12   | Optimización avanzada               | **Completada** |
| 13   | CI, banco de compatibilidad y TLB   | **Base completada** |
| 14   | COP1/FPU VR4300                     | **Completada** |
| 15   | RSP vectorial / COP2                | **Completada** |

## Requisitos

- CMake ≥ 3.16
- Compilador C++20 (GCC 12+, Clang 15+, MSVC 2022)
- SDL2 (desarrollo)
- Opcional: Ninja

### Dependencias en Debian/Ubuntu

```bash
sudo apt install build-essential cmake ninja-build libsdl2-dev
```

### SDL2 local (si no hay paquete del sistema)

```bash
# Ejemplo de prefijo usado en CI headless:
export CMAKE_PREFIX_PATH=/path/to/sdl2-install
```

## Compilar

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH:-} \
      -DN64EMU_BUILD_TESTS=ON
cmake --build build -j$(nproc)
```

Headless / sin display (CI):

```bash
cmake -S . -B build -DN64EMU_HEADLESS=ON -DCMAKE_PREFIX_PATH=/path/to/sdl2-install
cmake --build build -j$(nproc)
```

## Ejecutar

```bash
# Ventana vacía (cierra con Escape o el botón de cerrar)
./build/n64emu

# Smoke headless (cierra solo a los 500 ms)
./build/n64emu --headless --auto-close 500

# Con homebrew legal
./build/n64emu --rom roms/mi_homebrew.z64

./build/n64emu --help
```

## Tests

```bash
cmake --build build --target check
# o:
cd build && ctest --output-on-failure
```

Banco de compatibilidad reproducible:

```bash
python scripts/generate_smoke_rom.py --output build/conformance/n64emu_smoke.z64
python scripts/run_conformance.py --emulator build/n64emu \
  --manifest tests/conformance/manifest.json \
  --report build/conformance/report.json
```

## Estructura

```
include/n64/     Headers públicos por módulo
src/             Implementaciones
  core/          Emulator + scheduler
  cpu/           VR4300
  bus/           Mapa de memoria / DMA
  rcp/rsp|rdp/   Reality co-processors
  vi/ ai/ pif/   Interfaces de video, audio, PIF
  frontend/      SDL2 ventana + app
  common/        Logging, tipos, versión
tests/           GoogleTest
docs/            Arquitectura, incógnitas, roadmap
third_party/     spdlog, googletest (vendored)
roms/            Homebrew del usuario (gitignored)
```

## Documentación

- [Arquitectura](docs/ARCHITECTURE.md)
- [Incógnitas de hardware](docs/UNKNOWNS.md)
- [Roadmap](docs/ROADMAP.md)
- [Homebrews probados](docs/HOMEBREW.md)
- [Banco de compatibilidad](docs/CONFORMANCE.md)
- [COP1/FPU](docs/COP1_FPU.md)
- [RSP vectorial/COP2](docs/RSP_VECTOR.md)

## Licencia del código

Código original de este repositorio: ver `LICENSE` (MIT).

Third-party: spdlog (MIT), GoogleTest (BSD-3), SDL2 (zlib) — respetar sus licencias.
