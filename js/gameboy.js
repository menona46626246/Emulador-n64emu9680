// Consola completa: integra CPU, MMU, PPU, APU, Timer y Joypad

import { CPU } from './cpu.js';
import { MMU } from './mmu.js';
import { PPU } from './ppu.js';
import { APU } from './apu.js';
import { Timer } from './timer.js';
import { Joypad } from './joypad.js';

export const CYCLES_PER_FRAME = 70224; // 4194304 Hz / ~59.73 fps

export class GameBoy {
  constructor(sampleRate = 44100) {
    this.mmu = new MMU();
    this.ppu = new PPU(this.mmu);
    this.apu = new APU(sampleRate);
    this.timer = new Timer(this.mmu);
    this.joypad = new Joypad(this.mmu);
    this.cpu = new CPU(this.mmu);
    this.mmu.ppu = this.ppu;
    this.mmu.apu = this.apu;
    this.mmu.timer = this.timer;
    this.mmu.joypad = this.joypad;
    this.romLoaded = false;
  }

  loadROM(bytes) {
    this.mmu.loadROM(bytes);
    this.reset();
    this.romLoaded = true;
  }

  reset() {
    this.cpu.reset();
    this.ppu.reset();
    this.apu.reset();
    this.timer.reset();
    this.joypad.reset();
    const rom = this.mmu.rom;
    this.mmu.reset();
    this.mmu.rom = rom;
    this.mmu.reset2();
  }

  // Ejecuta un fotograma completo; devuelve el framebuffer
  runFrame() {
    const ppu = this.ppu, cpu = this.cpu, timer = this.timer, apu = this.apu;
    ppu.frameReady = false;
    let cycles = 0;
    while (cycles < CYCLES_PER_FRAME && !ppu.frameReady) {
      const c = cpu.step();
      timer.tick(c);
      ppu.tick(c);
      apu.tick(c);
      cycles += c;
    }
    // si la LCD esta apagada, consumir el resto del presupuesto del frame
    while (cycles < CYCLES_PER_FRAME && !(ppu.lcdc & 0x80)) {
      const c = cpu.step();
      timer.tick(c);
      ppu.tick(c);
      apu.tick(c);
      cycles += c;
    }
    return ppu.framebuffer;
  }

  // Ejecuta N ciclos (para pruebas)
  runCycles(n) {
    let cycles = 0;
    while (cycles < n) {
      const c = this.cpu.step();
      this.timer.tick(c);
      this.ppu.tick(c);
      this.apu.tick(c);
      cycles += c;
    }
    return cycles;
  }

  get saveData() {
    return this.mmu.hasBattery && this.mmu.eram.length ? this.mmu.eram : null;
  }
  set saveData(bytes) {
    if (bytes && this.mmu.eram.length) {
      this.mmu.eram.set(bytes.subarray(0, this.mmu.eram.length));
    }
  }
}
