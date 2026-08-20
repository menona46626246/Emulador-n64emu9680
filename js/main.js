// Interfaz: carga de ROMs, bucle de emulacion, audio, teclado/tactil, guardado

import { GameBoy } from './gameboy.js';

const canvas = document.getElementById('screen');
const ctx = canvas.getContext('2d');
const imageData = ctx.createImageData(160, 144);
const overlay = document.getElementById('overlay');
const powerLed = document.getElementById('powerLed');
const romTitleEl = document.getElementById('romTitle');
const fpsEl = document.getElementById('fps');

const btnPause = document.getElementById('btnPause');
const btnReset = document.getElementById('btnReset');
const btnTurbo = document.getElementById('btnTurbo');
const btnMute = document.getElementById('btnMute');
const paletteSel = document.getElementById('paletteSel');

let gb = null;
let running = false;
let paused = false;
let turbo = false;
let muted = false;
let audioCtx = null;
let scriptNode = null;
let saveKey = null;
let saveTimer = 0;

// ---------- audio ----------
function initAudio() {
  if (audioCtx) return;
  audioCtx = new (window.AudioContext || window.webkitAudioContext)();
  scriptNode = audioCtx.createScriptProcessor(2048, 0, 2);
  scriptNode.onaudioprocess = (e) => {
    const L = e.outputBuffer.getChannelData(0);
    const R = e.outputBuffer.getChannelData(1);
    if (gb && running && !paused && !muted && !turbo) {
      gb.apu.fill(L, R);
    } else {
      L.fill(0); R.fill(0);
    }
  };
  scriptNode.connect(audioCtx.destination);
}

// ---------- guardado (RAM con bateria) ----------
function saveKeyFor(mmu) {
  let sum = 0;
  for (let i = 0x134; i < 0x150; i++) sum = (sum * 31 + mmu.rom[i]) >>> 0;
  return `gb-save-${mmu.title}-${sum.toString(16)}`;
}
function persistSave() {
  if (!gb || !saveKey) return;
  const data = gb.saveData;
  if (!data) return;
  try {
    let bin = '';
    for (let i = 0; i < data.length; i++) bin += String.fromCharCode(data[i]);
    localStorage.setItem(saveKey, btoa(bin));
  } catch (e) { /* almacenamiento lleno */ }
}
function restoreSave() {
  if (!gb || !saveKey) return;
  const b64 = localStorage.getItem(saveKey);
  if (!b64) return;
  try {
    const bin = atob(b64);
    const bytes = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
    gb.saveData = bytes;
  } catch (e) { /* ignorar */ }
}
window.addEventListener('beforeunload', persistSave);

// ---------- carga de ROM ----------
function loadROM(bytes, name) {
  initAudio();
  if (audioCtx.state === 'suspended') audioCtx.resume();
  persistSave(); // guardar la partida anterior

  gb = new GameBoy(audioCtx.sampleRate);
  gb.ppu.setPalette(paletteSel.value);
  try {
    gb.loadROM(bytes);
  } catch (e) {
    alert('No se pudo cargar la ROM: ' + e.message);
    return;
  }
  saveKey = saveKeyFor(gb.mmu);
  restoreSave();

  const title = gb.mmu.title || name || 'ROM';
  romTitleEl.textContent = `🕹️ ${title}`;
  overlay.classList.add('hidden');
  powerLed.classList.add('on');
  btnPause.disabled = btnReset.disabled = btnTurbo.disabled = false;
  paused = false;
  btnPause.textContent = '⏸️ Pausa';
  btnPause.classList.remove('active');
  running = true;
}

document.getElementById('romInput').addEventListener('change', async (e) => {
  const file = e.target.files[0];
  if (!file) return;
  const buf = await file.arrayBuffer();
  loadROM(new Uint8Array(buf), file.name.replace(/\.[^.]+$/, ''));
  e.target.value = '';
});

// juegos incluidos
document.querySelectorAll('.homebrew .game').forEach((btn) => {
  btn.addEventListener('click', async () => {
    const res = await fetch(btn.dataset.rom);
    const buf = await res.arrayBuffer();
    loadROM(new Uint8Array(buf), btn.textContent.trim());
  });
});

// drag & drop
const shell = document.querySelector('.screen-frame');
['dragover', 'dragenter'].forEach((ev) =>
  document.body.addEventListener(ev, (e) => {
    e.preventDefault();
    overlay.classList.add('dragging');
  })
);
['dragleave', 'drop'].forEach((ev) =>
  document.body.addEventListener(ev, (e) => {
    e.preventDefault();
    overlay.classList.remove('dragging');
  })
);
document.body.addEventListener('drop', async (e) => {
  const file = e.dataTransfer.files[0];
  if (!file) return;
  const buf = await file.arrayBuffer();
  loadROM(new Uint8Array(buf), file.name.replace(/\.[^.]+$/, ''));
});

// ---------- botones ----------
btnPause.addEventListener('click', () => {
  paused = !paused;
  btnPause.textContent = paused ? '▶️ Continuar' : '⏸️ Pausa';
  btnPause.classList.toggle('active', paused);
});
btnReset.addEventListener('click', () => {
  if (!gb) return;
  persistSave();
  gb.reset();
  restoreSave();
});
btnTurbo.addEventListener('click', () => {
  turbo = !turbo;
  btnTurbo.classList.toggle('active', turbo);
});
btnMute.addEventListener('click', () => {
  muted = !muted;
  btnMute.textContent = muted ? '🔇 Silencio' : '🔊 Sonido';
  btnMute.classList.toggle('active', muted);
});
paletteSel.addEventListener('change', () => {
  if (gb) gb.ppu.setPalette(paletteSel.value);
});

// ---------- teclado ----------
const KEYMAP = {
  ArrowUp: 'up', ArrowDown: 'down', ArrowLeft: 'left', ArrowRight: 'right',
  KeyX: 'a', KeyZ: 'b',
  Enter: 'start', ShiftLeft: 'select', ShiftRight: 'select', Backspace: 'select',
};
window.addEventListener('keydown', (e) => {
  if (e.repeat) return;
  if (e.code === 'Space') { turbo = true; btnTurbo.classList.add('active'); e.preventDefault(); return; }
  if (e.code === 'KeyP') { btnPause.click(); return; }
  const k = KEYMAP[e.code];
  if (k && gb) { gb.joypad.press(k); e.preventDefault(); }
});
window.addEventListener('keyup', (e) => {
  if (e.code === 'Space') { turbo = false; btnTurbo.classList.remove('active'); return; }
  const k = KEYMAP[e.code];
  if (k && gb) gb.joypad.release(k);
});

// ---------- controles tactiles / raton ----------
document.querySelectorAll('[data-key]').forEach((btn) => {
  const key = btn.dataset.key;
  const press = (e) => { e.preventDefault(); btn.classList.add('pressed'); if (gb) gb.joypad.press(key); };
  const release = (e) => { e.preventDefault(); btn.classList.remove('pressed'); if (gb) gb.joypad.release(key); };
  btn.addEventListener('mousedown', press);
  btn.addEventListener('mouseup', release);
  btn.addEventListener('mouseleave', release);
  btn.addEventListener('touchstart', press, { passive: false });
  btn.addEventListener('touchend', release, { passive: false });
  btn.addEventListener('touchcancel', release, { passive: false });
});

// ---------- bucle principal ----------
let lastTime = performance.now();
let acc = 0;
const FRAME_MS = 1000 / 59.7275;
let fpsCount = 0;
let fpsTime = performance.now();

function mainLoop(now) {
  requestAnimationFrame(mainLoop);
  if (!gb || !running || paused) { lastTime = now; return; }

  acc += now - lastTime;
  lastTime = now;
  if (acc > 250) acc = 250; // evitar espiral tras pestana inactiva

  const framesToRun = turbo ? 4 : Math.floor(acc / FRAME_MS);
  if (!turbo) acc -= framesToRun * FRAME_MS;
  else acc = 0;

  let fb = null;
  for (let i = 0; i < Math.min(framesToRun, 6); i++) {
    fb = gb.runFrame();
    fpsCount++;
  }
  if (fb) {
    imageData.data.set(fb);
    ctx.putImageData(imageData, 0, 0);
  }

  if (now - fpsTime > 1000) {
    fpsEl.textContent = `${fpsCount} fps`;
    fpsCount = 0;
    fpsTime = now;
  }

  // autoguardado cada ~5 s
  if (++saveTimer > 300) { saveTimer = 0; persistSave(); }
}
requestAnimationFrame(mainLoop);

// pantalla inicial
(function splash() {
  const c = [224, 248, 208];
  for (let i = 0; i < 160 * 144; i++) {
    imageData.data[i * 4] = c[0];
    imageData.data[i * 4 + 1] = c[1];
    imageData.data[i * 4 + 2] = c[2];
    imageData.data[i * 4 + 3] = 255;
  }
  ctx.putImageData(imageData, 0, 0);
})();
