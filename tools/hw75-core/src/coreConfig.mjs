import { EventEmitter } from 'node:events';
import fs from 'node:fs/promises';

const DEFAULTS = {
  weather: {
    enabled: true,
    provider: 'open-meteo',
    lat: 31.2304,
    lon: 121.4737,
    city: 'Shanghai',
    units: 'celsius',
    refresh_minutes: 10,
  },
  clock: {
    enabled: true,
    refresh_minutes: 1,
  },
};

function deepMerge(base, patch) {
  if (!patch || typeof patch !== 'object') {
    return base;
  }
  const out = { ...base };
  for (const key of Object.keys(patch)) {
    const next = patch[key];
    if (next && typeof next === 'object' && !Array.isArray(next) && typeof out[key] === 'object') {
      out[key] = deepMerge(out[key], next);
    } else if (next !== undefined) {
      out[key] = next;
    }
  }
  return out;
}

export class CoreConfig extends EventEmitter {
  constructor(path) {
    super();
    this.path = path;
    this.config = structuredClone(DEFAULTS);
  }

  async load() {
    try {
      const raw = await fs.readFile(this.path, 'utf8');
      const parsed = JSON.parse(raw);
      this.config = deepMerge(DEFAULTS, parsed);
    } catch (err) {
      if (err.code !== 'ENOENT') {
        console.warn(`[core-config] load failed, using defaults: ${err.message}`);
      }
      await this.save();
    }
  }

  async save() {
    await fs.writeFile(this.path, JSON.stringify(this.config, null, 2), 'utf8');
  }

  snapshot() {
    return structuredClone(this.config);
  }

  async update(patch) {
    this.config = deepMerge(this.config, patch);
    await this.save();
    this.emit('change', this.snapshot());
  }
}
