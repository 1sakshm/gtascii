#!/usr/bin/env node
'use strict';
// Dedicated / online room server:  gtascii-server --rooms
require('../lib/launch').run('gtascii-server', process.argv.slice(2));
