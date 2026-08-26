# Banco de compatibilidad

El banco ejecuta ROMs legales en modo headless, limita el tiempo de cada caso y
genera un informe JSON. No distribuye ROMs de terceros ni considera que un
proceso sin crash equivalga por sí solo a emulación correcta; los resultados se
complementan con las pruebas unitarias y, cuando corresponda, trazas o imágenes
de referencia.

## Smoke reproducible del repositorio

La CI genera una ROM mínima original y comprueba su hash antes de ejecutarla:

```bash
python scripts/generate_smoke_rom.py --output build/conformance/n64emu_smoke.z64
python scripts/run_conformance.py \
  --emulator build/n64emu \
  --manifest tests/conformance/manifest.json \
  --report build/conformance/report.json
```

En Windows con un generador multiconfiguración, usa
`build/RelWithDebInfo/n64emu.exe` como ejecutable.

## Añadir homebrew propio o redistribuible

1. Coloca las ROMs en `roms/`; ese directorio está ignorado por git.
2. Crea `roms/conformance.json` con `schema_version: 1`.
3. Añade un caso por ROM. Las rutas son relativas a la raíz del repositorio.
4. Ejecuta el runner y conserva el informe fuera del control de versiones.

Ejemplo:

```json
{
  "schema_version": 1,
  "defaults": {
    "max_runtime_ms": 2000,
    "expected_exit_code": 0
  },
  "cases": [
    {
      "name": "mi-demo-legal",
      "rom": "roms/mi-demo.z64",
      "sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
      "required_output": ["Booted"]
    }
  ]
}
```

Campos opcionales por caso:

- `sha256`: fija exactamente la imagen probada.
- `max_runtime_ms`: tiempo emulado antes del cierre automático.
- `expected_exit_code`: código esperado del proceso; por defecto es `0`.
- `required_output`: textos que deben aparecer en stdout o stderr.
- `args`: argumentos adicionales enviados directamente al emulador.

El runner devuelve `0` solo si todos los casos pasan, `1` si falla algún caso y
`2` si el ejecutable o el manifiesto no son válidos.
