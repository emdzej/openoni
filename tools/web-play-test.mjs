#!/usr/bin/env node
// Headless Chrome test of the browser play page (docs/public/play/), through the page's own code: the
// game's folder is handed to its <input webkitdirectory> (CDP DOM.setFileInputFiles, as if picked), checked,
// imported into OPFS and run in gasm's Worker mode with WebGL 2 on the transferred canvas; then the
// imported copy is run again (a later visit), and the folder is run without importing (the File/Blob
// provider). Every run must print the hash line gasm-run --headless prints for the same frames and input
// (GL at 1280x720 on both: gasm-run's headless drawable, the page's hashframes default), except for what
// the page can't see: gasm-run (and gasm's Node runner) call the guest's gasm_exit before printing. The
// Worker API reports no stats after exit, so the page's line is the one before gasm_exit. The test checks
// that link too: the page against gasm's own GasmHost run in Node without the exit (same package, same
// input), and that one plus gasm_exit against gasm-run.
//
//   docs/scripts/copy-wasm.sh && (cd docs && scripts/vendor-web.sh && pnpm build)
//   node tools/web-play-test.mjs [game folder] [out dir]
// Defaults: ./game (the installed game, from openoni-extract), /tmp/openoni-play.
// Needs Chrome (CHROME=<binary>) and gasm-run (GASM_RUN=<binary>,
// default .deps/gasm-runner-macos-universal/gasm-run from tools/fetch-gasm-runner.sh). Serves
// docs/.vitepress/dist on 127.0.0.1:8793. Never opens a window.
import { spawn, execFileSync } from 'node:child_process';
import { existsSync, mkdirSync, mkdtempSync, rmSync, statSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const repo = fileURLToPath(new URL('..', import.meta.url));
const GAME = resolve(process.argv[2] ?? join(repo, 'game'));
const OUT = resolve(process.argv[3] ?? '/tmp/openoni-play');
const DIST = join(repo, 'docs/.vitepress/dist');
const WASM = join(DIST, 'play/openoni.wasm');   // the module the page runs: gasm-run gets the same one
const PORT = 8793, DEBUG = 9336;
const CHROME = process.env.CHROME ?? '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
/* The CD's image (OPENONI_CD=<.iso>, as the native tests take it): the image case runs only with one. */
const ISO = process.env.OPENONI_CD && /\.(iso|bin)$/i.test(process.env.OPENONI_CD) ? resolve(process.env.OPENONI_CD) : null;
const RUN = process.env.GASM_RUN ?? join(repo, '.deps/gasm-runner-macos-universal/gasm-run');
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
mkdirSync(OUT, { recursive: true });
if (!existsSync(WASM)) { console.error('build the site first (docs/scripts/copy-wasm.sh, scripts/vendor-web.sh, pnpm build)'); process.exit(1); }
if (!existsSync(CHROME)) { console.error(`no Chrome at ${CHROME} (CHROME=<binary>)`); process.exit(1); }

// Add cases (frames, scripted input and the game's parameters) as the game grows.
const CASES = [
  { name: 'level1', frames: Number(process.env.FRAMES ?? 600), input: '', params: { play: '1', level: '1' } },   // the training's splash screen
  { name: 'menu', frames: 90, input: '', params: {} },     // the main menu
];

// Reference hashes from the native runner (same module, same data, same input).
function native(c, dir) {
  if (!existsSync(RUN)) return null;
  const args = [WASM, ...(statSync(dir).isDirectory() ? ['--asset-dir', dir] : ['--asset', `cd=${dir}`]), '--headless', String(c.frames)];
  for (const [k, v] of Object.entries(c.params ?? {})) args.push('--param', `${k}=${v}`);
  if (c.input) args.push('--input', c.input);
  return execFileSync(RUN, args, { stdio: ['ignore', 'pipe', 'ignore'] }).toString().trim().split('\n').join(' ');
}

// gasm's GasmHost in Node on a folder (as headless.mjs builds it), N frames of the script; the hash line
// before and after gasm_exit.
const gasm = await import(pathToFileURL(join(repo, 'docs/node_modules/@emdzej/gasm-host/gasm-host.js')).href);
const { InputScript } = await import(pathToFileURL(join(repo, 'docs/node_modules/@emdzej/gasm-host/input-script.mjs')).href);
async function nodeRef(c, dir) {
  const { readdirSync, lstatSync, openSync, readSync, closeSync, readFileSync } = await import('node:fs');
  const table = new gasm.AssetTable(() => {});
  const fds = [];
  const walk = (abs, segs) => {
    for (const d of readdirSync(abs, { withFileTypes: true })) {
      if (d.name.startsWith('.') || d.isSymbolicLink()) continue;
      const path = join(abs, d.name), s = [...segs, d.name];
      if (d.isDirectory()) walk(path, s);
      else if (d.isFile()) {
        const size = lstatSync(path).size;
        let fd = null;
        table.add(s.join('/'), { size: () => size, readAt: (off, dst) => {
          fd ??= (fds.push(openSync(path, 'r')), fds.at(-1));
          let done = 0;
          while (done < dst.length) { const k = readSync(fd, dst, done, dst.length - done, off + done); if (!k) break; done += k; }
          return done;
        } }, { fromDir: true });
      }
    }
  };
  walk(dir, []);
  table.finish();
  const script = new InputScript(c.input);
  const host = new gasm.GasmHost({ assets: table, params: { ...c.params }, storage: new gasm.MemoryStorage(), virtualTime: true, onLog: () => {},
    getPad: (p) => (p !== 0 ? 0 : script.pad(host.frameIndex)) });
  host.hashing = true;
  await host.load(await WebAssembly.compile(readFileSync(WASM)));
  const st = {};
  for (let f = 0; f < c.frames; f++) {
    host.text = script.textAt(f);
    host.input = script.raw(f, st, [1280, 720], host.inputMode);
    host.frame();
  }
  const hex = (h) => (h >>> 0).toString(16).padStart(8, '0');
  const line = () => `frames=${host.frameIndex} presented=${host.framesPresented} size=${host.width}x${host.height} ` +
    `video_fnv32=${hex(host.videoHash)} audio_fnv32=${hex(host.audioHash)} audio_frames=${host.audioFrames}`;
  const before = line();
  host.exit();
  const after = line();
  for (const fd of fds) closeSync(fd);
  return { before, after };
}

const PROFILE = mkdtempSync(join(tmpdir(), 'openoni-play-chrome-'));
// A static server for the site (in-process: python's http.server drops connections when the machine is
// busy, and the page loads its modules in parallel).
const { createServer } = await import('node:http');
const { readFile } = await import('node:fs/promises');
const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.css': 'text/css', '.json': 'application/json',
  '.wasm': 'application/wasm', '.svg': 'image/svg+xml', '.png': 'image/png', '.woff2': 'font/woff2' };
const server = createServer(async (req, res) => {
  let path = decodeURIComponent(new URL(req.url, 'http://x').pathname);
  if (path.endsWith('/')) path += 'index.html';
  const file = join(DIST, path);
  if (!file.startsWith(DIST)) { res.writeHead(403).end(); return; }
  try {
    const body = await readFile(file);
    res.writeHead(200, { 'content-type': TYPES[path.slice(path.lastIndexOf('.'))] ?? 'application/octet-stream' }).end(body);
  } catch { res.writeHead(404).end(); }
});
server.kill = () => server.close();
await new Promise((r) => server.listen(PORT, '127.0.0.1', r));
const chromeArgs = ['--headless=new', `--remote-debugging-port=${DEBUG}`, `--user-data-dir=${PROFILE}`,
  '--autoplay-policy=no-user-gesture-required', 'about:blank'];
if (process.env.CI) chromeArgs.unshift('--no-sandbox');
const chrome = spawn(CHROME, chromeArgs, { stdio: 'ignore' });

async function cdp() {
  let target;
  // the about:blank tab (newer Chrome also lists internal pages such as the omnibox popup)
  for (let i = 0; i < 150 && !target; i++) {
    await sleep(200);
    target = await fetch(`http://127.0.0.1:${DEBUG}/json`).then((r) => r.json())
      .then((t) => t.find((x) => x.type === 'page' && x.url === 'about:blank')).catch(() => null);
  }
  if (!target) throw new Error(`Chrome's DevTools endpoint didn't come up on port ${DEBUG}`);
  const ws = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((r) => (ws.onopen = r));
  let id = 0; const pending = new Map(); const logs = [];
  ws.onmessage = (e) => {
    const m = JSON.parse(e.data);
    if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); }
    if (m.method === 'Runtime.consoleAPICalled') logs.push(m.params.args.map((a) => a.value ?? a.description).join(' '));
    if (m.method === 'Runtime.exceptionThrown') logs.push(`EXCEPTION ${m.params.exceptionDetails.exception?.description ?? m.params.exceptionDetails.text}`);
  };
  const send = (method, params = {}) => new Promise((r) => { const i = ++id; pending.set(i, r); ws.send(JSON.stringify({ id: i, method, params })); });
  await send('Runtime.enable'); await send('Page.enable'); await send('DOM.enable');
  const evaluate = async (expr) => (await send('Runtime.evaluate', { expression: expr, returnByValue: true, awaitPromise: true })).result.result?.value;
  const until = async (expr, ms) => {
    const t0 = Date.now();
    while (Date.now() - t0 < ms) { const v = await evaluate(expr); if (v) return v; await sleep(200); }
    throw new Error(`timeout waiting for ${expr}\n  ${logs.slice(-8).join('\n  ')}`);
  };
  const setFiles = async (selector, files) => {
    const doc = await send('DOM.getDocument');
    const node = await send('DOM.querySelector', { nodeId: doc.result.root.nodeId, selector });
    const r = await send('DOM.setFileInputFiles', { nodeId: node.result.nodeId, files });
    if (r.error) throw new Error(`setFileInputFiles: ${r.error.message}`);
  };
  const open = async (url) => {
    await evaluate('delete globalThis.__openoniChecked; delete globalThis.__openoniResult; 0');
    await send('Page.navigate', { url });
    await until('document.readyState === "complete" && !!globalThis.__openoniReady', 60000);
  };
  return { send, evaluate, until, setFiles, open, logs };
}

// The page's last hash-run frame, a PNG Blob from the worker's GL canvas (frames(..., { copy: true })).
async function saveFrame(page, file) {
  const url = await page.evaluate('globalThis.__openoniPng ? new Promise((r) => { const f = new FileReader(); f.onload = () => r(f.result); f.readAsDataURL(globalThis.__openoniPng); }) : null');
  if (url) writeFileSync(file, Buffer.from(url.split(',')[1], 'base64'));
}

async function result(page, ms = 600000) {
  const t0 = Date.now();
  for (;;) {
    const r = await page.evaluate('globalThis.__openoniResult');
    if (r) return { hash: r, secs: (Date.now() - t0) / 1000 };
    const err = await page.evaluate('document.getElementById("message").hidden ? "" : document.getElementById("message").textContent');
    if (/could not|stopped|failed/i.test(err)) throw new Error(`page: ${err}\n  ${page.logs.slice(-8).join('\n  ')}`);
    if (Date.now() - t0 > ms) throw new Error('timed out');
    await sleep(200);
  }
}

let failed = false;
const report = (ok, label, detail) => { failed ||= !ok; console.log(`${ok ? 'PASS' : 'FAIL'}  ${label}${detail ? `\n      ${detail}` : ''}`); };
const base = `http://127.0.0.1:${PORT}/play/`;
try {
  const page = await cdp();
  await page.send('Emulation.setDeviceMetricsOverride', { width: 1280, height: 1000, deviceScaleFactor: 1, mobile: false });
  await page.open(base);
  await sleep(2000); // let a fresh profile settle
  const shot = await page.send('Page.captureScreenshot', { format: 'png' });   // the page as Chrome renders it
  writeFileSync(join(OUT, 'page.png'), Buffer.from(shot.result.data, 'base64'));
  let imported = false;
  for (const c of CASES) {
    const nat = native(c, GAME);
    const ref = await nodeRef(c, GAME);
    if (!nat) console.log(`      no gasm-run at ${RUN}: the Node reference only`);
    else {
      console.log(`      gasm-run ${c.name}: ${nat}`);
      report(ref.after === nat, `${c.name}: gasm's Node host + gasm_exit = gasm-run`, ref.after === nat ? '' : ref.after);
    }
    if (ref.before !== ref.after) console.log(`      before gasm_exit (what the page sees): ${ref.before}`);
    const want = ref.before;
    const url = `${base}?hashframes=${c.frames}${c.input ? `&input=${encodeURIComponent(c.input)}` : ''}${Object.entries(c.params ?? {}).map(([k, v]) => `&${k}=${v}`).join('')}`;
    const check = (label, got) => report(!want || got.hash === want, `${c.name}: ${label}`,
      `${got.hash}  (${got.secs.toFixed(1)} s)${want && got.hash !== want ? `\n      native: ${want}` : ''}`);

    // 1. without importing: the installed folder via the File/Blob provider (the path that needs no OPFS)
    await page.open(url);
    await page.setFiles('#folder-input', [GAME]);
    const checked = await page.until('globalThis.__openoniChecked', 30000);
    report(checked.problems.length === 0, `${c.name}: game folder check`,
      `${checked.files} files, ${(checked.bytes / 1048576).toFixed(0)} MB ${checked.problems.join('; ')}`);
    await page.evaluate('document.getElementById("play-direct").click()');
    check('game folder without importing (File/Blob worker)', await result(page));
    await saveFrame(page, join(OUT, `${c.name}-direct.png`));

    // 2. without importing: the CD's image (the game reads the installer archive inside it)
    if (ISO) {
      await page.open(url);
      await page.setFiles('#image-input', [ISO]);
      const img = await page.until('globalThis.__openoniChecked', 30000);
      report(img.problems.length === 0, `${c.name}: disc image check`, `${(img.bytes / 1048576).toFixed(0)} MB ${img.problems.join('; ')}`);
      await page.evaluate('document.getElementById("play-direct").click()');
      check('CD image without importing (File/Blob worker)', await result(page));
    }

    // 3. first visit: import the folder into OPFS (once), play from OPFS
    if (!imported) {
      await page.open(url);
      if (await page.evaluate('!document.getElementById("imported").hidden')) await page.evaluate('document.getElementById("remove").click()');
      await page.until('document.getElementById("imported").hidden', 10000);
      await page.setFiles('#folder-input', [GAME]);
      await page.until('globalThis.__openoniChecked', 30000);
      const t0 = Date.now();
      await page.evaluate('document.getElementById("import").click()');
      const info = await page.until('globalThis.__openoniImported', 600000);
      console.log(`      imported ${info.files} files, ${(info.bytes / 1048576).toFixed(0)} MB into OPFS in ${((Date.now() - t0) / 1000).toFixed(1)} s`);
      check('import + OPFS worker', await result(page));
      await saveFrame(page, join(OUT, `${c.name}-opfs.png`));
      imported = true;
    }

    // 4. a later visit: the import is detected and played
    await page.open(url);
    report(await page.evaluate('!document.getElementById("imported").hidden'), `${c.name}: existing import detected`);
    await page.evaluate('document.getElementById("play-opfs").click()');
    check('OPFS worker (later visit)', await result(page));
  }

  // Real time: the game runs from the imported data and draws (the frames/s counter moves).
  await page.open(base);
  await page.evaluate('document.getElementById("play-opfs").click()');
  await page.until('/frames\\/s/.test(document.getElementById("status").textContent)', 20000);
  await sleep(2500);
  const png = await page.evaluate('globalThis.openoniPlay.framePng().then((b) => b ? b.size : 0)');
  const fps = await page.evaluate('document.getElementById("status").textContent');
  report(/^\d+ frames\/s$/.test(fps) && png > 0, 'real time from OPFS', `${fps}; frame PNG ${png} bytes`);
  const shot2 = await page.send('Page.captureScreenshot', { format: 'png' });
  writeFileSync(join(OUT, 'page-playing.png'), Buffer.from(shot2.result.data, 'base64'));
  await page.evaluate('document.getElementById("stop").click()');
  await sleep(500);
  const bad = page.logs.filter((l) => l.startsWith('EXCEPTION'));
  report(!bad.length, 'no uncaught exceptions', bad.slice(0, 3).join('\n      '));
  console.log(`screenshots in ${OUT}`);
} catch (e) {
  failed = true;
  console.error(`FAIL  ${e.message}`);
} finally {
  const exited = new Promise((r) => chrome.once('exit', r));
  chrome.kill(); server.kill();
  await Promise.race([exited, sleep(5000)]);
  try { rmSync(PROFILE, { recursive: true, force: true, maxRetries: 5, retryDelay: 200 }); } catch {}
}
process.exit(failed ? 1 : 0);
