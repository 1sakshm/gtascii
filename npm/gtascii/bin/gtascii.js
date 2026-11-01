#!/usr/bin/env node
'use strict';
// gtascii            -> the game (main menu)
// gtascii server ... -> the dedicated / room server
const { run } = require('../lib/launch');

const args = process.argv.slice(2);
if (args[0] === 'server') {
  run('gtascii-server', args.slice(1));
} else {
  run('gtascii', args);
}
