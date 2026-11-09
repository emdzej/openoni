// OpenOni in the browser: openoni.wasm in gasm's Worker mode. The worker runs the game and reads the data
// on demand, either from the site's OPFS copy (FileSystemSyncAccessHandle) or straight from the picked files
// (FileReaderSync). The game draws through gasm:gl (OpenGL ES 3.0 / WebGL 2): the page hands the worker its
// canvas (transferControlToOffscreen) and sends the display size with each batch. The page keeps input
// (keyboard, and the mouse with pointer lock) and audio.
//
// Test and debug query parameters:
//   hashframes=N   run N frames on virtual time as fast as possible, then print the same
//                  "frames=... video_fnv32=... audio_fnv32=..." line as gasm-run --headless
//                  (globalThis.__openoniResult)
//   batch=N        with hashframes: frames per worker batch (default 1: only a batch's last frame is shown,
//                  and the game draws only shown frames, so bigger batches draw less than gasm-run)
//   input=SCRIPT   with hashframes: scripted input, gasm-run's --input syntax
//   size=WxH       the GL drawing size instead of the display's (hash runs: 1280x720, as gasm-run --headless)
//   autoplay       start the imported data right away
//   any parameter the game reads (play, level, at, route, ...) is passed through
import { BrowserInput, ProcExit, Resampler } from './vendor/gasm/gasm-host.js';
import { GasmWorker } from './vendor/gasm/gasm-worker.js';
import { InputScript } from './vendor/gasm/input-script.js';
import * as data from './data.js';

const $ = (id) => document.getElementById(id);
const query = new URLSearchParams(location.search);
const PAGE_PARAMS = new Set(['hashframes', 'batch', 'input', 'size', 'autoplay']);
const HASH_FRAMES = Number(query.get('hashframes') || 0);
const BATCH = Number(query.get('batch') || 1);
const FIXED_SIZE = /^(\d+)x(\d+)$/.exec(query.get('size') ?? '')?.slice(1).map(Number) ?? (HASH_FRAMES > 0 ? [1280, 720] : null);
const STORAGE = 'openoni';
const TITLE = 'Oni';
const hasOpfs = !!navigator.storage?.getDirectory;
const hasGl = (() => { try { return !!document.createElement('canvas').getContext('webgl2'); } catch { return false; } })();
const hasOffscreen = typeof HTMLCanvasElement !== 'undefined' && 'transferControlToOffscreen' in HTMLCanvasElement.prototype;

function message(text, kind = 'info') {
  const m = $('message');
  m.hidden = !text;
  m.textContent = text ?? '';
  m.className = `message ${kind}`;
  if (text) console.log(`[play] ${text}`);
}
const show = (id, on) => { $(id).hidden = !on; };

// ---- the canvas -----------------------------------------------------------------------------
// A canvas can be transferred to a worker only once: every start gets a fresh one.
let canvas = $('screen');
function freshCanvas() {
  const c = document.createElement('canvas');
  c.id = 'screen';
  c.tabIndex = 0;
  c.setAttribute('aria-label', TITLE);
  c.ondblclick = () => $('fullscreen').onclick();
  canvas.replaceWith(c);
  canvas = c;
  keyboard.setElement(c);
  return c;
}
const displaySize = () => FIXED_SIZE ?? [Math.max(1, Math.round(canvas.clientWidth * (devicePixelRatio || 1))) || 1280,
                                         Math.max(1, Math.round(canvas.clientHeight * (devicePixelRatio || 1))) || 720];

// ---- keyboard and mouse ---------------------------------------------------------------------
// The game reads the raw keyboard and the mouse itself (src/platform_gasm.c maps gasm's keys to the
// DirectInput codes the original reads; a click locks the pointer for aiming).
const typing = (e) => e.target instanceof HTMLInputElement || e.target instanceof HTMLTextAreaElement || e.target instanceof HTMLSelectElement;
const keyboard = new BrowserInput(canvas, { ignore: (e) => !running || typing(e) }).attach();
const browserKey = (e) => e.metaKey || (e.ctrlKey && /^Key[A-Z]$/.test(e.code) && e.code !== 'KeyC' && e.code !== 'KeyF');
addEventListener('keydown', (e) => {
  if (!running || typing(e)) return;
  resumeAudio();
  if (!browserKey(e)) e.preventDefault();
});

// ---- audio: the gasm web player's AudioWorklet queue ------------------------------------------
const WORKLET = `
class GasmOut extends AudioWorkletProcessor {
  constructor() {
    super();
    this.q = []; this.off = 0; this.len = 0; this.primed = false;
    this.target = Math.round(sampleRate * 0.06) * 2; this.max = Math.round(sampleRate * 0.2) * 2;
    this.port.onmessage = (e) => {
      this.q.push(e.data); this.len += e.data.length;
      while (this.len > this.max && this.q.length > 1) { this.len -= this.q[0].length - this.off; this.q.shift(); this.off = 0; }
    };
  }
  process(_, [out]) {
    const L = out[0], R = out[1] ?? out[0];
    if (!this.primed && this.len >= this.target) this.primed = true;
    for (let i = 0; i < L.length; i++) {
      if (!this.primed || this.len < 2) { this.primed = false; L[i] = R[i] = 0; continue; }
      const b = this.q[0];
      L[i] = b[this.off]; R[i] = b[this.off + 1];
      this.off += 2; this.len -= 2;
      if (this.off >= b.length) { this.q.shift(); this.off = 0; }
    }
    return true;
  }
}
registerProcessor('gasm-out', GasmOut);`;

let audioCtx = null, audioNode = null, gain = null, resampler = null, audioReady = null;
let muted = localStorage.getItem('openoni.muted') === '1';
/** Call from a click handler (browsers only start audio on a user gesture). */
function startAudio() {
  if (!audioCtx) {
    try {
      audioCtx = new AudioContext({ latencyHint: 'interactive' });
    } catch (e) { message(`No sound: ${e.message}`, 'warn'); return; }
    audioReady = audioCtx.audioWorklet.addModule(URL.createObjectURL(new Blob([WORKLET], { type: 'text/javascript' }))).then(() => {
      audioNode = new AudioWorkletNode(audioCtx, 'gasm-out', { outputChannelCount: [2] });
      gain = new GainNode(audioCtx, { gain: muted ? 0 : 1 });
      audioNode.connect(gain).connect(audioCtx.destination);
      resampler = new Resampler(audioCtx.sampleRate);
    }).catch((e) => { message(`No sound: ${e.message}`, 'warn'); audioCtx = null; });
  }
  audioCtx?.resume();
}
function resumeAudio() { if (audioCtx && audioCtx.state !== 'running') audioCtx.resume().catch(() => {}); }
document.addEventListener('visibilitychange', () => { if (!document.hidden) resumeAudio(); });
addEventListener('pointerdown', resumeAudio);
function onAudio(samples, rate, channels) {
  if (!audioNode) return;
  const out = resampler.process(samples, rate, channels);
  audioNode.port.postMessage(out.slice());
}
function setMuted(m) {
  muted = m;
  localStorage.setItem('openoni.muted', m ? '1' : '0');
  if (gain) gain.gain.value = m ? 0 : 1;
  $('sound').textContent = m ? 'Sound: off' : 'Sound: on';
}
$('sound').onclick = () => { startAudio(); setMuted(!muted); };
setMuted(muted);

$('fullscreen').onclick = () => {
  if (document.fullscreenElement) document.exitFullscreen();
  else ($('stage').requestFullscreen ?? $('stage').webkitRequestFullscreen)?.call($('stage'));
};
canvas.ondblclick = () => $('fullscreen').onclick();
function gameFullscreen(on) {
  if (on && !document.fullscreenElement) $('stage').requestFullscreen?.().catch(() => {});
  else if (!on && document.fullscreenElement) document.exitFullscreen().catch(() => {});
}
document.addEventListener('fullscreenchange', () => { if (worker) worker.fullscreen = !!document.fullscreenElement; });

// ---- running the game ---------------------------------------------------------------------
let worker = null, running = false, inflight = false, rafId = 0, wasmBytes = null, lastLog = '', wantPng = null;
let acc = 0, last = 0, fpsN = 0, fpsT = 0;

async function loadWasm() {
  if (!wasmBytes) {
    const r = await fetch(new URL('openoni.wasm', import.meta.url));
    if (!r.ok) throw new Error(`openoni.wasm: HTTP ${r.status}`);
    wasmBytes = await r.arrayBuffer();
  }
  return wasmBytes.slice(0);
}

function onLog(msg) {
  console.log(msg);
  if (/^\[guest\]/.test(msg)) lastLog = msg.replace(/^\[guest\]\s*/, '');
}

async function stopGame() {
  cancelAnimationFrame(rafId);
  running = false; inflight = false;
  const w = worker; worker = null;
  await w?.exit();
}

function stopped(e) {
  running = false;
  if (e instanceof ProcExit) message('The game has ended. Press Play to start again.');
  else message(`The game stopped: ${e.message}${lastLog ? ` (${lastLog})` : ''}`, 'error');
  stopGame();
  endGameView();
}

function endGameView() {
  cancelAnimationFrame(rafId);
  show('game', false);
  show('setup', true);
  refreshImported();
}

/** Start the game from 'opfs' (the imported data) or a checked source (read in place). */
async function play(source) {
  startAudio();
  message('');
  await stopGame();
  lastLog = '';
  if (!hasGl || !hasOffscreen) {
    message('This browser can\'t run OpenOni: it needs WebGL 2 and OffscreenCanvas (a recent Chrome, Edge, Firefox or Safari 17 and later).', 'error');
    return;
  }
  show('game', true);
  show('setup', HASH_FRAMES > 0);
  $('status').textContent = 'Starting...';
  const params = Object.fromEntries([...query].filter(([k]) => !PAGE_PARAMS.has(k)));
  // the main menu unless a level is picked (or asked for in the address)
  const pick = $('level').value;
  if (!params.play && !params.viewer && pick !== 'menu') params.play = '1';
  if (!params.level && pick !== 'menu') params.level = pick;
  if (!params.intro && $('skip-intro').checked) params.intro = '0';
  const c = freshCanvas();
  try {
    await audioReady;
    worker = await GasmWorker.start({
      wasm: await loadWasm(), assets: data.assetSpecs(source), params,
      storage: HASH_FRAMES > 0 ? null : STORAGE,
      hashing: HASH_FRAMES > 0, virtualTime: HASH_FRAMES > 0, onLog, onAudio, keyboard: true,
      glCanvas: c.transferControlToOffscreen(), size: displaySize(),
      onTitle: (t) => { document.title = `${t ?? TITLE} | OpenOni`; },
      onFullscreen: gameFullscreen,
    });
  } catch (e) {
    worker = null;
    const busy = /NoModificationAllowed|InvalidState|lock|access handle/i.test(`${e.name} ${e.message}`);
    if (busy) message('The imported data is in use by another OpenOni tab or window. Close it and try again.', 'error');
    else if (e instanceof ProcExit) message(`The game could not start${lastLog ? `: ${lastLog}` : ''}.`, 'error');
    else message(`The game could not start: ${e.message}`, 'error');
    endGameView();
    return;
  }
  document.title = `${TITLE} | OpenOni`;
  if (HASH_FRAMES > 0) return hashRun(HASH_FRAMES);
  canvas.focus?.();
  acc = 0; last = performance.now(); running = true;
  rafId = requestAnimationFrame(tick);
}

function tick(now) {
  rafId = requestAnimationFrame(tick);
  if (!running || !worker) return;
  const period = 1000 / worker.frameRate;
  acc += Math.min(now - last, 100);
  last = now;
  // Fixed timestep; when catching up (at most 4 frames), only the last one is shown.
  const due = Math.min(4, Math.floor(acc / period));
  if (!inflight && due > 0) {
    inflight = true;
    acc -= due * period;
    const steps = Array.from({ length: due }, (_, k) => ({ input: keyboard.frame(k === 0) }));
    const copy = !!wantPng;
    worker.frames(steps, true, { size: displaySize(), copy }).then((r) => {
      inflight = false;
      fpsN += due;
      if (copy) { wantPng(r.png ?? null); wantPng = null; }
      keyboard.setMode(worker?.inputMode ?? 0);
    }, stopped);
  }
  if (acc > period * 4) acc = 0;
  if (now - fpsT >= 1000) { $('status').textContent = `${fpsN} frames/s`; fpsN = 0; fpsT = now; }
}

/** The next frame the game draws, as a PNG Blob (tests; the WebGL canvas belongs to the worker). */
function framePng() { return new Promise((r) => { wantPng = r; }); }

const hex = (h) => (h >>> 0).toString(16).padStart(8, '0');
/** gasm-run --headless N [--input SCRIPT]: the same frames, the same input, the same hash line. */
async function hashRun(n) {
  let script;
  try { script = new InputScript(query.get('input') ?? ''); } catch (e) { message(`input: ${e.message}`, 'error'); return; }
  const st = {};
  let done = 0, png = null;
  try {
    while (done < n) {
      const k = Math.min(BATCH, n - done);
      const steps = Array.from({ length: k }, (_, i) => {
        const f = done + i;
        return { pads: [script.pad(f), 0, 0, 0], text: script.textAt(f), input: script.raw(f, st, displaySize(), worker.inputMode) };
      });
      const r = await worker.frames(steps, done + k === n, { size: displaySize(), copy: done + k === n });
      png = r.png ?? png;
      done += k;
    }
  } catch (e) { if (!(e instanceof ProcExit)) { message(`hash run failed: ${e.message}`, 'error'); throw e; } }
  const s = worker.stats;
  const out = `frames=${s.frames} presented=${s.presented} size=${s.width}x${s.height} ` +
              `video_fnv32=${hex(s.videoHash)} audio_fnv32=${hex(s.audioHash)} audio_frames=${s.audioFrames}`;
  $('status').textContent = out;
  console.log(out);
  globalThis.__openoniPng = png;
  globalThis.__openoniResult = out;
}

$('stop').onclick = async () => { await stopGame(); endGameView(); };
addEventListener('pagehide', () => { worker?.exit(); });

// ---- choosing and importing the data --------------------------------------------------------
let checked = null;

function showCheck(result) {
  checked = result;
  const list = $('check-list');
  list.textContent = '';
  const item = (text, ok) => {
    const li = document.createElement('li');
    li.className = ok ? 'ok' : 'bad';
    li.textContent = text;
    list.append(li);
  };
  const src = result.source;
  if (src.kind === 'image') item(`disc image "${src.name}": ${data.mb(result.bytes)}`, true);
  else item(`folder "${src.name}": ${result.files} files, ${data.mb(result.bytes)}`, true);
  if (result.problems.length) {
    for (const p of result.problems) item(p, false);
    item('This does not look like Oni: choose an .iso of the CD, the CD\'s folder (ONISETUP.EXE and ONISETUP.002), or the installed game\'s folder (Oni.exe and GameDataFolder).', false);
  } else if (src.kind === 'image') {
    item('ISO 9660 volume found: the game reads the CD\'s installer archive inside it (it says so if Oni isn\'t on it)', true);
  } else if (src.cd) {
    item('the CD\'s installer archive found (ONISETUP.EXE and ONISETUP.002): the game reads the installed files out of it', true);
  } else {
    item('Oni.exe (v1.0) and GameDataFolder found', true);
  }
  show('check', true);
  show('check-actions', !result.problems.length);
  $('import').hidden = !hasOpfs;
  globalThis.__openoniChecked = { problems: result.problems, files: result.files, bytes: result.bytes };
}

$('pick-folder').onclick = async () => {
  message('');
  if (!window.showDirectoryPicker) return $('folder-input').click();
  let handle;
  try { handle = await showDirectoryPicker({ id: 'openoni-data', mode: 'read' }); } catch (e) {
    if (e.name !== 'AbortError') message(e.message, 'error');
    return;
  }
  message('Reading the folder...');
  try { showCheck(await data.folderFromHandle(handle)); message(''); } catch (e) { message(`Could not read the folder: ${e.message}`, 'error'); }
};
$('folder-input').onchange = (e) => {
  if (e.target.files.length) showCheck(data.folderFromFileList(e.target.files));
  e.target.value = '';
};

$('pick-image').onclick = () => { message(''); $('image-input').click(); };
$('image-input').onchange = async (e) => {
  const f = e.target.files[0];
  e.target.value = '';
  if (!f) return;
  try { showCheck(await data.imageFromFile(f)); } catch (err) { message(`Could not read the image: ${err.message}`, 'error'); }
};

$('import').onclick = async () => {
  if (!checked) return;
  startAudio();
  const source = checked.source;
  show('check', false);
  show('progress', true);
  $('pick-folder').disabled = $('pick-image').disabled = true;
  const t0 = performance.now();
  try {
    await stopGame();
    const info = await data.importSource(source, (p) => {
      $('progress-bar').value = p.total ? p.bytes / p.total : 1;
      $('progress-text').textContent = `Copying ${p.file}: ${data.mb(p.bytes)} of ${data.mb(p.total)} (${p.done} of ${p.files} files)`;
    });
    $('progress-text').textContent = `Imported ${info.files} files, ${data.mb(info.bytes)} in ${((performance.now() - t0) / 1000).toFixed(0)} s.`;
    globalThis.__openoniImported = info;
    await refreshImported();
    show('progress', false);
    await play('opfs');
  } catch (e) {
    show('progress', false);
    const full = /quota/i.test(`${e.name} ${e.message}`);
    message(full ? `Not enough browser storage for the game data (${data.mb(checked.bytes)}). Free some space, or play without importing.`
                 : `Import failed: ${e.message}. You can still play without importing.`, 'error');
    show('check', true);
    await data.removeImport().catch(() => {});
    refreshImported();
  } finally {
    $('pick-folder').disabled = $('pick-image').disabled = false;
  }
};
$('play-direct').onclick = () => { if (checked) play(checked.source); };
$('play-opfs').onclick = () => play('opfs');
$('remove').onclick = async () => {
  await stopGame();
  show('game', false);
  try { await data.removeImport(); message('The imported game data was removed from this browser.'); } catch (e) { message(`Could not remove it: ${e.message}`, 'error'); }
  refreshImported();
};
$('persist').onclick = async () => {
  const ok = await data.requestPersist();
  message(ok ? 'The browser will keep the imported data.' : 'The browser did not agree to keep the data; it stays until space runs low.', ok ? 'info' : 'warn');
  refreshImported();
};

async function refreshImported() {
  const info = hasOpfs ? await data.existingImport() : null;
  show('imported', !!info);
  $('choose-title').textContent = info ? 'Your game' : 'Choose your game';
  if (info) {
    $('imported-desc').textContent = info.kind === 'image' ? `disc image "${info.name}", ${data.mb(info.bytes)}`
      : `game folder "${info.name}", ${info.files} files, ${data.mb(info.bytes)}`;
    const s = await data.storageInfo();
    $('storage').textContent = s.usage !== undefined
      ? `This site uses ${data.mb(s.usage)} of the ${data.mb(s.quota ?? 0)} the browser allows${s.persisted ? '; kept even when space runs low' : ''}.`
      : '';
    show('persist-row', !s.persisted && !!navigator.storage?.persist);
  }
  return info;
}

$('level').value = localStorage.getItem('openoni.level') ?? 'menu';
if ($('level').selectedIndex < 0) $('level').value = 'menu';
$('level').onchange = () => localStorage.setItem('openoni.level', $('level').value);
$('skip-intro').checked = localStorage.getItem('openoni.skipIntro') === '1';
$('skip-intro').onchange = () => localStorage.setItem('openoni.skipIntro', $('skip-intro').checked ? '1' : '0');

if (!hasOpfs) message('This browser has no private file storage (OPFS), so the game data can\'t be imported. Play without importing works.', 'warn');
if (typeof Worker === 'undefined' || typeof WebAssembly === 'undefined') message('This browser can\'t run OpenOni (it needs WebAssembly and Web Workers).', 'error');
else if (!hasGl || !hasOffscreen) message('This browser can\'t run OpenOni: it needs WebGL 2 and OffscreenCanvas.', 'error');
const ready = refreshImported();
ready.then((info) => {
  globalThis.__openoniReady = true;
  if (info && query.has('autoplay')) play('opfs');
});
globalThis.openoniPlay = { play, stopGame, refreshImported, framePng, data };
