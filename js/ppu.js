// PPU: renderizado por linea de barrido (scanline), modos LCD, sprites, ventana

export const PALETTES = {
  verde: [[0xE0, 0xF8, 0xD0], [0x88, 0xC0, 0x70], [0x34, 0x68, 0x56], [0x08, 0x18, 0x20]],
  gris: [[0xFF, 0xFF, 0xFF], [0xAA, 0xAA, 0xAA], [0x55, 0x55, 0x55], [0x00, 0x00, 0x00]],
  ambar: [[0xFF, 0xF6, 0xD3], [0xF9, 0xA8, 0x75], [0xEB, 0x6B, 0x6F], [0x7C, 0x3F, 0x58]],
};

export class PPU {
  constructor(mmu) {
    this.mmu = mmu;
    this.vram = new Uint8Array(0x2000);
    this.oam = new Uint8Array(0xA0);
    this.framebuffer = new Uint8ClampedArray(160 * 144 * 4);
    this.bgLine = new Uint8Array(160); // indices de color del fondo (para prioridad de sprites)
    this.colors = PALETTES.verde;
    this.reset();
  }

  reset() {
    this.vram.fill(0);
    this.oam.fill(0);
    this.lcdc = 0x91;
    this.stat = 0x85;
    this.scy = 0; this.scx = 0;
    this.line = 0; this.lyc = 0;
    this.bgp = 0xFC; this.obp0 = 0xFF; this.obp1 = 0xFF;
    this.wy = 0; this.wx = 0;
    this.mode = 1;
    this.clock = 0;
    this.windowLine = 0;
    this.frameReady = false;
    // pantalla en blanco
    const c = this.colors[0];
    for (let i = 0; i < 160 * 144; i++) {
      this.framebuffer[i * 4] = c[0];
      this.framebuffer[i * 4 + 1] = c[1];
      this.framebuffer[i * 4 + 2] = c[2];
      this.framebuffer[i * 4 + 3] = 255;
    }
  }

  setPalette(name) {
    this.colors = PALETTES[name] || PALETTES.verde;
  }

  readSTAT() {
    return 0x80 | (this.stat & 0x78) | (this.line === this.lyc ? 0x04 : 0) | this.mode;
  }
  writeSTAT(v) { this.stat = (this.stat & 0x07) | (v & 0x78); }

  writeLCDC(v) {
    const wasOn = (this.lcdc & 0x80) !== 0;
    this.lcdc = v;
    const isOn = (v & 0x80) !== 0;
    if (wasOn && !isOn) {
      this.line = 0;
      this.clock = 0;
      this.mode = 0;
      this.windowLine = 0;
      // pantalla apagada -> blanco
      const c = this.colors[0];
      for (let i = 0; i < 160 * 144; i++) {
        this.framebuffer[i * 4] = c[0];
        this.framebuffer[i * 4 + 1] = c[1];
        this.framebuffer[i * 4 + 2] = c[2];
      }
    } else if (!wasOn && isOn) {
      this.clock = 0;
      this.mode = 2;
      this.checkLYC();
    }
  }

  checkLYC() {
    if (!(this.lcdc & 0x80)) return;
    if (this.line === this.lyc && (this.stat & 0x40)) {
      this.mmu.requestInterrupt(0x02);
    }
  }

  tick(cycles) {
    if (!(this.lcdc & 0x80)) return;
    this.clock += cycles;

    switch (this.mode) {
      case 2: // OAM scan
        if (this.clock >= 80) {
          this.clock -= 80;
          this.mode = 3;
        }
        break;
      case 3: // dibujo
        if (this.clock >= 172) {
          this.clock -= 172;
          this.mode = 0;
          this.renderScanline();
          if (this.stat & 0x08) this.mmu.requestInterrupt(0x02);
        }
        break;
      case 0: // HBlank
        if (this.clock >= 204) {
          this.clock -= 204;
          this.line++;
          if (this.line === 144) {
            this.mode = 1;
            this.frameReady = true;
            this.mmu.requestInterrupt(0x01);
            if (this.stat & 0x10) this.mmu.requestInterrupt(0x02);
          } else {
            this.mode = 2;
            if (this.stat & 0x20) this.mmu.requestInterrupt(0x02);
          }
          this.checkLYC();
        }
        break;
      case 1: // VBlank
        if (this.clock >= 456) {
          this.clock -= 456;
          this.line++;
          if (this.line > 153) {
            this.line = 0;
            this.windowLine = 0;
            this.mode = 2;
            if (this.stat & 0x20) this.mmu.requestInterrupt(0x02);
          }
          this.checkLYC();
        }
        break;
    }
  }

  renderScanline() {
    const ly = this.line;
    if (ly >= 144) return;
    const lcdc = this.lcdc;
    const bgLine = this.bgLine;
    const fb = this.framebuffer;
    const colors = this.colors;
    const rowBase = ly * 160 * 4;

    // ---- fondo ----
    if (lcdc & 0x01) {
      const mapBase = (lcdc & 0x08) ? 0x1C00 : 0x1800;
      const unsignedTiles = (lcdc & 0x10) !== 0;
      const yy = (ly + this.scy) & 0xFF;
      const tileRow = (yy >> 3) << 5;
      const py = (yy & 7) << 1;
      for (let x = 0; x < 160; x++) {
        const xx = (x + this.scx) & 0xFF;
        let tile = this.vram[mapBase + tileRow + (xx >> 3)];
        let tileAddr;
        if (unsignedTiles) tileAddr = tile << 4;
        else tileAddr = 0x1000 + ((tile << 24 >> 24) << 4);
        const lo = this.vram[tileAddr + py];
        const hi = this.vram[tileAddr + py + 1];
        const bit = 7 - (xx & 7);
        const ci = (((hi >> bit) & 1) << 1) | ((lo >> bit) & 1);
        bgLine[x] = ci;
        const shade = (this.bgp >> (ci << 1)) & 3;
        const c = colors[shade];
        const o = rowBase + x * 4;
        fb[o] = c[0]; fb[o + 1] = c[1]; fb[o + 2] = c[2]; fb[o + 3] = 255;
      }
    } else {
      const c = colors[0];
      for (let x = 0; x < 160; x++) {
        bgLine[x] = 0;
        const o = rowBase + x * 4;
        fb[o] = c[0]; fb[o + 1] = c[1]; fb[o + 2] = c[2]; fb[o + 3] = 255;
      }
    }

    // ---- ventana ----
    if ((lcdc & 0x20) && (lcdc & 0x01) && this.wy <= ly && this.wx <= 166) {
      const mapBase = (lcdc & 0x40) ? 0x1C00 : 0x1800;
      const unsignedTiles = (lcdc & 0x10) !== 0;
      const wl = this.windowLine;
      const tileRow = (wl >> 3) << 5;
      const py = (wl & 7) << 1;
      const startX = this.wx - 7;
      let drawn = false;
      for (let x = Math.max(0, startX); x < 160; x++) {
        const wxp = x - startX;
        let tile = this.vram[mapBase + tileRow + (wxp >> 3)];
        let tileAddr;
        if (unsignedTiles) tileAddr = tile << 4;
        else tileAddr = 0x1000 + ((tile << 24 >> 24) << 4);
        const lo = this.vram[tileAddr + py];
        const hi = this.vram[tileAddr + py + 1];
        const bit = 7 - (wxp & 7);
        const ci = (((hi >> bit) & 1) << 1) | ((lo >> bit) & 1);
        bgLine[x] = ci;
        const shade = (this.bgp >> (ci << 1)) & 3;
        const c = colors[shade];
        const o = rowBase + x * 4;
        fb[o] = c[0]; fb[o + 1] = c[1]; fb[o + 2] = c[2];
        drawn = true;
      }
      if (drawn) this.windowLine++;
    }

    // ---- sprites ----
    if (lcdc & 0x02) {
      const height = (lcdc & 0x04) ? 16 : 8;
      // seleccionar hasta 10 sprites visibles (orden OAM)
      const list = [];
      for (let i = 0; i < 40 && list.length < 10; i++) {
        const sy = this.oam[i * 4] - 16;
        if (ly >= sy && ly < sy + height) list.push(i);
      }
      // prioridad DMG: menor X gana; empate -> menor indice OAM.
      // Dibujamos de menor a mayor prioridad para que la mayor sobrescriba.
      list.sort((p, q) => {
        const xp = this.oam[p * 4 + 1], xq = this.oam[q * 4 + 1];
        return xp !== xq ? xq - xp : q - p;
      });
      for (const i of list) {
        const base = i * 4;
        const sy = this.oam[base] - 16;
        const sx = this.oam[base + 1] - 8;
        let tile = this.oam[base + 2];
        const attr = this.oam[base + 3];
        if (height === 16) tile &= 0xFE;
        let row = ly - sy;
        if (attr & 0x40) row = height - 1 - row; // flip Y
        const tileAddr = (tile << 4) + (row << 1);
        const lo = this.vram[tileAddr];
        const hi = this.vram[tileAddr + 1];
        const pal = (attr & 0x10) ? this.obp1 : this.obp0;
        for (let px = 0; px < 8; px++) {
          const x = sx + px;
          if (x < 0 || x >= 160) continue;
          const bit = (attr & 0x20) ? px : 7 - px; // flip X
          const ci = (((hi >> bit) & 1) << 1) | ((lo >> bit) & 1);
          if (ci === 0) continue; // transparente
          if ((attr & 0x80) && bgLine[x] !== 0) continue; // detras del fondo
          const shade = (pal >> (ci << 1)) & 3;
          const c = colors[shade];
          const o = rowBase + x * 4;
          fb[o] = c[0]; fb[o + 1] = c[1]; fb[o + 2] = c[2];
        }
      }
    }
  }
}
