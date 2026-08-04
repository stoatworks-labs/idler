#!/usr/bin/env node
/**
 * Check the ported savers against the plugin's own `idtest --geometry`.
 *
 * ## Why this exists
 *
 * Idler's demo is not like the rest of the fleet's. On tinsel or orrery the
 * plugin *is* a fragment shader, so copying the shader across and checking it
 * character for character (`check_shaders.py`) means the demo runs the plugin's
 * actual work. Idler's shaders are 242 lines and almost nothing lives there —
 * the substance is a 3D engine and eleven savers on the CPU, and those had to be
 * translated by hand into `plugin.js`.
 *
 * A hand port is a second implementation, and a second implementation with
 * nothing checking it is how the demo comes to draw a *plausible* wrong picture.
 * That is the exact failure mode the rest of this repo's harness exists to
 * prevent, and it would have been the one thing about this page that nothing
 * covered.
 *
 * So: `idtest --geometry` builds every saver on its own preset at a pinned time
 * and prints the triangle count, the vertex count and the bounding box. This
 * runs the ported savers under the same conditions and compares the same three
 * numbers.
 *
 * ## What it does and does not prove
 *
 * It proves the ported saver emits the same amount of geometry in the same place
 * as the C++ one. That catches a mis-ported loop bound, an off-by-one in a
 * builder, a wrong conversion curve feeding a count, a hash that has lost its
 * low bits to a double — the errors that actually happen in a port like this,
 * and that look fine on screen.
 *
 * It does NOT prove the vertices are individually identical, or that the colours
 * or normals match. A bounding box is a coarse instrument. It is a great deal
 * better than the nothing that was here before, and it is deliberately not
 * described on the page as more than it is.
 *
 *   node demo/tools/check_geometry.mjs                 compare against idtest
 *   node demo/tools/check_geometry.mjs --print         just print this side
 *
 * Exits non-zero on any mismatch, so tools/verify.sh can gate on it.
 */

import { execFileSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, resolve } from 'node:path';

import {
  SAVER_NAMES, PORTED, SAVER_FACTORIES, PRESET_TABLE,
  buildParams, settingsFromParams, Scene,
} from '../plugin.js';

const here = dirname(fileURLToPath(import.meta.url));
const repoRoot = resolve(here, '..', '..');

// The conditions idtest's runGeometry() uses: each saver on its own preset
// (dropdown element N+1, which is kPresets[N]), the clock pinned to 11.5s, at
// the harness's default render size.
const kTime = 11.5;
const kWidth = 960;
const kHeight = 540;

/** A stand-in for the kit's Params, holding defaults overridden by a preset. */
function paramsFor(preset) {
  const values = {};
  for (const p of buildParams('source')) {
    values[p.id] = p.type === 'text' ? (p.default ?? '') : (p.default ?? 0);
  }
  for (const [key, value] of Object.entries(preset)) {
    if (key === 'name') continue;
    values[key] = value;
  }
  return { get: (id) => values[id] };
}

function measure(saverKind, preset) {
  const params = paramsFor(preset);
  const settings = settingsFromParams(params, kWidth, kHeight, kTime, 'Idler', 0);
  settings.saver = saverKind;

  const scene = new Scene();
  scene.background = settings.background;
  SAVER_FACTORIES[saverKind]().build(settings, scene);

  const mesh = scene.mesh;
  const low = { x: Infinity, y: Infinity, z: Infinity };
  const high = { x: -Infinity, y: -Infinity, z: -Infinity };

  let badNormals = 0;
  let nonFinite = 0;

  for (let i = 0; i < mesh.px.length; i += 1) {
    // Every normal unit length. A zero-length normal becomes a NaN once
    // normalised, and one NaN normal takes the lighting of the whole draw call
    // with it — so the symptom is a saver that goes black with nothing to say
    // why. Same assertion the C++ makes.
    const len = Math.hypot(mesh.nx[i], mesh.ny[i], mesh.nz[i]);
    if (!(Math.abs(len - 1) < 0.02)) badNormals += 1;

    if (!Number.isFinite(mesh.px[i]) || !Number.isFinite(mesh.py[i]) || !Number.isFinite(mesh.pz[i])) {
      nonFinite += 1;
      continue;
    }

    low.x = Math.min(low.x, mesh.px[i]); high.x = Math.max(high.x, mesh.px[i]);
    low.y = Math.min(low.y, mesh.py[i]); high.y = Math.max(high.y, mesh.py[i]);
    low.z = Math.min(low.z, mesh.pz[i]); high.z = Math.max(high.z, mesh.pz[i]);
  }

  let indicesValid = true;
  for (const index of mesh.indices) {
    if (index >= mesh.px.length) { indicesValid = false; break; }
  }

  return {
    tris: mesh.triangleCount,
    verts: mesh.px.length,
    low, high, badNormals, nonFinite, indicesValid,
  };
}

const fmt = (n) => n.toFixed(2);
const bbox = (m) => `[${fmt(m.low.x)} ${fmt(m.low.y)} ${fmt(m.low.z)}]..[${fmt(m.high.x)} ${fmt(m.high.y)} ${fmt(m.high.z)}]`;

/** Parse a line of `idtest --geometry` output. */
function parseReference(line) {
  const match = /^(.{1,22}?)\s+tris\s+(\d+)\s+verts\s+(\d+)\s+bbox \[(-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\]\.\.\[(-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\]/.exec(line);
  if (!match) return null;
  const n = (i) => Number(match[i]);
  return {
    name: match[1].trim(),
    tris: n(2),
    verts: n(3),
    low: { x: n(4), y: n(5), z: n(6) },
    high: { x: n(7), y: n(8), z: n(9) },
  };
}

const printOnly = process.argv.includes('--print');

const mine = new Map();
for (const kind of PORTED) {
  const preset = PRESET_TABLE.find((p) => p.saver === kind);
  if (!preset) {
    console.error(`no preset for ${SAVER_NAMES[kind]} — idtest --geometry drives each saver from its own preset`);
    process.exit(2);
  }
  mine.set(SAVER_NAMES[kind], measure(kind, preset));
}

if (printOnly) {
  for (const [name, m] of mine) {
    console.log(`${name.padEnd(22)} tris ${String(m.tris).padStart(7)}  verts ${String(m.verts).padStart(7)}  bbox ${bbox(m)}`);
  }
  process.exit(0);
}

const idtest = resolve(repoRoot, 'build', 'idtest');
if (!existsSync(idtest)) {
  console.log('check_geometry: build/idtest is not built — skipping.');
  console.log('  cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build');
  process.exit(0);
}

const reference = new Map();
for (const line of execFileSync(idtest, ['--geometry'], { encoding: 'utf8' }).split('\n')) {
  const parsed = parseReference(line);
  if (parsed) reference.set(parsed.name, parsed);
}

let failures = 0;
let checked = 0;

for (const [name, m] of mine) {
  const ref = reference.get(name);
  if (!ref) {
    console.log(`MISSING  ${name} — not in idtest --geometry output`);
    failures += 1;
    continue;
  }

  const problems = [];
  if (m.tris !== ref.tris) problems.push(`tris ${m.tris} != ${ref.tris}`);
  if (m.verts !== ref.verts) problems.push(`verts ${m.verts} != ${ref.verts}`);

  // The bounding box is compared to the two decimals idtest prints. A port that
  // agrees to 0.005 in every axis is not accidentally agreeing.
  for (const axis of ['x', 'y', 'z']) {
    if (fmt(m.low[axis]) !== fmt(ref.low[axis]) || fmt(m.high[axis]) !== fmt(ref.high[axis])) {
      problems.push(`${axis} ${fmt(m.low[axis])}..${fmt(m.high[axis])} != ${fmt(ref.low[axis])}..${fmt(ref.high[axis])}`);
    }
  }

  if (!m.indicesValid) problems.push('index out of range');
  if (m.badNormals) problems.push(`${m.badNormals} normals not unit length`);
  if (m.nonFinite) problems.push(`${m.nonFinite} non-finite positions`);

  checked += 1;
  if (problems.length) {
    failures += 1;
    console.log(`DIFFERS  ${name}`);
    for (const problem of problems) console.log(`           ${problem}`);
  } else {
    console.log(`ok       ${name.padEnd(22)} tris ${String(m.tris).padStart(7)}  verts ${String(m.verts).padStart(7)}`);
  }
}

const ported = PORTED.length;
const total = SAVER_NAMES.length;
console.log(`\n${checked - failures}/${checked} checked savers match idtest (${ported}/${total} savers ported).`);

process.exit(failures === 0 ? 0 : 1);
