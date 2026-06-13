import { resolve } from 'path';
import fs from 'fs-extra';

const KEYMAP_PATH = resolve('..', '..', 'config', 'hw75_keyboard.keymap');
const OUTPUT_PATH = resolve('./src/generated/keyboard-config.ts');
const HW75_GEOMETRY = {
  rowUnits: [
    [1.25, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1.25],
    [1.25, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1.5, 1.25],
    [1.25, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1.25, 1.25],
    [1.5, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1.75, 1.25],
    [1.75, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1.75, 1, 1.25],
    [1.25, 1.25, 1.25, 5.5, 1.25, 1.25, 1.25, 1, 1, 1],
    [1, 1, 1, 1, 1, 1],
  ],
  rowGapBefore: [
    [0, 0.5, 0, 0, 0, 0.5, 0, 0, 0, 0.5, 0, 0, 0, 0.75],
    [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.75],
    [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1.0],
    [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1.25],
    [0, 0.5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.5],
    [0, 0, 0, 0, 0, 0, 0, 0.5, 0, 0],
    [0, 0, 0, 0, 0, 0],
  ],
};

function formatBinding(raw) {
  const parts = raw.trim().split(/\s+/);
  const behavior = parts[0]?.replace(/^&/, '') ?? '';
  const argument = parts[1] ?? '';

  if (behavior === 'kp') {
    return argument;
  }

  if (behavior === 'mo') {
    return `MO ${argument}`;
  }

  if (behavior === 'rgb_ug') {
    return argument.replace(/^RGB_/, 'RGB ');
  }

  if (behavior === 'tb_mode') {
    return 'TB MODE';
  }

  if (behavior === 'trans') {
    return 'TRANS';
  }

  if (behavior === 'none') {
    return '';
  }

  return [behavior.toUpperCase(), argument].filter(Boolean).join(' ');
}

function parseLayerRows(block) {
  return block
    .split(/\r?\n/)
    .map((line) => line.trim())
    .filter(Boolean)
    .map((line) => {
      const bindings = line.match(/&[A-Za-z0-9_]+(?:\s+[A-Za-z0-9_]+)?/g) ?? [];
      return bindings.map((raw) => ({
        raw,
        label: formatBinding(raw),
      }));
    });
}

async function main() {
  const source = await fs.readFile(KEYMAP_PATH, 'utf8');
  const layers = [];

  const layerPattern = /(\w+)\s*\{\s*label\s*=\s*"([^"]+)";[\s\S]*?bindings\s*=\s*<([\s\S]*?)>;/g;
  let match;
  while ((match = layerPattern.exec(source)) !== null) {
    const [, id, label, bindingsBlock] = match;
    layers.push({
      id,
      label,
      rows: parseLayerRows(bindingsBlock),
    });
  }

  for (const layer of layers) {
    layer.rows.forEach((row, rowIndex) => {
      const expected = HW75_GEOMETRY.rowUnits[rowIndex]?.length;
      if (expected !== row.length) {
        throw new Error(`HW75 geometry mismatch on layer ${layer.id} row ${rowIndex}: expected ${expected}, got ${row.length}`);
      }
      const expectedGaps = HW75_GEOMETRY.rowGapBefore[rowIndex]?.length;
      if (expectedGaps !== row.length) {
        throw new Error(`HW75 gap geometry mismatch on layer ${layer.id} row ${rowIndex}: expected ${expectedGaps}, got ${row.length}`);
      }
    });
  }

  const output = `/* Auto-generated from config/hw75_keyboard.keymap. */\n` +
    `export const keyboardConfig = ${JSON.stringify({
      sourcePath: 'config/hw75_keyboard.keymap',
      sourceText: source,
      geometry: HW75_GEOMETRY,
      layers,
    }, null, 2)} as const;\n`;

  await fs.outputFile(OUTPUT_PATH, output);
}

await main();
