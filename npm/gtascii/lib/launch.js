'use strict';
// Finds the prebuilt native binary for this machine and runs it with the
// game data that ships inside this package.

const { spawn } = require('child_process');
const fs = require('fs');
const path = require('path');

const SUPPORTED = ['win32-x64', 'linux-x64', 'linux-arm64', 'darwin-x64', 'darwin-arm64'];

function platformKey() {
  return `${process.platform}-${process.arch}`;
}

function binaryPath(name) {
  const exe = process.platform === 'win32' ? `${name}.exe` : name;
  // Escape hatch for development builds: GTASCII_BINARY_DIR=/path/to/build/bin
  if (process.env.GTASCII_BINARY_DIR) {
    return path.join(process.env.GTASCII_BINARY_DIR, exe);
  }
  const key = platformKey();
  if (!SUPPORTED.includes(key)) {
    fail(`Grand Theft ASCII has no prebuilt version for ${key} yet.
` +
         `Supported: ${SUPPORTED.join(', ')}. You can build it from source with CMake (see the project README).`);
  }
  return path.join(__dirname, '..', 'bin', key, exe);
}

function fail(message) {
  process.stderr.write(`gtascii: ${message}\n`);
  process.exit(1);
}

function run(name, args) {
  const bin = binaryPath(name);
  if (!fs.existsSync(bin)) {
    fail(`binary not found at ${bin}; try reinstalling: npm install -g gtascii`);
  }
  if (process.platform !== 'win32') {
    try { fs.chmodSync(bin, 0o755); } catch (_) { /* read-only install: fine if already executable */ }
  }
  const data = path.join(__dirname, '..', 'data');
  const child = spawn(bin, ['--data', data, ...args], { stdio: 'inherit', windowsHide: false });
  // The game owns the terminal; let it handle Ctrl+C itself.
  const ignore = () => {};
  process.on('SIGINT', ignore);
  process.on('SIGTERM', () => child.kill('SIGTERM'));
  child.on('error', (err) => fail(`could not start ${bin}: ${err.message}`));
  child.on('exit', (code, signal) => {
    process.removeListener('SIGINT', ignore);
    if (signal) {
      process.kill(process.pid, signal);
    } else {
      process.exit(code === null ? 1 : code);
    }
  });
}

module.exports = { run, binaryPath, platformKey, SUPPORTED };
