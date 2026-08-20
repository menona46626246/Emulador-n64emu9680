// MMU + cartucho (MBC0/1/2/3/5)

const RAM_SIZES = [0, 0x800, 0x2000, 0x8000, 0x20000, 0x10000];

export class MMU {
  constructor() {
    this.wram = new Uint8Array(0x2000);
    this.hram = new Uint8Array(0x7F);
    this.ie = 0;
    this.iflags = 0xE1;
    this.sb = 0; this.sc = 0x7E;
    this.serialOut = null; // callback(byte) para pruebas / link
    this.ppu = null; this.apu = null; this.timer = null; this.joypad = null;
    this.rom = new Uint8Array(0x8000);
    this.reset();
  }

  reset() {
    this.wram.fill(0);
    this.hram.fill(0);
    this.ie = 0;
    this.iflags = 0xE1;
    this.sb = 0; this.sc = 0x7E;
    // estado del cartucho
    this.ramEnabled = false;
    this.romBank = 1;
    this.bank2 = 0;      // MBC1: bits altos / banco RAM
    this.mbc1Mode = 0;
    this.ramBank = 0;
    this.rtcSel = -1;    // MBC3: registro RTC seleccionado
    this.rtc = { s: 0, m: 0, h: 0, dl: 0, dh: 0 };
    this.dmaLast = 0xFF;
  }

  loadROM(bytes) {
    this.rom = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
    const type = this.rom[0x147] | 0;
    this.mbc = 0;
    if (type >= 0x01 && type <= 0x03) this.mbc = 1;
    else if (type === 0x05 || type === 0x06) this.mbc = 2;
    else if (type >= 0x0F && type <= 0x13) this.mbc = 3;
    else if (type >= 0x19 && type <= 0x1E) this.mbc = 5;
    this.hasBattery = [0x03, 0x06, 0x09, 0x0D, 0x0F, 0x10, 0x13, 0x1B, 0x1E].includes(type);
    this.romBanks = Math.max(2, this.rom.length >> 14);
    let ramSize = RAM_SIZES[this.rom[0x149]] || 0;
    if (this.mbc === 2) ramSize = 0x200; // 512 x 4 bits internos
    if (this.mbc === 1 && ramSize === 0 && (type === 0x02 || type === 0x03)) ramSize = 0x2000;
    this.eram = new Uint8Array(ramSize);
    this.reset2();
  }

  reset2() {
    this.ramEnabled = false;
    this.romBank = 1;
    this.bank2 = 0;
    this.mbc1Mode = 0;
    this.ramBank = 0;
    this.rtcSel = -1;
  }

  get title() {
    let s = '';
    for (let i = 0x134; i < 0x144; i++) {
      const c = this.rom[i];
      if (!c) break;
      s += String.fromCharCode(c);
    }
    return s.trim();
  }

  // ---- lectura ----
  rb(addr) {
    addr &= 0xFFFF;
    if (addr < 0x4000) {
      // MBC1 modo 1: la zona baja puede mapear otro banco
      if (this.mbc === 1 && this.mbc1Mode === 1) {
        const bank = ((this.bank2 << 5) % this.romBanks) << 14;
        return this.rom[bank + addr];
      }
      return this.rom[addr];
    }
    if (addr < 0x8000) {
      let bank = this.romBank;
      if (this.mbc === 1) bank = ((this.bank2 << 5) | this.romBank) % this.romBanks;
      else bank = bank % this.romBanks;
      return this.rom[(bank << 14) + (addr - 0x4000)];
    }
    if (addr < 0xA000) return this.ppu.vram[addr - 0x8000];
    if (addr < 0xC000) return this.readERAM(addr - 0xA000);
    if (addr < 0xE000) return this.wram[addr - 0xC000];
    if (addr < 0xFE00) return this.wram[addr - 0xE000]; // echo
    if (addr < 0xFEA0) return this.ppu.oam[addr - 0xFE00];
    if (addr < 0xFF00) return 0xFF;
    if (addr < 0xFF80) return this.readIO(addr);
    if (addr < 0xFFFF) return this.hram[addr - 0xFF80];
    return this.ie;
  }

  readERAM(off) {
    if (!this.ramEnabled) return 0xFF;
    if (this.mbc === 3 && this.rtcSel >= 0) {
      switch (this.rtcSel) {
        case 0x08: return this.rtc.s;
        case 0x09: return this.rtc.m;
        case 0x0A: return this.rtc.h;
        case 0x0B: return this.rtc.dl;
        case 0x0C: return this.rtc.dh;
      }
      return 0xFF;
    }
    if (this.eram.length === 0) return 0xFF;
    if (this.mbc === 2) return this.eram[off & 0x1FF] | 0xF0;
    const idx = (this.ramBank * 0x2000 + off) % this.eram.length;
    return this.eram[idx];
  }

  readIO(addr) {
    switch (addr) {
      case 0xFF00: return this.joypad.read();
      case 0xFF01: return this.sb;
      case 0xFF02: return this.sc | 0x7E;
      case 0xFF04: return this.timer.div;
      case 0xFF05: return this.timer.tima;
      case 0xFF06: return this.timer.tma;
      case 0xFF07: return this.timer.tac | 0xF8;
      case 0xFF0F: return this.iflags | 0xE0;
      case 0xFF40: return this.ppu.lcdc;
      case 0xFF41: return this.ppu.readSTAT();
      case 0xFF42: return this.ppu.scy;
      case 0xFF43: return this.ppu.scx;
      case 0xFF44: return this.ppu.line;
      case 0xFF45: return this.ppu.lyc;
      case 0xFF46: return this.dmaLast;
      case 0xFF47: return this.ppu.bgp;
      case 0xFF48: return this.ppu.obp0;
      case 0xFF49: return this.ppu.obp1;
      case 0xFF4A: return this.ppu.wy;
      case 0xFF4B: return this.ppu.wx;
    }
    if (addr >= 0xFF10 && addr <= 0xFF3F) return this.apu.read(addr);
    return 0xFF;
  }

  // ---- escritura ----
  wb(addr, val) {
    addr &= 0xFFFF; val &= 0xFF;
    if (addr < 0x8000) { this.writeMBC(addr, val); return; }
    if (addr < 0xA000) { this.ppu.vram[addr - 0x8000] = val; return; }
    if (addr < 0xC000) { this.writeERAM(addr - 0xA000, val); return; }
    if (addr < 0xE000) { this.wram[addr - 0xC000] = val; return; }
    if (addr < 0xFE00) { this.wram[addr - 0xE000] = val; return; }
    if (addr < 0xFEA0) { this.ppu.oam[addr - 0xFE00] = val; return; }
    if (addr < 0xFF00) return;
    if (addr < 0xFF80) { this.writeIO(addr, val); return; }
    if (addr < 0xFFFF) { this.hram[addr - 0xFF80] = val; return; }
    this.ie = val;
  }

  writeMBC(addr, val) {
    switch (this.mbc) {
      case 0: return;
      case 1:
        if (addr < 0x2000) this.ramEnabled = (val & 0xF) === 0xA;
        else if (addr < 0x4000) { this.romBank = (val & 0x1F) || 1; }
        else if (addr < 0x6000) this.bank2 = val & 0x03;
        else this.mbc1Mode = val & 1;
        this.ramBank = this.mbc1Mode === 1 ? this.bank2 : 0;
        return;
      case 2:
        if (addr < 0x4000) {
          if (addr & 0x100) { this.romBank = (val & 0x0F) || 1; }
          else this.ramEnabled = (val & 0xF) === 0xA;
        }
        return;
      case 3:
        if (addr < 0x2000) this.ramEnabled = (val & 0xF) === 0xA;
        else if (addr < 0x4000) this.romBank = (val & 0x7F) || 1;
        else if (addr < 0x6000) {
          if (val <= 0x07) { this.ramBank = val & 0x03; this.rtcSel = -1; }
          else if (val >= 0x08 && val <= 0x0C) this.rtcSel = val;
        } else {
          // latch RTC (simplificado: usa la hora real)
          const now = new Date();
          this.rtc.s = now.getSeconds();
          this.rtc.m = now.getMinutes();
          this.rtc.h = now.getHours();
        }
        return;
      case 5:
        if (addr < 0x2000) this.ramEnabled = (val & 0xF) === 0xA;
        else if (addr < 0x3000) this.romBank = (this.romBank & 0x100) | val;
        else if (addr < 0x4000) this.romBank = (this.romBank & 0xFF) | ((val & 1) << 8);
        else if (addr < 0x6000) this.ramBank = val & 0x0F;
        return;
    }
  }

  writeERAM(off, val) {
    if (!this.ramEnabled) return;
    if (this.mbc === 3 && this.rtcSel >= 0) {
      switch (this.rtcSel) {
        case 0x08: this.rtc.s = val; break;
        case 0x09: this.rtc.m = val; break;
        case 0x0A: this.rtc.h = val; break;
        case 0x0B: this.rtc.dl = val; break;
        case 0x0C: this.rtc.dh = val; break;
      }
      return;
    }
    if (this.eram.length === 0) return;
    if (this.mbc === 2) { this.eram[off & 0x1FF] = val & 0x0F; return; }
    const idx = (this.ramBank * 0x2000 + off) % this.eram.length;
    this.eram[idx] = val;
  }

  writeIO(addr, val) {
    switch (addr) {
      case 0xFF00: this.joypad.write(val); return;
      case 0xFF01: this.sb = val; return;
      case 0xFF02:
        this.sc = val;
        if (val & 0x80) {
          // transferencia serie inmediata (sin cable link)
          if (this.serialOut) this.serialOut(this.sb);
          if (val & 0x01) { // reloj interno: completa la transferencia
            this.sb = 0xFF;
            this.sc &= 0x7F;
            this.iflags |= 0x08;
          }
        }
        return;
      case 0xFF04: this.timer.resetDiv(); return;
      case 0xFF05: this.timer.tima = val; return;
      case 0xFF06: this.timer.tma = val; return;
      case 0xFF07: this.timer.tac = val & 0x07; return;
      case 0xFF0F: this.iflags = val & 0x1F; return;
      case 0xFF40: this.ppu.writeLCDC(val); return;
      case 0xFF41: this.ppu.writeSTAT(val); return;
      case 0xFF42: this.ppu.scy = val; return;
      case 0xFF43: this.ppu.scx = val; return;
      case 0xFF44: return; // LY solo lectura
      case 0xFF45: this.ppu.lyc = val; this.ppu.checkLYC(); return;
      case 0xFF46: this.doDMA(val); return;
      case 0xFF47: this.ppu.bgp = val; return;
      case 0xFF48: this.ppu.obp0 = val; return;
      case 0xFF49: this.ppu.obp1 = val; return;
      case 0xFF4A: this.ppu.wy = val; return;
      case 0xFF4B: this.ppu.wx = val; return;
    }
    if (addr >= 0xFF10 && addr <= 0xFF3F) this.apu.write(addr, val);
  }

  doDMA(val) {
    this.dmaLast = val;
    const base = val << 8;
    for (let i = 0; i < 0xA0; i++) {
      this.ppu.oam[i] = this.rb(base + i);
    }
  }

  requestInterrupt(bit) { this.iflags |= bit; }
}
