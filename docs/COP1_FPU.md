# COP1/FPU del VR4300

La implementación COP1 mantiene 32 registros físicos de 64 bits y respeta los
dos modos definidos por `Status.FR`:

- `FR=1`: cada FPR referencia un FGR completo de 64 bits.
- `FR=0`: las operaciones dobles usan pares; el registro par contiene la
  palabra menos significativa y el impar adyacente la más significativa.

## Instrucciones cubiertas

- Transferencias `MFC1`, `DMFC1`, `CFC1`, `MTC1`, `DMTC1` y `CTC1`.
- Memoria `LWC1`, `LDC1`, `SWC1` y `SDC1`.
- Aritmética S/D: `ADD`, `SUB`, `MUL`, `DIV`, `SQRT`, `ABS`, `MOV` y `NEG`.
- Conversiones entre S, D, W y L; variantes `ROUND`, `TRUNC`, `CEIL` y
  `FLOOR` hacia W/L.
- Las 16 condiciones de comparación y ramas `BC1F`, `BC1T`, `BC1FL` y
  `BC1TL`, incluidos delay slots y anulación de ramas likely.

## Control y excepciones

`FCR0` expone el identificador de implementación `0x0B`. `FCR31` implementa
FS, condición, causas, enables, flags adhesivos y los cuatro modos de redondeo.
Una causa habilitada produce `ExcCode=15` sin modificar el destino ni los
flags; una operación no implementada siempre activa la causa E y genera la
misma excepción.

Los operandos denormalizados y quiet NaN solicitan emulación por software en
operaciones aritméticas/conversiones, mientras que `MOV` conserva sus bits y
las comparaciones los aceptan. Los underflows siguen la política FS del
VR4300: sin flush generan E; con FS y U/I deshabilitados se entregan cero o el
mínimo normal según el modo de redondeo y se registran U+I.

## Validación y límites

`tests/cpu/test_cop1.cpp` cubre estado, memoria, formatos FR, aritmética,
conversiones, comparaciones, ramas, excepciones habilitadas/deshabilitadas,
NaN, overflow, underflow y ejecución mediante Basic Block Cache. También
contrasta una matriz de operaciones finitas con la implementación IEEE-754 del
host.

El cálculo finito utiliza el entorno de punto flotante de C++ y restaura su
estado después de cada instrucción. Las reglas visibles del VR4300 se aplican
explícitamente, pero la paridad bit a bit en casos límite entre arquitecturas y
la temporización del pipeline todavía requieren pruebas contra hardware real;
se registran como U010 en `docs/UNKNOWNS.md`.

Fuente primaria: *NEC VR4300 MIPS RISC Microprocessor User's Manual* (1995),
capítulos 5 y 6, disponible en el
[archivo de Bitsavers de Internet Archive](https://archive.org/details/bitsavers_necmips199icroprocessorUsersManual_15736077).
