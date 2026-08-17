// DSH Desktop — first-run provisioner (runs on the EMBEDDED node.exe).
//
// Job: make sure $DSH_HOME/profiles/web exists (delegating template creation
// to dsh itself) and install the shipped TS plugin bundles into it.
// Idempotent; cheap after the first run.
//
// Environment (set by the Qt shell):
//   DSH_HOME          e.g. C:\Users\<u>\AppData\Roaming\DSH\DSHDesktop
//   DSH_RUNTIME_DIR   <exe>\runtime
//
// Layout it manages:
//   $DSH_HOME/profiles/web/...                (dsh's own template)
//   $DSH_HOME/profiles/web/node_modules/<pkg> (copies of shipped plugins)

'use strict';
const fs = require('fs');
const path = require('path');
const { spawnSync } = require('child_process');

const HOME = process.env.DSH_HOME;
const RUNTIME = process.env.DSH_RUNTIME_DIR;
if (!HOME || !RUNTIME) {
  console.error('provision.js: DSH_HOME / DSH_RUNTIME_DIR not set');
  process.exit(2);
}

const DSH_BIN = path.join(RUNTIME, 'dsh', 'node_modules', '@deepseek-ai', 'dsh', 'lib', 'bin.js');
const PROFILE_DIR = path.join(HOME, 'profiles', 'web');
const TSPLUGINS_SRC = path.join(path.dirname(RUNTIME), 'tsplugins');

function ensureDir(p) {
  fs.mkdirSync(p, { recursive: true });
}

function copyDir(src, dst) {
  ensureDir(dst);
  for (const entry of fs.readdirSync(src, { withFileTypes: true })) {
    if (entry.name === 'node_modules' || entry.name === '.git') continue;
    const s = path.join(src, entry.name);
    const d = path.join(dst, entry.name);
    if (entry.isDirectory()) copyDir(s, d);
    else if (entry.isFile()) fs.copyFileSync(s, d);
  }
}

// --- 1. let dsh create its own web profile template -------------------------
// `--dump-default-config` boots nothing but DOES auto-initialize the profile
// directory from the shipped template (and stays correct across dsh updates).
ensureDir(HOME);
if (!fs.existsSync(path.join(PROFILE_DIR, 'package.json'))) {
  const r = spawnSync(process.execPath, [DSH_BIN, '--profile', 'web', '--dump-default-config'], {
    env: { ...process.env, DSH_HOME: HOME },
    encoding: 'utf8',
    timeout: 60000,
    stdio: ['ignore', 'ignore', 'pipe'],
  });
  if (r.status !== 0 || !fs.existsSync(path.join(PROFILE_DIR, 'package.json'))) {
    console.error('provision.js: web profile auto-init failed:', (r.stderr || '').slice(0, 500));
    process.exit(3);
  }
  console.log('provision.js: web profile template created by dsh');
}

// --- 2. install shipped TS plugin bundles -----------------------------------
// Every directory under <exe>/tsplugins whose package.json declares
// dsh.bundle.patch is a plugin bundle: copy it into the profile's
// node_modules and register it in dsh.profile.bundles.
const pkgPath = path.join(PROFILE_DIR, 'package.json');
const pkg = JSON.parse(fs.readFileSync(pkgPath, 'utf8'));
pkg.dsh = pkg.dsh || {};
pkg.dsh.profile = pkg.dsh.profile || {};
const bundles = (pkg.dsh.profile.bundles = pkg.dsh.profile.bundles || []);
pkg.dependencies = pkg.dependencies || {};

let installed = 0;
if (fs.existsSync(TSPLUGINS_SRC)) {
  for (const name of fs.readdirSync(TSPLUGINS_SRC)) {
    const srcPkgDir = path.join(TSPLUGINS_SRC, name);
    const manifestPath = path.join(srcPkgDir, 'package.json');
    if (!fs.existsSync(manifestPath)) continue;

    let manifest;
    try {
      manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
    } catch (e) {
      console.error(`provision.js: skip ${name}: bad package.json (${e.message})`);
      continue;
    }
    if (!manifest.dsh || !manifest.dsh.bundle) {
      console.error(`provision.js: skip ${name}: no dsh.bundle declaration`);
      continue;
    }

    // Copy into profile node_modules (replace wholesale each launch so an
    // app update ships new plugin versions automatically). The bundle's own
    // dependencies resolve through the profile's hoisted node_modules, which
    // reaches dsh's maintained fallback at $DSH_HOME/profiles/node_modules
    // and then the dsh installation itself — no pnpm install needed.
    const dstPkgDir = path.join(PROFILE_DIR, 'node_modules', manifest.name);
    fs.rmSync(dstPkgDir, { recursive: true, force: true });
    ensureDir(path.dirname(dstPkgDir));
    copyDir(srcPkgDir, dstPkgDir);

    if (!bundles.includes(manifest.name)) {
      bundles.push(manifest.name);
      pkg.dependencies[manifest.name] = `file:./node_modules/${manifest.name}`;
      console.log(`provision.js: installed TS plugin bundle ${manifest.name}@${manifest.version}`);
      installed++;
    }
  }
}

fs.writeFileSync(pkgPath, JSON.stringify(pkg, null, 2) + '\n');
console.log(
  `provision.js: web profile ready at ${PROFILE_DIR} (${bundles.length} bundles, ${installed} newly installed)`
);
