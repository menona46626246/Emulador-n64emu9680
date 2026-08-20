// Joypad (FF00)

export class Joypad {
  constructor(mmu) {
    this.mmu = mmu;
    this.reset();
  }
  reset() {
    this.select = 0x30; // bits 4-5 de P1
    // true = pulsado
    this.state = {
      right: false, left: false, up: false, down: false,
      a: false, b: false, select: false, start: false,
    };
  }
  write(v) { this.select = v & 0x30; }
  read() {
    let res = 0xC0 | this.select | 0x0F;
    const s = this.state;
    if (!(this.select & 0x10)) { // direcciones
      if (s.right) res &= ~0x01;
      if (s.left) res &= ~0x02;
      if (s.up) res &= ~0x04;
      if (s.down) res &= ~0x08;
    }
    if (!(this.select & 0x20)) { // botones
      if (s.a) res &= ~0x01;
      if (s.b) res &= ~0x02;
      if (s.select) res &= ~0x04;
      if (s.start) res &= ~0x08;
    }
    return res;
  }
  press(key) {
    if (key in this.state && !this.state[key]) {
      this.state[key] = true;
      this.mmu.requestInterrupt(0x10);
    }
  }
  release(key) {
    if (key in this.state) this.state[key] = false;
  }
}
