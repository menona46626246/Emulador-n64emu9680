// Temporizadores: DIV, TIMA/TMA/TAC

const PERIODS = [1024, 16, 64, 256];

export class Timer {
  constructor(mmu) {
    this.mmu = mmu;
    this.reset();
  }
  reset() {
    this.div = 0xAB;
    this.tima = 0;
    this.tma = 0;
    this.tac = 0xF8 & 7;
    this.divCounter = 0;
    this.timaCounter = 0;
  }
  resetDiv() {
    this.div = 0;
    this.divCounter = 0;
    this.timaCounter = 0;
  }
  tick(cycles) {
    this.divCounter += cycles;
    while (this.divCounter >= 256) {
      this.divCounter -= 256;
      this.div = (this.div + 1) & 0xFF;
    }
    if (this.tac & 0x04) {
      this.timaCounter += cycles;
      const period = PERIODS[this.tac & 3];
      while (this.timaCounter >= period) {
        this.timaCounter -= period;
        this.tima++;
        if (this.tima > 0xFF) {
          this.tima = this.tma;
          this.mmu.requestInterrupt(0x04);
        }
      }
    }
  }
}
