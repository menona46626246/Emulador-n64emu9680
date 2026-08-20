// APU: 4 canales (2 cuadrados, onda, ruido) con secuenciador de frames a 512 Hz

const DUTY = [
  [0, 0, 0, 0, 0, 0, 0, 1],
  [1, 0, 0, 0, 0, 0, 0, 1],
  [1, 0, 0, 0, 0, 1, 1, 1],
  [0, 1, 1, 1, 1, 1, 1, 0],
];
const NOISE_DIV = [8, 16, 32, 48, 64, 80, 96, 112];

// mascaras OR de lectura FF10..FF3F
const READ_MASK = [
  0x80, 0x3F, 0x00, 0xFF, 0xBF, // NR10-NR14
  0xFF, 0x3F, 0x00, 0xFF, 0xBF, // ---- NR21-NR24
  0x7F, 0xFF, 0x9F, 0xFF, 0xBF, // NR30-NR34
  0xFF, 0xFF, 0x00, 0x00, 0xBF, // ---- NR41-NR44
  0x00, 0x00, 0x70,             // NR50-NR52
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // FF27-FF2F
];

class Square {
  constructor(hasSweep) {
    this.hasSweep = hasSweep;
    this.reset();
  }
  reset() {
    this.enabled = false;
    this.dacEnabled = false;
    this.duty = 0; this.dutyPos = 0;
    this.freq = 0; this.timer = 0;
    this.length = 0; this.lengthEnable = false;
    this.envVol = 0; this.envDir = 0; this.envPeriod = 0; this.envTimer = 0; this.volume = 0;
    this.sweepPeriod = 0; this.sweepNeg = false; this.sweepShift = 0;
    this.sweepTimer = 0; this.sweepEnabled = false; this.shadowFreq = 0;
  }
  trigger() {
    this.enabled = this.dacEnabled;
    if (this.length === 0) this.length = 64;
    this.timer = (2048 - this.freq) * 4;
    this.envTimer = this.envPeriod || 8;
    this.volume = this.envVol;
    if (this.hasSweep) {
      this.shadowFreq = this.freq;
      this.sweepTimer = this.sweepPeriod || 8;
      this.sweepEnabled = this.sweepPeriod > 0 || this.sweepShift > 0;
      if (this.sweepShift > 0) this.calcSweep();
    }
  }
  calcSweep() {
    let f = this.shadowFreq >> this.sweepShift;
    f = this.sweepNeg ? this.shadowFreq - f : this.shadowFreq + f;
    if (f > 2047) this.enabled = false;
    return f;
  }
  stepSweep() {
    if (!this.hasSweep) return;
    if (--this.sweepTimer <= 0) {
      this.sweepTimer = this.sweepPeriod || 8;
      if (this.sweepEnabled && this.sweepPeriod > 0) {
        const f = this.calcSweep();
        if (f <= 2047 && this.sweepShift > 0) {
          this.shadowFreq = f;
          this.freq = f;
          this.calcSweep();
        }
      }
    }
  }
  stepLength() {
    if (this.lengthEnable && this.length > 0) {
      if (--this.length === 0) this.enabled = false;
    }
  }
  stepEnv() {
    if (this.envPeriod === 0) return;
    if (--this.envTimer <= 0) {
      this.envTimer = this.envPeriod;
      if (this.envDir && this.volume < 15) this.volume++;
      else if (!this.envDir && this.volume > 0) this.volume--;
    }
  }
  tick(cycles) {
    this.timer -= cycles;
    while (this.timer <= 0) {
      this.timer += (2048 - this.freq) * 4;
      this.dutyPos = (this.dutyPos + 1) & 7;
    }
  }
  output() {
    if (!this.enabled || !this.dacEnabled) return 0;
    return DUTY[this.duty][this.dutyPos] * this.volume;
  }
}

class Wave {
  constructor() {
    this.ram = new Uint8Array(16);
    this.reset();
  }
  reset() {
    this.enabled = false;
    this.dacEnabled = false;
    this.length = 0; this.lengthEnable = false;
    this.freq = 0; this.timer = 0;
    this.pos = 0;
    this.volCode = 0;
  }
  trigger() {
    this.enabled = this.dacEnabled;
    if (this.length === 0) this.length = 256;
    this.timer = (2048 - this.freq) * 2;
    this.pos = 0;
  }
  stepLength() {
    if (this.lengthEnable && this.length > 0) {
      if (--this.length === 0) this.enabled = false;
    }
  }
  tick(cycles) {
    this.timer -= cycles;
    while (this.timer <= 0) {
      this.timer += (2048 - this.freq) * 2;
      this.pos = (this.pos + 1) & 31;
    }
  }
  output() {
    if (!this.enabled || !this.dacEnabled) return 0;
    let s = this.ram[this.pos >> 1];
    s = (this.pos & 1) ? (s & 0xF) : (s >> 4);
    switch (this.volCode) {
      case 0: return 0;
      case 1: return s;
      case 2: return s >> 1;
      case 3: return s >> 2;
    }
  }
}

class Noise {
  constructor() { this.reset(); }
  reset() {
    this.enabled = false;
    this.dacEnabled = false;
    this.length = 0; this.lengthEnable = false;
    this.envVol = 0; this.envDir = 0; this.envPeriod = 0; this.envTimer = 0; this.volume = 0;
    this.shift = 0; this.width7 = false; this.divCode = 0;
    this.timer = 0; this.lfsr = 0x7FFF;
  }
  trigger() {
    this.enabled = this.dacEnabled;
    if (this.length === 0) this.length = 64;
    this.timer = NOISE_DIV[this.divCode] << this.shift;
    this.envTimer = this.envPeriod || 8;
    this.volume = this.envVol;
    this.lfsr = 0x7FFF;
  }
  stepLength() {
    if (this.lengthEnable && this.length > 0) {
      if (--this.length === 0) this.enabled = false;
    }
  }
  stepEnv() {
    if (this.envPeriod === 0) return;
    if (--this.envTimer <= 0) {
      this.envTimer = this.envPeriod;
      if (this.envDir && this.volume < 15) this.volume++;
      else if (!this.envDir && this.volume > 0) this.volume--;
    }
  }
  tick(cycles) {
    this.timer -= cycles;
    while (this.timer <= 0) {
      this.timer += NOISE_DIV[this.divCode] << this.shift;
      const xor = (this.lfsr ^ (this.lfsr >> 1)) & 1;
      this.lfsr = (this.lfsr >> 1) | (xor << 14);
      if (this.width7) this.lfsr = (this.lfsr & ~0x40) | (xor << 6);
    }
  }
  output() {
    if (!this.enabled || !this.dacEnabled) return 0;
    return (this.lfsr & 1) ? 0 : this.volume;
  }
}

export class APU {
  constructor(sampleRate = 44100) {
    this.sampleRate = sampleRate;
    this.cyclesPerSample = 4194304 / sampleRate;
    this.ch1 = new Square(true);
    this.ch2 = new Square(false);
    this.ch3 = new Wave();
    this.ch4 = new Noise();
    // buffer circular estereo intercalado
    this.buffer = new Float32Array(32768);
    this.readIdx = 0;
    this.writeIdx = 0;
    this.reset();
  }

  reset() {
    this.regs = new Uint8Array(0x30);
    this.power = true;
    this.nr50 = 0x77;
    this.nr51 = 0xF3;
    this.fsCounter = 0;
    this.fsStep = 0;
    this.sampleCounter = 0;
    this.ch1.reset(); this.ch2.reset(); this.ch3.reset(); this.ch4.reset();
    this.readIdx = 0; this.writeIdx = 0;
    this.enabled = true; // salida global (mute del usuario aparte)
  }

  read(addr) {
    const i = addr - 0xFF10;
    if (addr >= 0xFF30) { // wave RAM
      return this.ch3.ram[addr - 0xFF30];
    }
    if (addr === 0xFF26) {
      let v = 0x70;
      if (this.power) v |= 0x80;
      if (this.ch1.enabled) v |= 1;
      if (this.ch2.enabled) v |= 2;
      if (this.ch3.enabled) v |= 4;
      if (this.ch4.enabled) v |= 8;
      return v;
    }
    return (this.regs[i] | (READ_MASK[i] !== undefined ? READ_MASK[i] : 0xFF)) & 0xFF;
  }

  write(addr, val) {
    if (addr >= 0xFF30 && addr <= 0xFF3F) {
      this.ch3.ram[addr - 0xFF30] = val;
      return;
    }
    if (addr === 0xFF26) { // NR52
      const on = (val & 0x80) !== 0;
      if (!on && this.power) {
        // apagado: limpiar registros
        for (let a = 0xFF10; a < 0xFF26; a++) this.write(a, 0);
        this.regs.fill(0, 0, 0x16);
        this.ch1.reset(); this.ch2.reset(); this.ch3.reset(); this.ch4.reset();
      }
      if (on && !this.power) { this.fsStep = 0; }
      this.power = on;
      return;
    }
    if (!this.power) return;
    const i = addr - 0xFF10;
    this.regs[i] = val;
    const c1 = this.ch1, c2 = this.ch2, c3 = this.ch3, c4 = this.ch4;
    switch (addr) {
      case 0xFF10:
        c1.sweepPeriod = (val >> 4) & 7;
        c1.sweepNeg = (val & 8) !== 0;
        c1.sweepShift = val & 7;
        break;
      case 0xFF11: c1.duty = val >> 6; c1.length = 64 - (val & 0x3F); break;
      case 0xFF12:
        c1.envVol = val >> 4; c1.envDir = (val >> 3) & 1; c1.envPeriod = val & 7;
        c1.dacEnabled = (val & 0xF8) !== 0;
        if (!c1.dacEnabled) c1.enabled = false;
        break;
      case 0xFF13: c1.freq = (c1.freq & 0x700) | val; break;
      case 0xFF14:
        c1.freq = (c1.freq & 0xFF) | ((val & 7) << 8);
        c1.lengthEnable = (val & 0x40) !== 0;
        if (val & 0x80) c1.trigger();
        break;
      case 0xFF16: c2.duty = val >> 6; c2.length = 64 - (val & 0x3F); break;
      case 0xFF17:
        c2.envVol = val >> 4; c2.envDir = (val >> 3) & 1; c2.envPeriod = val & 7;
        c2.dacEnabled = (val & 0xF8) !== 0;
        if (!c2.dacEnabled) c2.enabled = false;
        break;
      case 0xFF18: c2.freq = (c2.freq & 0x700) | val; break;
      case 0xFF19:
        c2.freq = (c2.freq & 0xFF) | ((val & 7) << 8);
        c2.lengthEnable = (val & 0x40) !== 0;
        if (val & 0x80) c2.trigger();
        break;
      case 0xFF1A:
        c3.dacEnabled = (val & 0x80) !== 0;
        if (!c3.dacEnabled) c3.enabled = false;
        break;
      case 0xFF1B: c3.length = 256 - val; break;
      case 0xFF1C: c3.volCode = (val >> 5) & 3; break;
      case 0xFF1D: c3.freq = (c3.freq & 0x700) | val; break;
      case 0xFF1E:
        c3.freq = (c3.freq & 0xFF) | ((val & 7) << 8);
        c3.lengthEnable = (val & 0x40) !== 0;
        if (val & 0x80) c3.trigger();
        break;
      case 0xFF20: c4.length = 64 - (val & 0x3F); break;
      case 0xFF21:
        c4.envVol = val >> 4; c4.envDir = (val >> 3) & 1; c4.envPeriod = val & 7;
        c4.dacEnabled = (val & 0xF8) !== 0;
        if (!c4.dacEnabled) c4.enabled = false;
        break;
      case 0xFF22:
        c4.shift = val >> 4; c4.width7 = (val & 8) !== 0; c4.divCode = val & 7;
        break;
      case 0xFF23:
        c4.lengthEnable = (val & 0x40) !== 0;
        if (val & 0x80) c4.trigger();
        break;
      case 0xFF24: this.nr50 = val; break;
      case 0xFF25: this.nr51 = val; break;
    }
  }

  stepFrameSequencer() {
    const s = this.fsStep;
    if ((s & 1) === 0) { // 0,2,4,6: longitud
      this.ch1.stepLength(); this.ch2.stepLength();
      this.ch3.stepLength(); this.ch4.stepLength();
    }
    if (s === 2 || s === 6) this.ch1.stepSweep();
    if (s === 7) { this.ch1.stepEnv(); this.ch2.stepEnv(); this.ch4.stepEnv(); }
    this.fsStep = (s + 1) & 7;
  }

  tick(cycles) {
    if (this.power) {
      this.ch1.tick(cycles);
      this.ch2.tick(cycles);
      this.ch3.tick(cycles);
      this.ch4.tick(cycles);
      this.fsCounter += cycles;
      while (this.fsCounter >= 8192) {
        this.fsCounter -= 8192;
        this.stepFrameSequencer();
      }
    }
    this.sampleCounter += cycles;
    while (this.sampleCounter >= this.cyclesPerSample) {
      this.sampleCounter -= this.cyclesPerSample;
      this.pushSample();
    }
  }

  pushSample() {
    let left = 0, right = 0;
    if (this.power) {
      const outs = [this.ch1.output(), this.ch2.output(), this.ch3.output(), this.ch4.output()];
      const dacs = [
        this.ch1.dacEnabled, this.ch2.dacEnabled,
        this.ch3.dacEnabled, this.ch4.dacEnabled,
      ];
      for (let c = 0; c < 4; c++) {
        const analog = dacs[c] ? (outs[c] / 7.5 - 1) : 0;
        if (this.nr51 & (0x10 << c)) left += analog;
        if (this.nr51 & (0x01 << c)) right += analog;
      }
      left *= (((this.nr50 >> 4) & 7) + 1) / 8;
      right *= ((this.nr50 & 7) + 1) / 8;
    }
    const mask = this.buffer.length - 1;
    // si el buffer esta lleno, descartar la muestra mas vieja
    if (((this.writeIdx + 2) & mask) === this.readIdx || ((this.writeIdx + 3) & mask) === this.readIdx) {
      this.readIdx = (this.readIdx + 2) & mask;
    }
    this.buffer[this.writeIdx] = left * 0.25;
    this.buffer[(this.writeIdx + 1) & mask] = right * 0.25;
    this.writeIdx = (this.writeIdx + 2) & mask;
  }

  // cantidad de frames estereo disponibles
  available() {
    const mask = this.buffer.length - 1;
    return ((this.writeIdx - this.readIdx) & mask) >> 1;
  }

  // rellena dos canales Float32Array del AudioContext
  fill(outL, outR) {
    const mask = this.buffer.length - 1;
    const n = outL.length;
    for (let i = 0; i < n; i++) {
      if (this.readIdx === this.writeIdx) {
        outL[i] = 0; outR[i] = 0;
      } else {
        outL[i] = this.buffer[this.readIdx];
        outR[i] = this.buffer[(this.readIdx + 1) & mask];
        this.readIdx = (this.readIdx + 2) & mask;
      }
    }
  }
}
