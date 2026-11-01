#!/usr/bin/env node
// Assembles the npm packages for Grand Theft ASCII.
//
//   node scripts/build-npm.mjs platform <key> <dir-with-binaries>
//       -> dist/npm/gtascii-<key>/   (key: win32-x64, linux-x64, linux-arm64, darwin-x64, darwin-arm64)
//   node scripts/build-npm.mjs main [--platforms a,b,c]
//       -> dist/npm/gtascii/         (launcher + game data; depends on the platform packages)
//   node scripts/build-npm.mjs pack
//       -> runs `npm pack` in every assembled package (tarballs in dist/npm/)
//
// The version comes from the VERSION file at the repository root.
import { cpSync, existsSync, mkdirSync, readFileSync, rmSync, writeFileSync, readdirSync, chmodSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { execSync } from 'node:child_process';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const dist = join(root, 'dist', 'npm');
const version = readFileSync(join(root, 'VERSION'), 'utf8').trim();
const ALL = ['win32-x64', 'linux-x64', 'linux-arm64', 'darwin-x64', 'darwin-arm64'];

function fresh(dir) {
  rmSync(dir, { recursive: true, force: true });
  mkdirSync(dir, { recursive: true });
}

function writeJson(path, value) {
  writeFileSync(path, JSON.stringify(value, null, 2) + '\n');
}

function buildPlatform(key, binDir) {
  if (!ALL.includes(key)) throw new Error(`unknown platform ${key}; expected one of ${ALL.join(', ')}`);
  const [os, cpu] = key.split('-');
  const out = join(dist, `gtascii-${key}`);
  fresh(join(out, 'bin'));
  const ext = os === 'win32' ? '.exe' : '';
  for (const name of ['gtascii', 'gtascii-server']) {
    const src = join(binDir, name + ext);
    if (!existsSync(src)) throw new Error(`missing ${src}`);
    cpSync(src, join(out, 'bin', name + ext));
    if (!ext) chmodSync(join(out, 'bin', name), 0o755);
  }
  writeJson(join(out, 'package.json'), {
    name: `gtascii-${key}`,
    version,
    description: `Grand Theft ASCII native binaries for ${key}`,
    license: 'MIT',
    repository: { type: 'git', url: 'git+https://github.com/1sakshm/gtascii.git' },
    os: [os],
    cpu: [cpu],
    files: ['bin/'],
    preferUnplugged: true,
  });
  cpSync(join(root, 'LICENSE'), join(out, 'LICENSE'));
  writeFileSync(join(out, 'README.md'), `Native binaries of [gtascii](https://www.npmjs.com/package/gtascii) for ${key}. Install \`gtascii\` instead.\n`);
  console.log(`built ${out}`);
}

function buildMain(platforms) {
  const out = join(dist, 'gtascii');
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
  manifest.optionalDependencies = Object.fromEntries(platforms.map((p) => [`gtascii-${p}`, version]));
  writeJson(join(out, 'package.json'), manifest);
  console.log(`built ${out} (v${version}, platforms: ${platforms.join(', ')})`);
}

function packAll() {
  for (const entry of readdirSync(dist, { withFileTypes: true })) {
    if (!entry.isDirectory()) continue;
    execSync('npm pack --pack-destination ..', { cwd: join(dist, entry.name), stdio: 'inherit' });
  }
}

const [cmd, ...rest] = process.argv.slice(2);
if (cmd === 'platform') {
  buildPlatform(rest[0], resolve(rest[1]));
} else if (cmd === 'main') {
  const i = rest.indexOf('--platforms');
  buildMain(i >= 0 ? rest[i + 1].split(',') : ALL);
} else if (cmd === 'pack') {
  packAll();
} else {
  console.error('usage: build-npm.mjs platform <key> <bindir> | main [--platforms a,b] | pack');
  process.exit(2);
}
