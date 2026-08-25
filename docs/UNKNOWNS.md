# Incógnitas de hardware

Detalles no asumidos como hechos. Cada entrada debe resolverse con documentación pública o un experimento reproducible **antes** de codificar comportamiento crítico.

| ID | Área | Descripción | Experimento propuesto | Estado |
|----|------|-------------|----------------------|--------|
| U001 | Timing | Relación exacta de reloj VR4300 ↔ RCP (divisores NTSC/PAL) | Fase 9: presupuestos NTSC 60/PAL 50 y batch VI/AI. Falta ratio RCP exacto y sync por eventos del scheduler | Parcial |
| U002 | Bus | Valor exacto de open-bus por dominio (RDRAM, cart, MMIO) | Lecturas a regiones no mapeadas desde homebrew; loggear patrones | Abierta — usamos `(addr>>8)&0xFF` provisional |
| U003 | PIF | Secuencia mínima HLE de boot compatible con libdragon/homebrew modernos | HLE IPL3 (Fase 3) + seeds os*/CIC/TV + carga PIF/IPL3 user-owned + LLE-PIF opcional. Falta paridad byte-exacta de GPRs post-IPL3 vs hardware y CIC challenge 6105 | Parcial |
| U004 | VI | Momento exacto de latched `VI_CURRENT` e interrupt | Fase 4: step de half-lines + fire en igualdad con V_INTR; falta paridad exacta NTSC/PAL y serrate | Parcial |
| U005 | RDP | Precisión de coverage/blending/filtrado/triángulos | Fase 6+: FillRect + TexRect point-sample + TMEM OK; falta bilinear, blender cycles, coverage y triángulos | Parcial |
| U006 | RSP | Hazards de vector pipeline / acumulación | Fase 7: scalar OK; COP2 es NOP. Falta VU + hazards reales | Parcial |
| U007 | AI | FIFO depth exacta y relación DACRATE↔reloj | Fase 8: 1 DMA + 1 queued, rate=DAC_CLOCK/(dac+1), tick por CPU cycle. Falta paridad exacta de underrun/FIFO HW | Parcial |
| U008 | TLB | Comportamiento de wired entries y random en cold boot | Lectura COP0 post-reset en homebrew | Abierta |

## Regla

Si un comportamiento no está en esta lista ni en un test, **no inventarlo**. Añadir una fila aquí y un test que falle hasta conocer la respuesta.
