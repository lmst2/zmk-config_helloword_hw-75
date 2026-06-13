/*
 * Loads usb_comm.proto at runtime via protobufjs. The proto file at
 * config/proto/usb_comm.proto is the single source of truth; we preprocess it
 * to strip nanopb-specific C generator options (which protobufjs cannot parse)
 * before handing the text to protobuf.parse.
 */
import { fileURLToPath } from 'node:url';
import { dirname, resolve } from 'node:path';
import fs from 'node:fs/promises';
import protobuf from 'protobufjs';

const here = dirname(fileURLToPath(import.meta.url));
const protoPath = resolve(here, '../../..', 'config/proto/usb_comm.proto');

function stripNanopbAnnotations(text) {
  return text
    .replace(/\s*import\s+"nanopb\.proto"\s*;\s*/g, '')
    .replace(/\[\s*\(nanopb[^\]]*\)[^\]]*\]/g, '')
    .replace(/\s*option\s*\(nanopb_msgopt\)[^;]*;\s*/g, '');
}

const raw = await fs.readFile(protoPath, 'utf8');
const cleaned = stripNanopbAnnotations(raw);

const parsed = protobuf.parse(cleaned, { keepCase: false });
const root = parsed.root;
root.resolveAll();

const comm = root.lookup('usb.comm');
if (!comm) {
  throw new Error(`Failed to locate usb.comm namespace in ${protoPath}`);
}

function flattenEnum(enumNode) {
  const out = {};
  for (const [name, value] of Object.entries(enumNode.values)) {
    out[name] = value;
    out[value] = name;
  }
  return out;
}

function exportNamespace(ns) {
  const out = {};
  for (const child of ns.nestedArray) {
    if (child instanceof protobuf.Enum) {
      out[child.name] = flattenEnum(child);
    } else if (child instanceof protobuf.Type) {
      out[child.name] = child;
      const nestedExport = exportNamespace(child);
      for (const [key, value] of Object.entries(nestedExport)) {
        child[key] = value;
      }
    } else if (child instanceof protobuf.Namespace) {
      out[child.name] = exportNamespace(child);
    }
  }
  return out;
}

export const UsbComm = exportNamespace(comm);
