import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

/*
 * Inlines the canonical usb_comm.proto into a JS module so the 中枢 can parse it
 * with no filesystem lookup — required once it's bundled into a single
 * executable (where import.meta.url / relative paths don't resolve). Run in
 * prestart for dev and before bundling for packaging.
 */

const here = path.dirname(fileURLToPath(import.meta.url));
const src = path.resolve(here, '../../../config/proto/usb_comm.proto');
const out = path.resolve(here, '../src/usb_comm.proto.mjs');

const text = fs.readFileSync(src, 'utf8');
fs.writeFileSync(
  out,
  `// AUTO-GENERATED from config/proto/usb_comm.proto — do not edit.\n` +
  `export default ${JSON.stringify(text)};\n`,
);
console.log(`[gen-proto] wrote ${path.relative(process.cwd(), out)} (${text.length} bytes)`);
