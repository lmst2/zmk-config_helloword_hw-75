import { createCanvas } from '@napi-rs/canvas';

import { UsbComm } from './protoLoader.mjs';

/*
 * E-ink now-playing card for the HW-75 dynamic module.
 *
 * The panel is a portrait 128x296, 1bpp framebuffer in the SAME format the
 * firmware renderer (eink_render.c) and the web (utils/graphic.ts::toBits) use:
 * row-major, pitch = width/8 = 16 bytes/row, MSB-first within each byte, bit
 * value 1 = white. We clear to white and draw black text, so the result is
 * black text on a white background — no inversion.
 *
 * renderNowPlaying() rasterizes the card with @napi-rs/canvas (cross-platform,
 * prebuilt for win/linux/mac x64+arm64) and returns the exact 4736-byte frame.
 * Antialiasing is off so the output is pure black/white and the 1bpp packing is
 * unambiguous. EinkCard owns every write to the panel so the live media overlay
 * and the engine's per-app base mode never fight over it.
 */

const EINK_WIDTH = 128;
const EINK_HEIGHT = 296;
const EINK_FRAME_BYTES = (EINK_WIDTH / 8) * EINK_HEIGHT; // 4736

// CJK uses whatever system font the host has (YaHei / PingFang / Noto), falling
// back to sans-serif. Covers Chinese, Japanese kana, and Latin.
const FONT_STACK =
  '"Microsoft YaHei","PingFang SC","Hiragino Sans GB","Noto Sans CJK SC",' +
  '"Microsoft JhengHei","Source Han Sans SC",sans-serif';

/* Greedy char-wrap into at most maxLines, ellipsizing the last on overflow. */
function wrapText(ctx, text, maxWidth, maxLines) {
  const chars = [...String(text || '')];
  const lines = [];
  let line = '';
  for (const ch of chars) {
    const test = line + ch;
    if (ctx.measureText(test).width > maxWidth && line) {
      lines.push(line);
      line = ch;
      if (lines.length === maxLines) {
        break;
      }
    } else {
      line = test;
    }
  }
  if (lines.length < maxLines && line) {
    lines.push(line);
  }
  if (lines.join('').length < chars.length && lines.length) {
    let last = lines[lines.length - 1];
    while (last && ctx.measureText(last + '…').width > maxWidth) {
      last = last.slice(0, -1);
    }
    lines[lines.length - 1] = last + '…';
  }
  return lines;
}

export function renderNowPlaying(spec) {
  const canvas = createCanvas(EINK_WIDTH, EINK_HEIGHT);
  const ctx = canvas.getContext('2d');
  ctx.antialias = 'none';
  ctx.textBaseline = 'middle';

  ctx.fillStyle = '#ffffff';
  ctx.fillRect(0, 0, EINK_WIDTH, EINK_HEIGHT);
  ctx.fillStyle = '#000000';

  // Header (source app) + divider, mirroring eink_render.c's divider style.
  ctx.textAlign = 'center';
  ctx.font = `bold 13px ${FONT_STACK}`;
  ctx.fillText(String(spec.app || ''), EINK_WIDTH / 2, 16);
  ctx.fillRect(8, 29, EINK_WIDTH - 16, 3);

  // Play/pause glyph (from primitives) + status, centered as a group.
  ctx.font = `12px ${FONT_STACK}`;
  const statusText = spec.playing ? '正在播放' : '已暂停';
  const sw = ctx.measureText(statusText).width;
  const tx = Math.round((EINK_WIDTH - sw) / 2) + 9;
  ctx.textAlign = 'left';
  ctx.fillText(statusText, tx, 49);
  const gx = tx - 19;
  if (spec.playing) {
    ctx.beginPath();
    ctx.moveTo(gx, 43);
    ctx.lineTo(gx, 55);
    ctx.lineTo(gx + 12, 49);
    ctx.closePath();
    ctx.fill();
  } else {
    ctx.fillRect(gx, 43, 4, 12);
    ctx.fillRect(gx + 7, 43, 4, 12);
  }

  // Title (wrapped, up to 3 lines, vertically centered around y=120).
  ctx.textAlign = 'center';
  ctx.font = `bold 17px ${FONT_STACK}`;
  const titleLines = wrapText(ctx, spec.title, EINK_WIDTH - 12, 3);
  const titleStartY = 120 - ((titleLines.length - 1) * 24) / 2;
  titleLines.forEach((ln, i) => ctx.fillText(ln, EINK_WIDTH / 2, titleStartY + i * 24));

  // Artist (up to 2 lines).
  ctx.font = `12px ${FONT_STACK}`;
  const artistLines = wrapText(ctx, spec.artist, EINK_WIDTH - 12, 2);
  artistLines.forEach((ln, i) => ctx.fillText(ln, EINK_WIDTH / 2, 196 + i * 16));

  // Footer divider + wall-clock time.
  ctx.fillRect(8, 250, EINK_WIDTH - 16, 3);
  ctx.font = `13px ${FONT_STACK}`;
  ctx.fillText(String(spec.time || ''), EINK_WIDTH / 2, 266);

  // Pack to 1bpp (row-major, MSB-first, bit 1 = white) — matches eink_render.c
  // and the web's toBits. Red channel suffices since the image is pure b/w.
  const data = ctx.getImageData(0, 0, EINK_WIDTH, EINK_HEIGHT).data;
  const rowBytes = EINK_WIDTH / 8;
  const out = Buffer.alloc(rowBytes * EINK_HEIGHT);
  for (let y = 0; y < EINK_HEIGHT; y++) {
    for (let x = 0; x < EINK_WIDTH; x++) {
      if (data[(y * EINK_WIDTH + x) * 4] > 127) {
        out[y * rowBytes + (x >> 3)] |= 0x80 >> (x & 7);
      }
    }
  }
  return Promise.resolve(out);
}

const APP_LABELS = [
  [/cloudmusic|netease/i, '网易云音乐'],
  [/qqmusic/i, 'QQ音乐'],
  [/kugou/i, '酷狗音乐'],
  [/kuwo/i, '酷我音乐'],
  [/spotify/i, 'Spotify'],
  [/bilibili/i, '哔哩哔哩'],
  [/tencentvideo|qqlive/i, '腾讯视频'],
  [/potplayer/i, 'PotPlayer'],
  [/chrome|msedge|firefox/i, '浏览器'],
];

function appLabel(app) {
  if (!app) {
    return '正在播放';
  }
  for (const [re, label] of APP_LABELS) {
    if (re.test(app)) {
      return label;
    }
  }
  return '正在播放';
}

function hhmm() {
  const d = new Date();
  const pad = (n) => String(n).padStart(2, '0');
  return `${pad(d.getHours())}:${pad(d.getMinutes())}`;
}

/*
 * Owns every write to the e-ink panel. The engine selects a per-app base mode
 * via setBaseMode() (EINK_SET_ACTIVE); media playback overlays a live
 * now-playing card (EINK_SET_IMAGE). When playback ends the base mode is
 * restored, so the panel never gets stuck on a stale card. Pushes are coalesced
 * and rate-limited because an e-paper full refresh is slow.
 */
export class EinkCard {
  constructor({ dynamic, media, minIntervalMs = 4000 }) {
    this.dynamic = dynamic;
    this.media = media;
    this.minIntervalMs = minIntervalMs;
    this.baseIndex = 0;
    this.overlayActive = false;
    this.lastHash = '';
    this.lastPushAt = 0;
    this.coalesceTimer = undefined;
    this.pendingSpec = null;
    this.busy = false;
  }

  start() {
    this.media.on('change', (info) => {
      this.onMedia(info).catch(() => { /* logged inside */ });
    });
    this._learnBase();
    console.log('[eink] now-playing card coordinator started');
  }

  async _learnBase() {
    try {
      const r = await this.dynamic.send({ action: UsbComm.Action.EINK_GET_CONFIG, nop: {} });
      if (r.payload === 'einkModeConfig') {
        this.baseIndex = Number(r.einkModeConfig.activeIndex || 0);
      }
    } catch {
      /* device not ready yet; restore falls back to index 0 */
    }
  }

  // Engine entry point: pick the per-app base mode. Applied immediately when no
  // media overlay is showing; otherwise remembered and applied once media ends.
  setBaseMode(index) {
    this.baseIndex = index | 0;
    if (!this.overlayActive) {
      this._applyBase().catch(() => {});
    }
  }

  async _applyBase() {
    try {
      await this.dynamic.send({
        action: UsbComm.Action.EINK_SET_ACTIVE,
        einkActive: { activeIndex: this.baseIndex },
      });
    } catch (e) {
      console.warn(`[eink] restore base mode failed: ${e.message}`);
    }
  }

  async onMedia(info) {
    const active = info && info.title && (info.status === 'Playing' || info.status === 'Paused');
    if (!active) {
      if (this.overlayActive) {
        this.overlayActive = false;
        await this._applyBase();
      }
      return;
    }

    const spec = {
      app: appLabel(info.app),
      title: info.title,
      artist: info.artist || '',
      playing: info.status === 'Playing',
      time: hhmm(),
    };
    // Ignore time-only changes so the panel isn't refreshed every minute.
    const hash = `${spec.app}|${spec.title}|${spec.artist}|${spec.playing}`;
    if (this.overlayActive && hash === this.lastHash) {
      return;
    }
    this.pendingSpec = { spec, hash };
    this._schedulePush();
  }

  _schedulePush() {
    if (this.busy || this.coalesceTimer) {
      return;
    }
    const wait = Math.max(0, this.minIntervalMs - (Date.now() - this.lastPushAt));
    this.coalesceTimer = setTimeout(() => {
      this.coalesceTimer = undefined;
      this._flush().catch(() => {});
    }, wait);
  }

  async _flush() {
    if (!this.pendingSpec) {
      return;
    }
    const { spec, hash } = this.pendingSpec;
    this.pendingSpec = null;
    this.busy = true;
    try {
      const bits = await renderNowPlaying(spec);
      await this.dynamic.send({
        action: UsbComm.Action.EINK_SET_IMAGE,
        einkImage: { id: Date.now() & 0xffff, bits },
      });
      this.overlayActive = true;
      this.lastHash = hash;
      this.lastPushAt = Date.now();
      console.log(`[eink] now-playing -> "${spec.title}" (${spec.app})`);
    } catch (e) {
      console.warn(`[eink] push failed: ${e.message}`);
    } finally {
      this.busy = false;
      if (this.pendingSpec) {
        this._schedulePush(); // a newer track arrived while rendering
      }
    }
  }
}
