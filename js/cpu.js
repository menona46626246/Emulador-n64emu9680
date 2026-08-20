// CPU Sharp SM83 (nucleo del Game Boy DMG)
// Flags en F: Z=0x80, N=0x40, H=0x20, C=0x10

export class CPU {
  constructor(mmu) {
    this.mmu = mmu;
    this.reset();
  }

  reset() {
    // Estado tras el boot ROM oficial (DMG)
    this.a = 0x01; this.f = 0xB0;
    this.b = 0x00; this.c = 0x13;
    this.d = 0x00; this.e = 0xD8;
    this.h = 0x01; this.l = 0x4D;
    this.sp = 0xFFFE;
    this.pc = 0x0100;
    this.ime = false;
    this.imeDelay = false;
    this.halted = false;
    this.haltBug = false;
  }

  // ---- pares de registros ----
  get af() { return (this.a << 8) | this.f; }
  set af(v) { this.a = (v >> 8) & 0xFF; this.f = v & 0xF0; }
  get bc() { return (this.b << 8) | this.c; }
  set bc(v) { this.b = (v >> 8) & 0xFF; this.c = v & 0xFF; }
  get de() { return (this.d << 8) | this.e; }
  set de(v) { this.d = (v >> 8) & 0xFF; this.e = v & 0xFF; }
  get hl() { return (this.h << 8) | this.l; }
  set hl(v) { this.h = (v >> 8) & 0xFF; this.l = v & 0xFF; }

  fetch8() {
    const v = this.mmu.rb(this.pc);
    if (this.haltBug) this.haltBug = false;
    else this.pc = (this.pc + 1) & 0xFFFF;
    return v;
  }
  fetch16() {
    const lo = this.fetch8();
    const hi = this.fetch8();
    return (hi << 8) | lo;
  }
  push16(v) {
    this.sp = (this.sp - 1) & 0xFFFF;
    this.mmu.wb(this.sp, (v >> 8) & 0xFF);
    this.sp = (this.sp - 1) & 0xFFFF;
    this.mmu.wb(this.sp, v & 0xFF);
  }
  pop16() {
    const lo = this.mmu.rb(this.sp);
    this.sp = (this.sp + 1) & 0xFFFF;
    const hi = this.mmu.rb(this.sp);
    this.sp = (this.sp + 1) & 0xFFFF;
    return (hi << 8) | lo;
  }

  // Un paso: devuelve T-cycles consumidos
  step() {
    const pending = this.mmu.ie & this.mmu.iflags & 0x1F;

    if (this.halted) {
      if (pending) this.halted = false;
      else return 4;
    }

    if (this.ime && pending) {
      this.ime = false;
      this.imeDelay = false;
      let bit = 0;
      while (!(pending & (1 << bit))) bit++;
      this.mmu.iflags &= ~(1 << bit);
      this.push16(this.pc);
      this.pc = 0x40 + bit * 8;
      return 20;
    }

    if (this.imeDelay) { this.ime = true; this.imeDelay = false; }

    const op = this.fetch8();
    return this.exec(op);
  }

  getR(i) {
    switch (i) {
      case 0: return this.b; case 1: return this.c;
      case 2: return this.d; case 3: return this.e;
      case 4: return this.h; case 5: return this.l;
      case 6: return this.mmu.rb(this.hl);
      case 7: return this.a;
    }
  }
  setR(i, v) {
    switch (i) {
      case 0: this.b = v; break; case 1: this.c = v; break;
      case 2: this.d = v; break; case 3: this.e = v; break;
      case 4: this.h = v; break; case 5: this.l = v; break;
      case 6: this.mmu.wb(this.hl, v); break;
      case 7: this.a = v; break;
    }
  }

  // ---- ALU ----
  add8(v, carry) {
    const c = carry ? 1 : 0;
    const r = this.a + v + c;
    let f = 0;
    if ((r & 0xFF) === 0) f |= 0x80;
    if (((this.a & 0xF) + (v & 0xF) + c) > 0xF) f |= 0x20;
    if (r > 0xFF) f |= 0x10;
    this.f = f;
    this.a = r & 0xFF;
  }
  sub8(v, carry, store) {
    const c = carry ? 1 : 0;
    const r = this.a - v - c;
    let f = 0x40;
    if ((r & 0xFF) === 0) f |= 0x80;
    if (((this.a & 0xF) - (v & 0xF) - c) < 0) f |= 0x20;
    if (r < 0) f |= 0x10;
    this.f = f;
    if (store) this.a = r & 0xFF;
  }
  alu(idx, v) {
    switch (idx) {
      case 0: this.add8(v, false); break;              // ADD
      case 1: this.add8(v, (this.f & 0x10) !== 0); break; // ADC
      case 2: this.sub8(v, false, true); break;        // SUB
      case 3: this.sub8(v, (this.f & 0x10) !== 0, true); break; // SBC
      case 4: this.a &= v; this.f = (this.a === 0 ? 0x80 : 0) | 0x20; break; // AND
      case 5: this.a ^= v; this.f = this.a === 0 ? 0x80 : 0; break;          // XOR
      case 6: this.a |= v; this.f = this.a === 0 ? 0x80 : 0; break;          // OR
      case 7: this.sub8(v, false, false); break;       // CP
    }
  }
  inc8(v) {
    const r = (v + 1) & 0xFF;
    this.f = (this.f & 0x10) | (r === 0 ? 0x80 : 0) | ((v & 0xF) === 0xF ? 0x20 : 0);
    return r;
  }
  dec8(v) {
    const r = (v - 1) & 0xFF;
    this.f = (this.f & 0x10) | 0x40 | (r === 0 ? 0x80 : 0) | ((v & 0xF) === 0 ? 0x20 : 0);
    return r;
  }
  addHL(v) {
    const hl = this.hl;
    const r = hl + v;
    this.f = (this.f & 0x80) |
      (((hl & 0xFFF) + (v & 0xFFF)) > 0xFFF ? 0x20 : 0) |
      (r > 0xFFFF ? 0x10 : 0);
    this.hl = r & 0xFFFF;
  }
  // ADD SP,e8 y LD HL,SP+e8 comparten flags
  spAdd(raw) {
    const v = raw < 0x80 ? raw : raw - 256;
    const r = (this.sp + v) & 0xFFFF;
    this.f = (((this.sp & 0xF) + (raw & 0xF)) > 0xF ? 0x20 : 0) |
             (((this.sp & 0xFF) + raw) > 0xFF ? 0x10 : 0);
    return r;
  }
  daa() {
    let a = this.a;
    const n = this.f & 0x40, h = this.f & 0x20, c = this.f & 0x10;
    let carry = c !== 0;
    if (!n) {
      if (c || a > 0x99) { a = (a + 0x60) & 0xFF; carry = true; }
      if (h || (a & 0xF) > 0x9) a = (a + 0x06) & 0xFF;
    } else {
      if (c) a = (a - 0x60) & 0xFF;
      if (h) a = (a - 0x06) & 0xFF;
    }
    this.f = (this.f & 0x40) | (a === 0 ? 0x80 : 0) | (carry ? 0x10 : 0);
    this.a = a;
  }

  // ---- rotaciones/CB ----
  rlc(v) { const c = (v >> 7) & 1; const r = ((v << 1) | c) & 0xFF; this.f = (r === 0 ? 0x80 : 0) | (c ? 0x10 : 0); return r; }
  rrc(v) { const c = v & 1; const r = (v >> 1) | (c << 7); this.f = (r === 0 ? 0x80 : 0) | (c ? 0x10 : 0); return r; }
  rl(v) { const old = (this.f & 0x10) ? 1 : 0; const c = (v >> 7) & 1; const r = ((v << 1) | old) & 0xFF; this.f = (r === 0 ? 0x80 : 0) | (c ? 0x10 : 0); return r; }
  rr(v) { const old = (this.f & 0x10) ? 0x80 : 0; const c = v & 1; const r = (v >> 1) | old; this.f = (r === 0 ? 0x80 : 0) | (c ? 0x10 : 0); return r; }
  sla(v) { const c = (v >> 7) & 1; const r = (v << 1) & 0xFF; this.f = (r === 0 ? 0x80 : 0) | (c ? 0x10 : 0); return r; }
  sra(v) { const c = v & 1; const r = (v >> 1) | (v & 0x80); this.f = (r === 0 ? 0x80 : 0) | (c ? 0x10 : 0); return r; }
  swap(v) { const r = ((v << 4) | (v >> 4)) & 0xFF; this.f = r === 0 ? 0x80 : 0; return r; }
  srl(v) { const c = v & 1; const r = v >> 1; this.f = (r === 0 ? 0x80 : 0) | (c ? 0x10 : 0); return r; }

  execCB() {
    const op = this.fetch8();
    const reg = op & 7;
    const kind = op >> 6;
    const bit = (op >> 3) & 7;
    if (kind === 1) { // BIT
      const v = this.getR(reg);
      this.f = (this.f & 0x10) | 0x20 | ((v & (1 << bit)) === 0 ? 0x80 : 0);
      return reg === 6 ? 12 : 8;
    }
    let v = this.getR(reg);
    if (kind === 2) v &= ~(1 << bit);          // RES
    else if (kind === 3) v |= (1 << bit);      // SET
    else {
      switch (bit) {
        case 0: v = this.rlc(v); break;
        case 1: v = this.rrc(v); break;
        case 2: v = this.rl(v); break;
        case 3: v = this.rr(v); break;
        case 4: v = this.sla(v); break;
        case 5: v = this.sra(v); break;
        case 6: v = this.swap(v); break;
        case 7: v = this.srl(v); break;
      }
    }
    this.setR(reg, v);
    return reg === 6 ? 16 : 8;
  }

  exec(op) {
    // LD r,r' (0x40-0x7F) y HALT (0x76)
    if (op >= 0x40 && op <= 0x7F) {
      if (op === 0x76) { // HALT
        const pending = this.mmu.ie & this.mmu.iflags & 0x1F;
        if (!this.ime && pending) this.haltBug = true;
        else this.halted = true;
        return 4;
      }
      const src = op & 7, dst = (op >> 3) & 7;
      this.setR(dst, this.getR(src));
      return (src === 6 || dst === 6) ? 8 : 4;
    }
    // ALU A,r (0x80-0xBF)
    if (op >= 0x80 && op <= 0xBF) {
      const src = op & 7;
      this.alu((op >> 3) & 7, this.getR(src));
      return src === 6 ? 8 : 4;
    }

    let t, v;
    switch (op) {
      case 0x00: return 4; // NOP
      case 0x01: this.bc = this.fetch16(); return 12;
      case 0x02: this.mmu.wb(this.bc, this.a); return 8;
      case 0x03: this.bc = (this.bc + 1) & 0xFFFF; return 8;
      case 0x04: this.b = this.inc8(this.b); return 4;
      case 0x05: this.b = this.dec8(this.b); return 4;
      case 0x06: this.b = this.fetch8(); return 8;
      case 0x07: { const c = (this.a >> 7) & 1; this.a = ((this.a << 1) | c) & 0xFF; this.f = c ? 0x10 : 0; return 4; } // RLCA
      case 0x08: { const a = this.fetch16(); this.mmu.wb(a, this.sp & 0xFF); this.mmu.wb((a + 1) & 0xFFFF, this.sp >> 8); return 20; }
      case 0x09: this.addHL(this.bc); return 8;
      case 0x0A: this.a = this.mmu.rb(this.bc); return 8;
      case 0x0B: this.bc = (this.bc - 1) & 0xFFFF; return 8;
      case 0x0C: this.c = this.inc8(this.c); return 4;
      case 0x0D: this.c = this.dec8(this.c); return 4;
      case 0x0E: this.c = this.fetch8(); return 8;
      case 0x0F: { const c = this.a & 1; this.a = (this.a >> 1) | (c << 7); this.f = c ? 0x10 : 0; return 4; } // RRCA

      case 0x10: this.fetch8(); return 4; // STOP (tratado como NOP)
      case 0x11: this.de = this.fetch16(); return 12;
      case 0x12: this.mmu.wb(this.de, this.a); return 8;
      case 0x13: this.de = (this.de + 1) & 0xFFFF; return 8;
      case 0x14: this.d = this.inc8(this.d); return 4;
      case 0x15: this.d = this.dec8(this.d); return 4;
      case 0x16: this.d = this.fetch8(); return 8;
      case 0x17: { const old = (this.f & 0x10) ? 1 : 0; const c = (this.a >> 7) & 1; this.a = ((this.a << 1) | old) & 0xFF; this.f = c ? 0x10 : 0; return 4; } // RLA
      case 0x18: { v = this.fetch8(); this.pc = (this.pc + (v < 0x80 ? v : v - 256)) & 0xFFFF; return 12; } // JR
      case 0x19: this.addHL(this.de); return 8;
      case 0x1A: this.a = this.mmu.rb(this.de); return 8;
      case 0x1B: this.de = (this.de - 1) & 0xFFFF; return 8;
      case 0x1C: this.e = this.inc8(this.e); return 4;
      case 0x1D: this.e = this.dec8(this.e); return 4;
      case 0x1E: this.e = this.fetch8(); return 8;
      case 0x1F: { const old = (this.f & 0x10) ? 0x80 : 0; const c = this.a & 1; this.a = (this.a >> 1) | old; this.f = c ? 0x10 : 0; return 4; } // RRA

      case 0x20: v = this.fetch8(); if (!(this.f & 0x80)) { this.pc = (this.pc + (v < 0x80 ? v : v - 256)) & 0xFFFF; return 12; } return 8;
      case 0x21: this.hl = this.fetch16(); return 12;
      case 0x22: this.mmu.wb(this.hl, this.a); this.hl = (this.hl + 1) & 0xFFFF; return 8;
      case 0x23: this.hl = (this.hl + 1) & 0xFFFF; return 8;
      case 0x24: this.h = this.inc8(this.h); return 4;
      case 0x25: this.h = this.dec8(this.h); return 4;
      case 0x26: this.h = this.fetch8(); return 8;
      case 0x27: this.daa(); return 4;
      case 0x28: v = this.fetch8(); if (this.f & 0x80) { this.pc = (this.pc + (v < 0x80 ? v : v - 256)) & 0xFFFF; return 12; } return 8;
      case 0x29: this.addHL(this.hl); return 8;
      case 0x2A: this.a = this.mmu.rb(this.hl); this.hl = (this.hl + 1) & 0xFFFF; return 8;
      case 0x2B: this.hl = (this.hl - 1) & 0xFFFF; return 8;
      case 0x2C: this.l = this.inc8(this.l); return 4;
      case 0x2D: this.l = this.dec8(this.l); return 4;
      case 0x2E: this.l = this.fetch8(); return 8;
      case 0x2F: this.a ^= 0xFF; this.f = (this.f & 0x90) | 0x60; return 4; // CPL

      case 0x30: v = this.fetch8(); if (!(this.f & 0x10)) { this.pc = (this.pc + (v < 0x80 ? v : v - 256)) & 0xFFFF; return 12; } return 8;
      case 0x31: this.sp = this.fetch16(); return 12;
      case 0x32: this.mmu.wb(this.hl, this.a); this.hl = (this.hl - 1) & 0xFFFF; return 8;
      case 0x33: this.sp = (this.sp + 1) & 0xFFFF; return 8;
      case 0x34: this.mmu.wb(this.hl, this.inc8(this.mmu.rb(this.hl))); return 12;
      case 0x35: this.mmu.wb(this.hl, this.dec8(this.mmu.rb(this.hl))); return 12;
      case 0x36: this.mmu.wb(this.hl, this.fetch8()); return 12;
      case 0x37: this.f = (this.f & 0x80) | 0x10; return 4; // SCF
      case 0x38: v = this.fetch8(); if (this.f & 0x10) { this.pc = (this.pc + (v < 0x80 ? v : v - 256)) & 0xFFFF; return 12; } return 8;
      case 0x39: this.addHL(this.sp); return 8;
      case 0x3A: this.a = this.mmu.rb(this.hl); this.hl = (this.hl - 1) & 0xFFFF; return 8;
      case 0x3B: this.sp = (this.sp - 1) & 0xFFFF; return 8;
      case 0x3C: this.a = this.inc8(this.a); return 4;
      case 0x3D: this.a = this.dec8(this.a); return 4;
      case 0x3E: this.a = this.fetch8(); return 8;
      case 0x3F: this.f = (this.f & 0x80) | ((this.f & 0x10) ^ 0x10); return 4; // CCF

      case 0xC0: if (!(this.f & 0x80)) { this.pc = this.pop16(); return 20; } return 8;
      case 0xC1: this.bc = this.pop16(); return 12;
      case 0xC2: t = this.fetch16(); if (!(this.f & 0x80)) { this.pc = t; return 16; } return 12;
      case 0xC3: this.pc = this.fetch16(); return 16;
      case 0xC4: t = this.fetch16(); if (!(this.f & 0x80)) { this.push16(this.pc); this.pc = t; return 24; } return 12;
      case 0xC5: this.push16(this.bc); return 16;
      case 0xC6: this.alu(0, this.fetch8()); return 8;
      case 0xC7: this.push16(this.pc); this.pc = 0x00; return 16;
      case 0xC8: if (this.f & 0x80) { this.pc = this.pop16(); return 20; } return 8;
      case 0xC9: this.pc = this.pop16(); return 16;
      case 0xCA: t = this.fetch16(); if (this.f & 0x80) { this.pc = t; return 16; } return 12;
      case 0xCB: return this.execCB();
      case 0xCC: t = this.fetch16(); if (this.f & 0x80) { this.push16(this.pc); this.pc = t; return 24; } return 12;
      case 0xCD: t = this.fetch16(); this.push16(this.pc); this.pc = t; return 24;
      case 0xCE: this.alu(1, this.fetch8()); return 8;
      case 0xCF: this.push16(this.pc); this.pc = 0x08; return 16;

      case 0xD0: if (!(this.f & 0x10)) { this.pc = this.pop16(); return 20; } return 8;
      case 0xD1: this.de = this.pop16(); return 12;
      case 0xD2: t = this.fetch16(); if (!(this.f & 0x10)) { this.pc = t; return 16; } return 12;
      case 0xD4: t = this.fetch16(); if (!(this.f & 0x10)) { this.push16(this.pc); this.pc = t; return 24; } return 12;
      case 0xD5: this.push16(this.de); return 16;
      case 0xD6: this.alu(2, this.fetch8()); return 8;
      case 0xD7: this.push16(this.pc); this.pc = 0x10; return 16;
      case 0xD8: if (this.f & 0x10) { this.pc = this.pop16(); return 20; } return 8;
      case 0xD9: this.pc = this.pop16(); this.ime = true; return 16; // RETI
      case 0xDA: t = this.fetch16(); if (this.f & 0x10) { this.pc = t; return 16; } return 12;
      case 0xDC: t = this.fetch16(); if (this.f & 0x10) { this.push16(this.pc); this.pc = t; return 24; } return 12;
      case 0xDE: this.alu(3, this.fetch8()); return 8;
      case 0xDF: this.push16(this.pc); this.pc = 0x18; return 16;

      case 0xE0: this.mmu.wb(0xFF00 + this.fetch8(), this.a); return 12;
      case 0xE1: this.hl = this.pop16(); return 12;
      case 0xE2: this.mmu.wb(0xFF00 + this.c, this.a); return 8;
      case 0xE5: this.push16(this.hl); return 16;
      case 0xE6: this.alu(4, this.fetch8()); return 8;
      case 0xE7: this.push16(this.pc); this.pc = 0x20; return 16;
      case 0xE8: this.sp = this.spAdd(this.fetch8()); return 16; // ADD SP,e8
      case 0xE9: this.pc = this.hl; return 4;
      case 0xEA: this.mmu.wb(this.fetch16(), this.a); return 16;
      case 0xEE: this.alu(5, this.fetch8()); return 8;
      case 0xEF: this.push16(this.pc); this.pc = 0x28; return 16;

      case 0xF0: this.a = this.mmu.rb(0xFF00 + this.fetch8()); return 12;
      case 0xF1: this.af = this.pop16(); return 12;
      case 0xF2: this.a = this.mmu.rb(0xFF00 + this.c); return 8;
      case 0xF3: this.ime = false; this.imeDelay = false; return 4; // DI
      case 0xF5: this.push16(this.af); return 16;
      case 0xF6: this.alu(6, this.fetch8()); return 8;
      case 0xF7: this.push16(this.pc); this.pc = 0x30; return 16;
      case 0xF8: this.hl = this.spAdd(this.fetch8()); return 12; // LD HL,SP+e8
      case 0xF9: this.sp = this.hl; return 8;
      case 0xFA: this.a = this.mmu.rb(this.fetch16()); return 16;
      case 0xFB: this.imeDelay = true; return 4; // EI
      case 0xFE: this.alu(7, this.fetch8()); return 8;
      case 0xFF: this.push16(this.pc); this.pc = 0x38; return 16;

      default: return 4; // opcodes invalidos: NOP
    }
  }
}
