// The player's game data: picking it, checking it, and importing it into the site's origin private file
// system (OPFS). Nothing here is game data; everything comes from the user's own copy of the game and
// stays on their device.
//
// A source is { kind: 'folder' | 'image', name, entries: [[path, File], ...] }. A folder is the installed
// game, as the Windows installer leaves it or as openoni-extract makes it from the CD:
//   Oni.exe           the program (v1.0, 1,527,808 bytes): OpenOni reads its tables at run time
//                     (src/exe.c checks the size and the CRC-32 a8223704)
//   GameDataFolder/   the levels (levelN_Final.dat, .raw, .sep), the scripts (IGMD/), the movies
// or the CD's files, of which the game needs only the installer archive that holds all of the above:
//   ONISETUP.EXE, ONISETUP.002   (src/vfs_vise.c reads the installed files out of them)
// The game (openoni.wasm) reads them as assets named by their paths. An image is an .iso (ISO 9660) or raw
// .bin of the CD, one file that reaches the game as the asset "cd" (src/platform_gasm.c mounts it with
// vfs_mount_image, then the archive inside it); the game checks what is inside.
import { directoryHandleEntries, fileListEntries } from './vendor/gasm/gasm-host.js';
import { clearNamespace, opfsFileSystem, persist } from '@emdzej/csfs-opfs';

/** OPFS directory the data is imported into. */
export const NAMESPACE = 'openoni-data';
/** Written last: an import without it is incomplete. Hidden, so the game never sees it. */
const MARKER = '.openoni-import.json';
export const EXE = 'Oni.exe';
export const EXE_SIZE = 1527808;
const FOLDER = 'GAMEDATAFOLDER';
/** The CD's installer archive: the setup program and its second segment, the compressed files. */
const SETUP = 'ONISETUP.EXE', SETUP_DATA = 'ONISETUP.002', SETUP_DATA_SIZE = 348137970;

const upper = (s) => s.replace(/[a-z]/g, (c) => c.toUpperCase());
export const mb = (n) => (n >= 1 << 30 ? `${(n / 2 ** 30).toFixed(1)} GB` : `${(n / 1048576).toFixed(n < 10485760 ? 1 : 0)} MB`);

// ---- picking --------------------------------------------------------------------------

/** Folder from showDirectoryPicker() (Chromium). */
export async function folderFromHandle(handle) {
  return checkFolder(handle.name, await directoryHandleEntries(handle));
}

/** Folder from <input type="file" webkitdirectory> (all browsers). */
export function folderFromFileList(files) {
  const name = files[0]?.webkitRelativePath.split('/')[0] || 'folder';
  return checkFolder(name, fileListEntries(files));
}

/** The game's root inside the picked folder: the folder itself or one below it, wherever Oni.exe,
    GameDataFolder/ or the CD's ONISETUP.EXE is. Returns '' or 'sub/'. */
function findRoot(entries) {
  const roots = new Set();
  for (const [p] of entries) {
    const segs = upper(p).split('/');
    for (let d = 0; d <= 1 && d < segs.length; d++) {
      if ((d === segs.length - 1 && (segs[d] === upper(EXE) || segs[d] === SETUP)) || (d < segs.length - 1 && segs[d] === FOLDER)) {
        roots.add(p.split('/').slice(0, d).map((s) => `${s}/`).join(''));
      }
    }
  }
  return [...roots].sort((a, b) => a.length - b.length)[0] ?? null;
}

/** Keep what the game reads and check it. Returns { source, problems, files, bytes }. */
export function checkFolder(name, entries) {
  const problems = [];
  const kept = [];
  const root = findRoot(entries);
  const cd = root !== null && entries.some(([p]) => upper(p) === `${upper(root)}${SETUP}`) &&
    !entries.some(([p]) => upper(p) === `${upper(root)}${upper(EXE)}`);
  if (root === null) {
    problems.push('no Oni.exe, no GameDataFolder and no ONISETUP.EXE in this folder');
  } else if (cd) {
    for (const [path, file] of entries) {
      const up = upper(path.slice(root.length));
      if (path.startsWith(root) && (up === SETUP || up === SETUP_DATA)) kept.push([path.slice(root.length), file]);
    }
    const seg = kept.find(([p]) => upper(p) === SETUP_DATA);
    if (!seg) problems.push(`no ${SETUP_DATA} next to ${SETUP} (the installer's compressed files)`);
    else if (seg[1].size !== SETUP_DATA_SIZE) problems.push(`${SETUP_DATA} is ${seg[1].size.toLocaleString('en')} bytes, not ${SETUP_DATA_SIZE.toLocaleString('en')}: OpenOni needs the CD of the Windows release v1.0`);
  } else {
    for (const [path, file] of entries) {
      if (!path.startsWith(root)) continue;
      const rel = path.slice(root.length), up = upper(rel);
      if ((up.startsWith(`${FOLDER}/`) && !up.endsWith('/')) || up === upper(EXE)) kept.push([rel, file]);
    }
    const exe = kept.find(([p]) => upper(p) === upper(EXE));
    const has = (n) => kept.some(([p]) => upper(p) === `${FOLDER}/${upper(n)}`);
    if (!has('level0_Final.dat') || !has('level1_Final.dat')) problems.push('GameDataFolder has no level0_Final.dat and level1_Final.dat');
    if (!exe) problems.push(`no ${EXE} (OpenOni reads the original program's tables)`);
    else if (exe[1].size !== EXE_SIZE) problems.push(`${EXE} is ${exe[1].size.toLocaleString('en')} bytes, not ${EXE_SIZE.toLocaleString('en')}: OpenOni needs the Windows release v1.0`);
  }
  const bytes = kept.reduce((n, [, f]) => n + f.size, 0);
  const shown = root ? `${name}/${root.slice(0, -1)}` : name;
  return { source: { kind: 'folder', name: shown, entries: kept, cd }, problems, files: kept.length, bytes };
}

/** Where the ISO 9660 volume descriptor ("CD001") is: 2048-byte sectors (.iso) or raw 2352-byte ones (.bin). */
const ISO_MAGIC = [16 * 2048 + 1, 16 * 2352 + 16 + 1, 16 * 2352 + 24 + 1];

/**
 * A picked file as a disc image: an .iso or raw .bin with an ISO 9660 volume. Returns { source, problems,
 * files, bytes } like checkFolder (the one entry is the asset "cd").
 */
export async function imageFromFile(file) {
  const problems = [];
  const ext = (/\.[^.]*$/.exec(file.name)?.[0] ?? '').toLowerCase();
  if (ext === '.cue') problems.push('a .cue sheet only names the image: choose the .bin file next to it');
  else if (ext !== '.iso' && ext !== '.bin') problems.push('not an .iso or .bin disc image');
  else {
    let found = false;
    for (const off of ISO_MAGIC) {
      if (file.size < off + 5) continue;
      const b = new Uint8Array(await file.slice(off, off + 5).arrayBuffer());
      if (String.fromCharCode(...b) === 'CD001') { found = true; break; }
    }
    if (!found) problems.push('no ISO 9660 file system in this image');
  }
  return { source: { kind: 'image', name: file.name, entries: [['cd', file]] }, problems, files: 1, bytes: file.size };
}

// ---- OPFS import ----------------------------------------------------------------------

/** The completed import's description ({ kind, name, files, bytes, date }), or null. */
export async function existingImport() {
  try {
    const root = await navigator.storage.getDirectory();
    const dir = await root.getDirectoryHandle(NAMESPACE);
    const f = await (await dir.getFileHandle(MARKER)).getFile();
    return JSON.parse(await f.text());
  } catch {
    return null;
  }
}

export async function removeImport() {
  await clearNamespace(NAMESPACE);
  try { await (await navigator.storage.getDirectory()).removeEntry(NAMESPACE, { recursive: true }); } catch {}
}

/** Ask the browser to keep the data under storage pressure. Firefox may never answer a prompt. */
export async function requestPersist() {
  return persist({ signal: AbortSignal.timeout(10000) }).catch(() => false);
}

export async function storageInfo() {
  const est = await navigator.storage?.estimate?.().catch(() => null);
  const persisted = await navigator.storage?.persisted?.().catch(() => false);
  return { usage: est?.usage, quota: est?.quota, persisted: !!persisted };
}

/** Copy a checked source into OPFS (each file through a stream, so a large file never sits in memory).
    The marker goes last. onProgress({ bytes, total, file, files, done }). */
export async function importSource(source, onProgress = () => {}) {
  const items = source.entries;
  const total = items.reduce((n, [, f]) => n + f.size, 0);
  await persist({ signal: AbortSignal.timeout(3000) }).catch(() => false);
  await removeImport();
  const fs = await opfsFileSystem({ namespace: NAMESPACE });
  let bytes = 0, last = 0;
  for (let i = 0; i < items.length; i++) {
    const [path, file] = items[i];
    const counted = file.stream().pipeThrough(new TransformStream({
      transform(chunk, ctrl) {
        bytes += chunk.byteLength;
        const now = performance.now();
        if (now - last > 100) { last = now; onProgress({ bytes, total, file: path, files: items.length, done: i }); }
        ctrl.enqueue(chunk);
      },
    }));
    await fs.write(`/${path}`, counted);
    onProgress({ bytes, total, file: path, files: items.length, done: i + 1 });
  }
  const info = { kind: source.kind, name: source.name, files: items.length, bytes: total, date: new Date().toISOString() };
  await fs.write(`/${MARKER}`, new TextEncoder().encode(JSON.stringify(info)));
  return info;
}

// ---- running --------------------------------------------------------------------------

/** gasm Worker-mode asset specs for a source ('opfs' = the imported data). */
export function assetSpecs(source) {
  if (source === 'opfs') return [{ kind: 'opfs', dir: NAMESPACE }];
  return [{ kind: 'files', entries: source.entries }];
}
