import { createRequire } from 'module';
import { promisify } from 'util';
import { join, resolve } from 'path';
import fs from 'fs-extra';

const require = createRequire(import.meta.url);
const _pbjs = require('protobufjs-cli/pbjs.js');
const _pbts = require('protobufjs-cli/pbts.js');

const pbjs = promisify(_pbjs.main);
const pbts = promisify(_pbts.main);

const DST_DIR = resolve('./src/proto');
const PROTO_SOURCES = [
  {
    src: resolve('../..', 'config/proto/usb_comm.proto'),
    out: 'comm.proto'
  }
];

for (const { src, out } of PROTO_SOURCES) {
  let js = await pbjs([
    '--target', 'static-module',
    '--wrap', 'es6',
    src
  ]);

  if (out === 'comm.proto') {
    js += '\nexport const UsbComm = usb.comm;\n';
  }

  await fs.outputFile(join(DST_DIR, `${out}.js`), js);

  let ts = await pbts([
    join(DST_DIR, `${out}.js`)
  ]);

  if (out === 'comm.proto') {
    ts += '\nexport import UsbComm = usb.comm;\n';
  }

  await fs.outputFile(join(DST_DIR, `${out}.d.ts`), ts);
}
