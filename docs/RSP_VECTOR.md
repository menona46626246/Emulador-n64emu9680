# RSP vectorial / COP2

La Fase 15 sustituye el antiguo stub COP2 por un intérprete funcional de la
unidad vectorial del RSP. El estado implementado contiene 32 registros de
128 bits, ocho acumuladores con envoltura exacta a 48 bits y los registros de
control `VCO`, `VCC` y `VCE`.

## Instrucciones cubiertas

- Transferencias COP2 `MFC2`, `MTC2`, `CFC2` y `CTC2`, incluida la
  direccionabilidad big-endian por byte y la extensión de signo.
- Selección de elementos para los 16 valores del campo `e`, incluidas las
  formas por cuadrante, half y broadcast.
- Multiplicación y acumulación: `VMULF/U/Q`, `VMUDL/M/N/H`, `VMACF/U/Q`,
  `VMADL/M/N/H` y `VRNDP/N`.
- ALU y acumulador: `VADD`, `VSUB`, `VABS`, `VADDC`, `VSUBC` y `VSAR`.
- Comparación/clip: `VLT`, `VEQ`, `VNE`, `VGE`, `VCL`, `VCH`, `VCR` y
  `VMRG`, con sus efectos sobre `VCO`, `VCC` y `VCE`.
- Lógica: `VAND`, `VNAND`, `VOR`, `VNOR`, `VXOR` y `VNXOR`.
- División: `VRCP`, `VRCPL`, `VRCPH`, `VRSQ`, `VRSQL`, `VRSQH`, `VMOV` y
  `VNOP`. La ROM de estimación se reconstruye con aritmética entera, sin
  depender del punto flotante del host.
- Memoria normal `BV/SV/LV/DV/QV/RV`, empaquetada `PV/UV/HV/FV`, `SWV` y
  transpuesta `LTV/STV`; los offsets de siete bits se extienden con signo y
  se escalan según el formato.

Los stores vectoriales actualizan DMEM y la ventana del bus inmediatamente,
igual que los stores escalares. Los helpers de `rsp/insn.hpp` permiten crear
microcódigos de prueba sin introducir un ensamblador externo.

## Validación

`tests/rsp/test_rsp_vector.cpp` cubre estado, transferencias, endianidad,
selección de elementos, saturación, banderas, acumulador de 48 bits,
cuantización, comparaciones, ROM recíproca, cargas/stores y transposición.
También ejecuta desde IMEM un kernel reproducible que carga ocho muestras,
aplica una multiplicación Q15 y escribe el resultado en DMEM.

La suite completa contiene 244 pruebas en la configuración normal. La variante
MSVC con AddressSanitizer y frontend desactivado ejecuta 240 pruebas; ambas
terminan sin fallos ni diagnósticos de memoria.

## Límites conocidos

El intérprete aplica el resultado arquitectónico al retirar cada instrucción;
no modela los hazards ni las latencias internas de los pipelines vectorial y
de memoria. Los microcódigos que respetan las separaciones exigidas por el SDK
observan el resultado correcto, pero la ejecución ciclo a ciclo contra código
que dependa deliberadamente de un hazard sigue registrada como U006.

Las codificaciones reservadas se registran y no modifican estado. Las variantes
con elementos ilegales de las transferencias packed/transpose no se inventan:
se rechazan o conservan únicamente el comportamiento común documentado hasta
que exista una prueba de hardware reproducible.

Fuente primaria: *RSP Programmer's Guide, Version 1.1* de Silicon Graphics,
disponible en el
[archivo público de la guía](https://bukosek.si/hardware/collection/sgi-o2/n64-rsp-programmers-guide.pdf).
