// DSH Desktop — harness updater (runs on the EMBEDDED node.exe).
//
// Updates @deepseek-ai/dsh inside <exe>/runtime/dsh WITHOUT any Node.js or
// package manager installed on the customer machine. We use the npm that
// ships INSIDE the embedded Node.js distribution (node_modules/npm), so no
// separate pnpm bundle is needed.
//
// Usage:
//   node update.js check          print {"current","latest","update"} as JSON
//   node update.js apply [ver]    install <ver> (default latest) into a
//                                 staging dir and atomically swap it in
//
// Environment:
//   DSH_RUNTIME_DIR       <exe>\runtime           (required)
//   DSH_UPDATE_REGISTRY   npm registry url        (default registry.npmjs.org)

'use strict';
const fs = require('fs');
const path = require('path');
const https = require('https');
const http = require('http');
const { spawnSync } = require('child_process');

const RUNTIME = process.env.DSH_RUNTIME_DIR;
if (!RUNTIME) {
  console.error('update.js: DSH_RUNTIME_DIR not set');
  process.exit(2);
}

const REGISTRY = (process.env.DSH_UPDATE_REGISTRY || 'https://registry.npmjs.org').replace(/\/$/, '');
const DSH_DIR = path.join(RUNTIME, 'dsh');
const DSH_PKG_JSON = path.join(DSH_DIR, 'node_modules', '@deepseek-ai', 'dsh', 'package.json');
const PKG_NAME = '@deepseek-ai/dsh';

// npm-cli.js lives inside the embedded Node.js distribution.
// Layout (win-x64 distro): runtime/node/node.exe
//                           runtime/node/node_modules/npm/bin/npm-cli.js
// We resolve it relative to the running node binary so it works regardless
// of the exact runtime dir name.
const NODE_BIN = process.execPath;                                   // .../node/node.exe
const NODE_DIR = path.dirname(NODE_BIN);                             // .../node
const NPM_CLI = path.join(NODE_DIR, 'node_modules', 'npm', 'bin', 'npm-cli.js');

function currentVersion() {
  try {
    return JSON.parse(fs.readFileSync(DSH_PKG_JSON, 'utf8')).version;
  } catch {
    return null;
  }
}

function fetchJson(url) {
  return new Promise((resolve, reject) => {
    const mod = url.startsWith('https') ? https : http;
    const req = mod.get(url, { headers: { accept: 'application/json' }, timeout: 15000 }, (res) => {
      if (res.statusCode >= 300 && res.statusCode < 400 && res.headers.location) {
        res.resume();
        return fetchJson(res.headers.location).then(resolve, reject);
      }
      if (res.statusCode !== 200) {
        res.resume();
        return reject(new Error(`HTTP ${res.statusCode} for ${url}`));
      }
      let body = '';
      res.setEncoding('utf8');
      res.on('data', (c) => (body += c));
      res.on('end', () => {
        try {
          resolve(JSON.parse(body));
        } catch (e) {
          reject(e);
        }
      });
    });
    req.on('timeout', () => req.destroy(new Error('timeout')));
    req.on('error', reject);
  });
}

async function check() {
  const current = currentVersion();
  const meta = await fetchJson(`${REGISTRY}/${encodeURIComponent(PKG_NAME)}`);
  const latest = meta['dist-tags'] && meta['dist-tags'].latest;
  const out = { current, latest, update: !!current && !!latest && current !== latest };
  console.log(JSON.stringify(out));
  return out;
}

function rmrf(p) {
  fs.rmSync(p, { recursive: true, force: true });
}

function npmInstall(cwd, spec) {
  // Runs the embedded npm. spawnSync so the caller can inspect the exit code.
  return spawnSync(
    NODE_BIN,
    [
      NPM_CLI, 'install', spec,
      '--registry', REGISTRY,
      '--ignore-scripts',          // dsh ships pre-built; no install hooks needed
      '--no-audit', '--no-fund',
      '--loglevel', 'error',
    ],
    { cwd, encoding: 'utf8', timeout: 600000, stdio: ['ignore', 'inherit', 'inherit'] },
  );
}

async function apply(version) {
  if (!fs.existsSync(NPM_CLI)) {
    console.error('update.js: embedded npm not found at', NPM_CLI);
    process.exit(3);
  }
  const target = version || (await check().then((o) => o.latest));
  if (!target) {
    console.error('update.js: could not determine target version');
    process.exit(4);
  }
  if (target === currentVersion()) {
    console.log(JSON.stringify({ ok: true, skipped: true, version: target }));
    return;
  }

  // 1. Stage into a temp dir next to runtime/dsh so the final swap is a
  //    rename on the same volume.
  const staging = path.join(path.dirname(DSH_DIR), `dsh-staging-${Date.now()}`);
  fs.mkdirSync(staging, { recursive: true });
  fs.writeFileSync(
    path.join(staging, 'package.json'),
    JSON.stringify({ name: 'dsh-runtime', private: true, version: '0.0.0' }, null, 2),
  );

  console.error(`update.js: installing ${PKG_NAME}@${target} from ${REGISTRY} ...`);
  const r = npmInstall(staging, `${PKG_NAME}@${target}`);
  if (r.status !== 0) {
    rmrf(staging);
    console.error(`update.js: npm install failed (exit ${r.status})`);
    process.exit(5);
  }

  // 2. Atomic-ish swap: current -> .old, staging -> current, drop .old.
  const old = path.join(path.dirname(DSH_DIR), `dsh-old-${Date.now()}`);
  try {
    if (fs.existsSync(DSH_DIR)) fs.renameSync(DSH_DIR, old);
    fs.renameSync(staging, DSH_DIR);
    rmrf(old);
  } catch (e) {
    try {
      if (!fs.existsSync(DSH_DIR) && fs.existsSync(old)) fs.renameSync(old, DSH_DIR);
    } catch {}
    rmrf(staging);
    console.error('update.js: swap failed:', e.message);
    process.exit(6);
  }

  console.log(JSON.stringify({ ok: true, version: target }));
}

const [, , cmd, arg] = process.argv;
if (cmd === 'check') {
  check().catch((e) => {
    console.error('update.js: check failed:', e.message);
    process.exit(1);
  });
} else if (cmd === 'apply') {
  apply(arg).catch((e) => {
    console.error('update.js: apply failed:', e.message);
    process.exit(1);
  });
} else {
  console.error('usage: node update.js check | apply [version]');
  process.exit(2);
}
