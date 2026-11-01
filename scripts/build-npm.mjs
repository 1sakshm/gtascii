#!/usr/bin/env node
// Assembles the npm package for Grand Theft ASCII.
//
// One package carries the launcher, the game data and the native binaries of
// every supported platform (bin/<key>/), so there is a single package to
// publish and approve. The launcher picks the right binary at run time.
//
//   node scripts/build-npm.mjs bin <key> <dir-with-binaries>
//       -> adds dist/npm/gtascii/bin/<key>/   (key: win32-x64, linux-x64, linux-arm64, darwin-x64, darwin-arm64)
//   node scripts/build-npm.mjs main
//       -> dist/npm/gtascii/ launcher + data (keeps binaries added with `bin`)
//   node scripts/build-npm.mjs pack
//       -> npm pack (tarball in dist/npm/)
//
// Run `main` first, then `bin` once per platform. The version comes from the
// VERSION file at the repository root.
import { cpSync, existsSync, mkdirSync, readFileSync, rmSync, writeFileSync, chmodSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { execSync } from 'node:child_process';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const dist = join(root, 'dist', 'npm');
const out = join(dist, 'gtascii');
const version = readFileSync(join(root, 'VERSION'), 'utf8').trim();
const ALL = ['win32-x64', 'linux-x64', 'linux-arm64', 'darwin-x64', 'darwin-arm64'];

function fresh(dir) {
  rmSync(dir, { recursive: true, force: true });
  mkdirSync(dir, { recursive: true });
}

function writeJson(path, value) {
  writeFileSync(path, JSON.stringify(value, null, 2) + '\n');
}

function addBinaries(key, binDir) {
  if (!ALL.includes(key)) throw new Error(`unknown platform ${key}; expected one of ${ALL.join(', ')}`);
  if (!existsSync(join(out, 'package.json'))) throw new Error('run `main` first');
  const target = join(out, 'bin', key);
  fresh(target);
  const ext = key.startsWith('win32') ? '.exe' : '';
  for (const name of ['gtascii', 'gtascii-server']) {
    const src = join(binDir, name + ext);
    if (!existsSync(src)) throw new Error(`missing ${src}`);
    cpSync(src, join(target, name + ext));
    if (!ext) chmodSync(join(target, name), 0o755);
  }
  console.log(`added ${key} binaries`);
}

function buildMain() {
  fresh(out);
  const src = join(root, 'npm', 'gtascii');
  cpSync(join(src, 'bin'), join(out, 'bin'), { recursive: true });
  cpSync(join(src, 'lib'), join(out, 'lib'), { recursive: true });
  cpSync(join(src, 'README.md'), join(out, 'README.md'));
  cpSync(join(root, 'LICENSE'), join(out, 'LICENSE'));
  // Game data: assets + default configs (the binaries get --data <this dir>).
  cpSync(join(root, 'assets'), join(out, 'data', 'assets'), { recursive: true, filter: (p) => !p.endsWith('.gitkeep') });
  cpSync(join(root, 'config'), join(out, 'data', 'config'), { recursive: true, filter: (p) => !p.endsWith('.gitkeep') });
  const manifest = JSON.parse(readFileSync(join(src, 'package.json'), 'utf8'));
  manifest.version = version;
  writeJson(join(out, 'package.json'), manifest);
  console.log(`built ${out} (v${version})`);
}

const [cmd, ...rest] = process.argv.slice(2);
if (cmd === 'bin') {
  addBinaries(rest[0], resolve(rest[1]));
} else if (cmd === 'main') {
  buildMain();
} else if (cmd === 'pack') {
  execSync('npm pack --pack-destination ..', { cwd: out, stdio: 'inherit' });
} else {
  console.error('usage: build-npm.mjs main | bin <key> <bindir> | pack');
  process.exit(1);
}
