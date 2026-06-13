/*
 * Loads usb_comm.proto via protobufjs. The proto text is inlined at build time
 * (scripts/gen-proto.mjs -> usb_comm.proto.mjs) so this works both in dev and
 * inside a bundled single executable, where filesystem/relative paths don't
 * resolve. config/proto/usb_comm.proto stays the single source of truth; we
 * strip nanopb-specific C generator options (which protobufjs can't parse)
 * before handing the text to protobuf.parse.
 */
import protobuf from 'protobufjs';
import PROTO_TEXT from './usb_comm.proto.mjs';

function stripNanopbAnnotations(text) {
  return text
    .replace(/\s*import\s+"nanopb\.proto"\s*;\s*/g, '')
    .replace(/\[\s*\(nanopb[^\]]*\)[^\]]*\]/g, '')
    .replace(/\s*option\s*\(nanopb_msgopt\)[^;]*;\s*/g, '');
}

const cleaned = stripNanopbAnnotations(PROTO_TEXT);

const parsed = protobuf.parse(cleaned, { keepCase: false });
const root = parsed.root;
root.resolveAll();

const comm = root.lookup('usb.comm');
if (!comm) {
  throw new Error('Failed to locate usb.comm namespace in usb_comm.proto');
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
