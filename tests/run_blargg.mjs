// Ejecuta ROMs de prueba de Blargg y captura la salida serie
import { readFileSync } from 'node:fs';
import { GameBoy } from '../js/gameboy.js';

const romPath = process.argv[2];
const maxSeconds = Number(process.argv[3] || 60); // segundos emulados

const rom = new Uint8Array(readFileSync(romPath));
const gb = new GameBoy();

let output = '';
gb.mmu.serialOut = (b) => {
  output += String.fromCharCode(b);
  process.stdout.write(String.fromCharCode(b));
};

gb.loadROM(rom);

const start = Date.now();
const CYCLES_PER_SEC = 4194304;
for (let s = 0; s < maxSeconds; s++) {
  gb.runCycles(CYCLES_PER_SEC);
  if (/Passed|Failed/.test(output)) break;
}
const elapsed = ((Date.now() - start) / 1000).toFixed(1);
console.log(`\n--- tiempo real: ${elapsed}s ---`);
process.exit(/Passed/.test(output) && !/Failed/.test(output) ? 0 : 1);
