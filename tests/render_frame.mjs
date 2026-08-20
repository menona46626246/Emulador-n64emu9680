// Renderiza N frames de una ROM y guarda la pantalla como PNG (para verificar la PPU)
import { readFileSync, writeFileSync } from 'node:fs';
import { deflateSync } from 'node:zlib';
import { GameBoy } from '../js/gameboy.js';

const romPath = process.argv[2];
const frames = Number(process.argv[3] || 300);
const outPath = process.argv[4] || 'screen.png';
// pulsaciones opcionales: "frame:tecla,frame:tecla"
const presses = (process.argv[5] || '').split(',').filter(Boolean).map(s => {
  const [f, k] = s.split(':');
  return { frame: Number(f), key: k };
});

const gb = new GameBoy();
gb.loadROM(new Uint8Array(readFileSync(romPath)));

for (let i = 0; i < frames; i++) {
  for (const p of presses) {
    if (i === p.frame) gb.joypad.press(p.key);
    if (i === p.frame + 5) gb.joypad.release(p.key);
  }
  gb.runFrame();
}

// PNG minimo (RGBA sin filtros)
function crc32(buf) {
  let c, table = [];
  for (let n = 0; n < 256; n++) {
    c = n;
    for (let k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320 ^ (c >>> 1) : c >>> 1;
    table[n] = c >>> 0;
  }
  let crc = 0xFFFFFFFF;
  for (const b of buf) crc = table[(crc ^ b) & 0xFF] ^ (crc >>> 8);
  return (crc ^ 0xFFFFFFFF) >>> 0;
}
function chunk(type, data) {
  const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
  const t = Buffer.from(type, 'ascii');
  const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(Buffer.concat([t, data])));
  return Buffer.concat([len, t, data, crc]);
}
const W = 160, H = 144;
const ihdr = Buffer.alloc(13);
ihdr.writeUInt32BE(W, 0); ihdr.writeUInt32BE(H, 4);
ihdr[8] = 8; ihdr[9] = 6; // 8 bits, RGBA
const raw = Buffer.alloc(H * (1 + W * 4));
const fb = gb.ppu.framebuffer;
for (let y = 0; y < H; y++) {
  raw[y * (1 + W * 4)] = 0;
  for (let x = 0; x < W * 4; x++) raw[y * (1 + W * 4) + 1 + x] = fb[y * W * 4 + x];
}
const png = Buffer.concat([
  Buffer.from([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]),
  chunk('IHDR', ihdr),
  chunk('IDAT', deflateSync(raw)),
  chunk('IEND', Buffer.alloc(0)),
]);
writeFileSync(outPath, png);
console.log(`guardado ${outPath}`);
