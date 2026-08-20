# 🎮 Emulador de Game Boy (DMG)

Emulador de Game Boy funcional escrito en JavaScript puro, sin dependencias.
Corre en el navegador: carga cualquier ROM `.gb` y juega.

## Características

- **CPU Sharp SM83** completa (los 500 opcodes, interrupciones, HALT/halt-bug, EI retardado)
  - ✅ Pasa `cpu_instrs` de Blargg (los 11 tests)
  - ✅ Pasa `instr_timing` de Blargg
- **PPU** con renderizado por línea: fondo, ventana, sprites 8×8/8×16, prioridades DMG,
  modos LCD con STAT/LYC e interrupciones
- **APU** con los 4 canales (2 pulsos con barrido/envolvente, onda, ruido LFSR),
  secuenciador de frames a 512 Hz y salida estéreo vía Web Audio
- **Mappers**: ROM plano, MBC1, MBC2, MBC3 (+RTC básico), MBC5
- **Timers** (DIV/TIMA/TMA/TAC), DMA de OAM, joypad con interrupción
- **Guardado con batería**: la RAM externa se conserva en `localStorage` automáticamente
- Extras: pausa, reinicio, turbo, silencio, 3 paletas (verde clásico / gris / ámbar),
  controles táctiles para móvil, arrastrar y soltar ROMs

## Cómo usarlo

```bash
python3 -m http.server 8000
# abre http://localhost:8000
```

Carga tu ROM con el botón «Cargar ROM» o arrástrala a la pantalla.
Se incluyen dos juegos homebrew libres para probar al instante:

| Juego | Licencia |
|---|---|
| 🐰 Tobu Tobu Girl (Tangram Games) | MIT / CC-BY 4.0 |
| 🔢 2048-gb (Sanqui) | zlib |

## Controles

| Tecla | Función |
|---|---|
| ← ↑ → ↓ | Cruceta |
| X / Z | A / B |
| Enter | Start |
| Shift | Select |
| Espacio (mantener) | Turbo |
| P | Pausa |

También hay botones táctiles en pantalla para jugar desde el móvil.

## Pruebas

```bash
npm test                                      # cpu_instrs de Blargg
node tests/run_blargg.mjs tests/roms/instr_timing.gb
node tests/render_frame.mjs roms/2048.gb 200 captura.png   # captura de pantalla headless
```

## Estructura

```
js/
  cpu.js      CPU SM83 (opcodes, flags, interrupciones)
  mmu.js      Bus de memoria + cartucho/MBCs
  ppu.js      Video (scanline renderer)
  apu.js      Sonido (4 canales)
  timer.js    DIV/TIMA
  joypad.js   Controles
  gameboy.js  Integración de la consola
  main.js     Interfaz web
tests/        Arnés de pruebas headless (Node) + ROMs de test de Blargg
roms/         Juegos homebrew libres incluidos
```

*Este proyecto es solo para fines educativos. Game Boy es una marca de Nintendo.
Usa únicamente ROMs de las que poseas los derechos.*
