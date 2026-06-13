import { spawn } from 'node:child_process';

import { UsbComm } from './protoLoader.mjs';

/*
 * E-ink now-playing card for the HW-75 dynamic module.
 *
 * The panel is a portrait 128x296, 1bpp framebuffer in the SAME format the
 * firmware renderer (eink_render.c) and the web (utils/graphic.ts::toBits) use:
 * row-major, pitch = width/8 = 16 bytes/row, MSB-first within each byte, bit
 * value 1 = white. We clear to white and draw black text, so the result is
 * black text on a white background — no inversion (matches what the device
 * already shows).
 *
 * renderNowPlaying() rasterizes a card with GDI+ (System.Drawing) via
 * PowerShell and returns the exact 4736-byte frame; EinkCard owns every write
 * to the panel so the live media overlay and the engine's per-app base mode
 * never fight over it.
 */

const EINK_WIDTH = 128;
const EINK_HEIGHT = 296;
const EINK_FRAME_BYTES = (EINK_WIDTH / 8) * EINK_HEIGHT; // 4736

// Portrait 128x296 card. SingleBitPerPixelGridFit + SmoothingMode.None keep the
// output pure black/white (no anti-aliased grays), so packing is unambiguous.
const RENDER_SCRIPT = `
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class Hw75Eink {
  public static string Pack(Bitmap bmp, int w, int h) {
    var rect = new Rectangle(0, 0, w, h);
    var data = bmp.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
    int stride = data.Stride;
    byte[] src = new byte[stride * h];
    Marshal.Copy(data.Scan0, src, 0, src.Length);
    bmp.UnlockBits(data);
    int rowBytes = w / 8;
    byte[] outp = new byte[rowBytes * h];
    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        int p = y * stride + x * 4;          // BGRA; pure b/w so any channel works
        if (src[p + 2] > 127) {              // white pixel -> bit 1, MSB-first
          outp[y * rowBytes + (x >> 3)] |= (byte)(0x80 >> (x & 7));
        }
      }
    }
    return Convert.ToBase64String(outp);
  }
}
"@

$spec = $env:HW75_EINK_SPEC | ConvertFrom-Json
$W = 128; $H = 296
$bmp = New-Object System.Drawing.Bitmap($W, $H)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.Clear([System.Drawing.Color]::White)
$g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::SingleBitPerPixelGridFit
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::None

$black = [System.Drawing.Brushes]::Black
$pen = New-Object System.Drawing.Pen([System.Drawing.Color]::Black, 2)

$fApp = New-Object System.Drawing.Font('Microsoft YaHei', 10, [System.Drawing.FontStyle]::Bold)
$fTitle = New-Object System.Drawing.Font('Microsoft YaHei', 14, [System.Drawing.FontStyle]::Bold)
$fSub = New-Object System.Drawing.Font('Microsoft YaHei', 10)
$fFoot = New-Object System.Drawing.Font('Consolas', 11)

$sfC = New-Object System.Drawing.StringFormat
$sfC.Alignment = [System.Drawing.StringAlignment]::Center
$sfTitle = New-Object System.Drawing.StringFormat
$sfTitle.Alignment = [System.Drawing.StringAlignment]::Center
$sfTitle.LineAlignment = [System.Drawing.StringAlignment]::Center
$sfTitle.Trimming = [System.Drawing.StringTrimming]::EllipsisCharacter

# Header: source app, with a divider beneath (mirrors eink_render.c's dividers)
$g.DrawString([string]$spec.app, $fApp, $black, (New-Object System.Drawing.RectangleF(4, 6, 120, 20)), $sfC)
$g.DrawLine($pen, 8, 30, 119, 30)

# Play/pause glyph (drawn from primitives, no font-glyph dependency) + status,
# centered as a group via MeasureString so CJK text never clips.
$statusText = if ($spec.playing) { '正在播放' } else { '已暂停' }
$sz = $g.MeasureString($statusText, $fSub)
$ty = 40
$tx = [int](($W - $sz.Width) / 2) + 9
$g.DrawString($statusText, $fSub, $black, [float]$tx, [float]$ty)
$gx = $tx - 19
if ($spec.playing) {
  $tri = New-Object 'System.Drawing.PointF[]' 3
  $tri[0] = New-Object System.Drawing.PointF($gx, ($ty + 3))
  $tri[1] = New-Object System.Drawing.PointF($gx, ($ty + 15))
  $tri[2] = New-Object System.Drawing.PointF(($gx + 12), ($ty + 9))
  $g.FillPolygon($black, $tri)
} else {
  $g.FillRectangle($black, $gx, ($ty + 3), 4, 12)
  $g.FillRectangle($black, ($gx + 7), ($ty + 3), 4, 12)
}

# Title (wrapped + vertically centered in its box, ellipsis on overflow)
$g.DrawString([string]$spec.title, $fTitle, $black, (New-Object System.Drawing.RectangleF(6, 74, 116, 92)), $sfTitle)

# Artist
$g.DrawString([string]$spec.artist, $fSub, $black, (New-Object System.Drawing.RectangleF(6, 176, 116, 44)), $sfC)

# Footer: divider + wall-clock time
$g.DrawLine($pen, 8, 250, 119, 250)
$g.DrawString([string]$spec.time, $fFoot, $black, (New-Object System.Drawing.RectangleF(4, 258, 120, 20)), $sfC)

$g.Dispose()
$b64 = [Hw75Eink]::Pack($bmp, $W, $H)
$bmp.Dispose()
[Console]::Out.Write($b64)
`;

export function renderNowPlaying(spec) {
  return new Promise((resolve, reject) => {
    const child = spawn('powershell.exe', [
      '-NoProfile', '-NonInteractive', '-WindowStyle', 'Hidden',
      '-EncodedCommand', Buffer.from(RENDER_SCRIPT, 'utf16le').toString('base64'),
    ], { windowsHide: true, env: { ...process.env, HW75_EINK_SPEC: JSON.stringify(spec) } });

    let stdout = '';
    let stderr = '';
    child.stdout.on('data', (c) => { stdout += c.toString('utf8'); });
    child.stderr.on('data', (c) => { stderr += c.toString('utf8'); });
    child.on('error', reject);
    child.on('close', (code) => {
      const b64 = stdout.trim();
      if (!b64) {
        reject(new Error(stderr.trim() || `eink render failed (code ${code})`));
        return;
      }
      const buf = Buffer.from(b64, 'base64');
      if (buf.length !== EINK_FRAME_BYTES) {
        reject(new Error(`eink frame is ${buf.length}B, expected ${EINK_FRAME_BYTES}B`));
        return;
      }
      resolve(buf);
    });
  });
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
