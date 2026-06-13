import http from 'node:http';
import os from 'node:os';
import path from 'node:path';
import fs from 'node:fs/promises';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';

import { Device } from './keyboard.mjs';
import { Bus } from './bus.mjs';
import { CoreConfig } from './coreConfig.mjs';
import { Weather } from './weather.mjs';
import { Clock } from './clock.mjs';
import { Foreground } from './foreground.mjs';
import { ContextEngine } from './engine.mjs';
import { DEFAULT_RULES } from './rules.mjs';
import { Media } from './media.mjs';
import { Slots } from './slots.mjs';
import { EinkCard } from './eink.mjs';

const HOST = '127.0.0.1';
const PORT = 8755;
const CORE_VERSION = '0.3.0';
const APPDATA_BASE = process.env.APPDATA || path.join(os.homedir(), '.config');
const DATA_DIR = path.join(APPDATA_BASE, 'hw75-core');
const LEGACY_DATA_DIR = path.join(APPDATA_BASE, 'hw75-helper');
const PROFILE_PATH = path.join(DATA_DIR, 'profiles.json');
const CORE_CONFIG_PATH = path.join(DATA_DIR, 'core-config.json');
const SCRIPT_PATH = fileURLToPath(import.meta.url);

const ACTIONS = [
  { code: 100, moduleId: 'system', actionId: 'show_desktop', displayName: '显示桌面', category: '系统', icon: 'desktop', schema: [] },
  { code: 101, moduleId: 'system', actionId: 'lock_screen', displayName: '锁屏', category: '系统', icon: 'lock', schema: [] },
  { code: 102, moduleId: 'system', actionId: 'task_manager', displayName: '任务管理器', category: '系统', icon: 'dashboard', schema: [] },
  {
    code: 200,
    moduleId: 'launcher',
    actionId: 'open_url',
    displayName: '打开 URL',
    category: '启动',
    icon: 'link',
    schema: [{ key: 'url', label: 'URL', type: 'url', required: true, placeholder: 'https://example.com' }],
  },
  {
    code: 201,
    moduleId: 'launcher',
    actionId: 'open_path',
    displayName: '打开路径',
    category: '启动',
    icon: 'folder-open',
    schema: [{ key: 'target', label: '路径', type: 'path', required: true, placeholder: 'C:\\Tools' }],
  },
  {
    code: 202,
    moduleId: 'launcher',
    actionId: 'open_app',
    displayName: '打开应用',
    category: '启动',
    icon: 'appstore',
    schema: [
      { key: 'target', label: '程序路径', type: 'path', required: true, placeholder: 'C:\\Program Files\\App\\app.exe' },
      { key: 'args', label: '参数', type: 'text', required: false, placeholder: '--flag value' },
    ],
  },
  {
    code: 300,
    moduleId: 'command',
    actionId: 'run_command',
    displayName: '执行命令',
    category: '命令',
    icon: 'console-sql',
    schema: [
      { key: 'command', label: '命令', type: 'command', required: true, placeholder: 'python' },
      { key: 'args', label: '参数', type: 'text', required: false, placeholder: 'script.py --arg' },
      { key: 'cwd', label: '工作目录', type: 'path', required: false, placeholder: 'E:\\code' },
    ],
  },
  {
    code: 400,
    moduleId: 'input',
    actionId: 'inject_key',
    displayName: '注入按键',
    category: '输入',
    icon: 'enter',
    schema: [
      { key: 'keys', label: 'SendKeys 序列', type: 'text', required: true, placeholder: '^c / {LEFT} / %{TAB}' },
    ],
  },
  {
    code: 401,
    moduleId: 'input',
    actionId: 'media_key',
    displayName: '媒体键',
    category: '输入',
    icon: 'sound',
    schema: [
      { key: 'key', label: '媒体键', type: 'text', required: true, placeholder: 'volume_up|volume_down|mute|play_pause|next|prev' },
    ],
  },
];

/* Virtual-key codes for the media/volume keys injectable via media_key. */
const MEDIA_VK = {
  volume_up: 0xAF,
  volume_down: 0xAE,
  mute: 0xAD,
  play_pause: 0xB3,
  next: 0xB0,
  prev: 0xB1,
};

let profileState = { nextProfileId: 1, profiles: [] };

await ensureProfileState();

const coreConfig = new CoreConfig(CORE_CONFIG_PATH);
await coreConfig.load();

/* The 中枢 owns a HID session to BOTH boards: the dynamic module (knob / e-ink,
 * the primary for weather/clock pushes) and the keyboard board (touchbar /
 * function slots / RGB). */
const keyboard = new Device({ productHint: 'dynamic', name: 'dynamic' });
const keyboardBoard = new Device({ productHint: 'keyboard', name: 'keyboard' });
const foreground = new Foreground();
foreground.on('change', (info) => {
  console.log(`[foreground] ${info.process} :: ${info.title}`);
});

const media = new Media();
media.on('change', (info) => {
  console.log(`[media] ${info.status}: ${info.title}${info.artist ? ' - ' + info.artist : ''}`);
});

/* The EinkCard owns every write to the dynamic module's e-ink panel: the
 * engine's per-app base mode (EINK_SET_ACTIVE) plus the live now-playing
 * overlay (EINK_SET_IMAGE) rendered from the current media session. */
const einkCard = new EinkCard({ dynamic: keyboard, media });

/* The per-application context engine: foreground app -> device scene. Sends knob
 * feel/detents to the dynamic, RGB theme to the keyboard board, and the e-ink
 * base mode through the EinkCard coordinator. */
const engine = new ContextEngine({ dynamic: keyboard, keyboardBoard, foreground, eink: einkCard });
engine.setRules(DEFAULT_RULES);

/* Drain function-slot triggers from the keyboard board and run their helper
 * actions in-process, so they fire even with no web page open. */
const slots = new Slots({ keyboardBoard, executeEvents });

const server = http.createServer(async (req, res) => {
  try {
    if (handleCors(req, res)) {
      return;
    }

    const url = new URL(req.url ?? '/', `http://${HOST}:${PORT}`);

    if (req.method === 'GET' && url.pathname === '/api/health') {
      return json(res, 200, { ok: true, version: CORE_VERSION });
    }

    if (req.method === 'GET' && url.pathname === '/api/helper-core/status') {
      return json(res, 200, {
        version: CORE_VERSION,
        keyboard: { connected: keyboard.isConnected(), path: keyboard.devicePath ?? null },
        keyboardBoard: { connected: keyboardBoard.isConnected(), path: keyboardBoard.devicePath ?? null },
        foreground: foreground.snapshot(),
        media: media.snapshot(),
        config: coreConfig.snapshot(),
      });
    }

    if (req.method === 'GET' && url.pathname === '/api/catalog') {
      return json(res, 200, { actions: ACTIONS });
    }

    if (req.method === 'GET' && url.pathname === '/api/profiles/export') {
      return json(res, 200, exportProfiles());
    }

    if (req.method === 'GET' && url.pathname.startsWith('/api/profiles/')) {
      const profileId = Number(url.pathname.split('/').pop());
      const profile = profileState.profiles.find((item) => item.id === profileId);
      if (!profile) {
        return json(res, 404, { error: 'Profile not found' });
      }
      return json(res, 200, profile);
    }

    if (req.method === 'POST' && url.pathname === '/api/profiles/upsert') {
      const payload = await readJson(req);
      const result = await upsertProfile(payload);
      return json(res, 200, result);
    }

    if (req.method === 'POST' && url.pathname === '/api/profiles/import') {
      const payload = await readJson(req);
      const result = await importProfiles(payload?.data, !!payload?.replace);
      return json(res, 200, result);
    }

    if (req.method === 'POST' && url.pathname === '/api/events/execute') {
      const payload = await readJson(req);
      const results = await executeEvents(payload?.events ?? []);
      return json(res, 200, { results });
    }

    if (req.method === 'POST' && url.pathname === '/api/restart') {
      scheduleRestart();
      return json(res, 200, { ok: true, restarting: true, version: CORE_VERSION });
    }

    return json(res, 404, { error: 'Not found' });
  } catch (error) {
    return json(res, 500, { error: error instanceof Error ? error.message : String(error) });
  }
});

const weather = new Weather({ keyboard, coreConfig });
const clock = new Clock({ keyboard, coreConfig });
const bus = new Bus({ httpServer: server, keyboard, keyboardBoard, coreConfig, weather });

/* Backfill the bus reference so broadcasters can reach websocket clients. */
weather.bus = bus;
clock.bus = bus;

server.listen(PORT, HOST, () => {
  console.log(`[hw75-core] listening on http://${HOST}:${PORT}`);
  console.log(`[hw75-core] WebSocket at ws://${HOST}:${PORT}/ws`);
  keyboard.start();
  keyboardBoard.start();
  weather.start();
  clock.start();
  foreground.start();
  engine.start();
  media.start();
  slots.start();
  einkCard.start();
});

let restartScheduled = false;

function handleCors(req, res) {
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Methods', 'GET,POST,OPTIONS');
  res.setHeader('Access-Control-Allow-Headers', 'Content-Type');

  if (req.method === 'OPTIONS') {
    res.writeHead(204);
    res.end();
    return true;
  }

  return false;
}

function json(res, status, payload) {
  res.writeHead(status, { 'Content-Type': 'application/json; charset=utf-8' });
  res.end(JSON.stringify(payload));
}

async function readJson(req) {
  const chunks = [];
  for await (const chunk of req) {
    chunks.push(chunk);
  }

  if (!chunks.length) {
    return {};
  }

  return JSON.parse(Buffer.concat(chunks).toString('utf8'));
}

async function migrateLegacyData() {
  // One-time forward-migration from the pre-rename APPDATA folder
  // (hw75-helper -> hw75-core), so existing profiles/config survive the rename.
  for (const file of ['profiles.json', 'core-config.json']) {
    const dest = path.join(DATA_DIR, file);
    const src = path.join(LEGACY_DATA_DIR, file);
    try {
      await fs.access(dest);
      continue; // new file already present — never clobber it
    } catch {
      /* dest missing -> try to bring the legacy one forward */
    }
    try {
      await fs.copyFile(src, dest);
      console.log(`[hw75-core] migrated ${file} from legacy hw75-helper data dir`);
    } catch {
      /* no legacy file; nothing to migrate */
    }
  }
}

async function ensureProfileState() {
  await fs.mkdir(DATA_DIR, { recursive: true });
  await migrateLegacyData();
  try {
    const raw = await fs.readFile(PROFILE_PATH, 'utf8');
    const parsed = JSON.parse(raw);
    profileState = {
      nextProfileId: Number(parsed?.nextProfileId) || 1,
      profiles: Array.isArray(parsed?.profiles) ? parsed.profiles : [],
    };
  } catch {
    await saveProfileState();
  }
}

async function saveProfileState() {
  await fs.writeFile(PROFILE_PATH, JSON.stringify(profileState, null, 2), 'utf8');
}

async function upsertProfile(payload) {
  const actionCode = Number(payload?.actionCode);
  const profilePayload = payload?.payload ?? {};
  const existingId = Number(payload?.profileId || 0);

  ensureActionCode(actionCode);

  let profile = existingId
    ? profileState.profiles.find((item) => item.id === existingId)
    : undefined;

  if (!profile) {
    profile = {
      id: profileState.nextProfileId++,
      actionCode,
      payload: profilePayload,
    };
    profileState.profiles.push(profile);
  } else {
    profile.actionCode = actionCode;
    profile.payload = profilePayload;
  }

  await saveProfileState();
  return profile;
}

function exportProfiles() {
  return {
    version: 1,
    exportedAt: new Date().toISOString(),
    nextProfileId: profileState.nextProfileId,
    profiles: profileState.profiles,
  };
}

async function importProfiles(data, replace) {
  if (!data || typeof data !== 'object' || !Array.isArray(data.profiles)) {
    throw new Error('Invalid helper profile payload');
  }

  const existing = replace ? [] : [...profileState.profiles];
  const profileMap = new Map(existing.map((item) => [item.id, item]));
  let nextProfileId = Math.max(profileState.nextProfileId, Number(data.nextProfileId) || 1);
  let importedCount = 0;

  for (const rawProfile of data.profiles) {
    const actionCode = Number(rawProfile?.actionCode || 0);
    ensureActionCode(actionCode);

    const requestedId = Number(rawProfile?.id || 0);
    const id = requestedId || nextProfileId++;
    profileMap.set(id, {
      id,
      actionCode,
      payload: rawProfile?.payload ?? {},
    });
    nextProfileId = Math.max(nextProfileId, id + 1);
    importedCount++;
  }

  profileState.profiles = [...profileMap.values()].sort((lhs, rhs) => lhs.id - rhs.id);
  profileState.nextProfileId = nextProfileId;
  await saveProfileState();

  return {
    importedCount,
    nextProfileId: profileState.nextProfileId,
  };
}

async function executeEvents(events) {
  const results = [];

  for (const event of events) {
    const actionCode = Number(event?.actionCode);
    const profileId = Number(event?.arg0 || 0);
    const action = ACTIONS.find((item) => item.code === actionCode);
    const profile = profileState.profiles.find((item) => item.id === profileId);

    if (!action) {
      results.push({
        seq: Number(event?.seq || 0),
        slotIndex: Number(event?.slotIndex || 0),
        actionCode,
        ok: false,
        error: `Unknown action code ${actionCode}`,
      });
      continue;
    }

    try {
      await executeAction(action, profile?.payload ?? {});
      results.push({
        seq: Number(event?.seq || 0),
        slotIndex: Number(event?.slotIndex || 0),
        actionCode,
        ok: true,
      });
    } catch (error) {
      results.push({
        seq: Number(event?.seq || 0),
        slotIndex: Number(event?.slotIndex || 0),
        actionCode,
        ok: false,
        error: error instanceof Error ? error.message : String(error),
      });
    }
  }

  return results;
}

async function executeAction(action, payload) {
  switch (action.actionId) {
    case 'show_desktop':
      return runPowerShellWait(`
$shell = New-Object -ComObject Shell.Application
$shell.ToggleDesktop()
`);
    case 'lock_screen':
      return runDetached('rundll32.exe', ['user32.dll,LockWorkStation']);
    case 'task_manager':
      return runPowerShellWait(`
Start-Process -FilePath (Join-Path $env:WINDIR 'System32\\Taskmgr.exe') | Out-Null
Start-Sleep -Milliseconds 200
$ws = New-Object -ComObject WScript.Shell
try {
  [void]$ws.AppActivate('任务管理器')
} catch {
  try {
    [void]$ws.AppActivate('Task Manager')
  } catch {
  }
}
`);
    case 'open_url':
      return openWithShell(targetString(payload.url, 'URL'));
    case 'open_path':
      return openWithShell(targetString(payload.target, 'Target path'));
    case 'open_app':
      return runDetached(targetString(payload.target, 'App path'), splitArgs(payload.args));
    case 'run_command':
      return runDetached(targetString(payload.command, 'Command'), splitArgs(payload.args), optionalString(payload.cwd));
    case 'inject_key':
      return injectSendKeys(targetString(payload.keys, 'Keys'));
    case 'media_key':
      return injectMediaKey(targetString(payload.key, 'Media key'));
    default:
      throw new Error(`Unsupported action ${action.actionId}`);
  }
}

function ensureActionCode(actionCode) {
  if (!ACTIONS.some((action) => action.code === actionCode)) {
    throw new Error(`Unknown action code ${actionCode}`);
  }
}

function targetString(value, fieldName) {
  const normalized = optionalString(value);
  if (!normalized) {
    throw new Error(`${fieldName} is required`);
  }
  return normalized;
}

function optionalString(value) {
  if (typeof value !== 'string') {
    return undefined;
  }
  const normalized = value.trim();
  return normalized ? normalized : undefined;
}

function splitArgs(value) {
  const source = optionalString(value);
  if (!source) {
    return [];
  }

  const args = [];
  let current = '';
  let quote;

  for (const char of source) {
    if (quote) {
      if (char === quote) {
        quote = undefined;
      } else {
        current += char;
      }
      continue;
    }

    if (char === '"' || char === '\'') {
      quote = char;
      continue;
    }

    if (/\s/.test(char)) {
      if (current) {
        args.push(current);
        current = '';
      }
      continue;
    }

    current += char;
  }

  if (quote) {
    throw new Error('Unclosed quote in args');
  }

  if (current) {
    args.push(current);
  }

  return args;
}

function openWithShell(target) {
  return runPowerShellWait('Start-Process -FilePath $env:HW75_TARGET | Out-Null', {
    HW75_TARGET: target,
  });
}

function injectSendKeys(keys) {
  return runPowerShellWait(
    '$ws = New-Object -ComObject WScript.Shell; [void]$ws.SendKeys($env:HW75_KEYS)',
    { HW75_KEYS: keys });
}

function injectMediaKey(name) {
  const vk = MEDIA_VK[name];
  if (vk === undefined) {
    throw new Error(`Unknown media key ${name}`);
  }

  const script = `
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Hw75Key {
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
}
"@
$vk = [byte]$env:HW75_VK
[Hw75Key]::keybd_event($vk, 0, 0, [UIntPtr]::Zero)
[Hw75Key]::keybd_event($vk, 0, 2, [UIntPtr]::Zero)
`;
  return runPowerShellWait(script, { HW75_VK: String(vk) });
}

function encodePowerShell(script) {
  return Buffer.from(script, 'utf16le').toString('base64');
}

function scheduleRestart() {
  if (restartScheduled) {
    return;
  }

  restartScheduled = true;

  const child = spawn('powershell.exe', [
    '-NoProfile',
    '-NonInteractive',
    '-WindowStyle',
    'Hidden',
    '-EncodedCommand',
    encodePowerShell(`
Start-Sleep -Milliseconds 450
& $env:HW75_NODE $env:HW75_SCRIPT
`),
  ], {
    cwd: path.dirname(SCRIPT_PATH),
    detached: true,
    shell: false,
    stdio: 'ignore',
    windowsHide: true,
    env: {
      ...process.env,
      HW75_NODE: process.execPath,
      HW75_SCRIPT: SCRIPT_PATH,
    },
  });

  child.unref();

  setTimeout(() => {
    server.close(() => {
      process.exit(0);
    });

    setTimeout(() => process.exit(0), 800).unref?.();
  }, 120);
}

function runPowerShellWait(script, extraEnv = {}) {
  return new Promise((resolve, reject) => {
    const child = spawn('powershell.exe', [
      '-NoProfile',
      '-NonInteractive',
      '-WindowStyle',
      'Hidden',
      '-EncodedCommand',
      encodePowerShell(script),
    ], {
      shell: false,
      windowsHide: true,
      env: {
        ...process.env,
        ...extraEnv,
      },
    });

    let stderr = '';

    child.stderr.on('data', (chunk) => {
      stderr += chunk.toString();
    });

    child.on('error', reject);
    child.on('close', (code) => {
      if (code === 0) {
        resolve();
        return;
      }

      const message = stderr.trim() || `PowerShell exited with code ${code}`;
      reject(new Error(message));
    });
  });
}

function runDetached(command, args = [], cwd, shell = false, extraEnv = {}) {
  if (!command || typeof command !== 'string') {
    throw new Error('Command is required');
  }

  return new Promise((resolve, reject) => {
    const child = spawn(command, args, {
      cwd,
      detached: true,
      shell,
      stdio: 'ignore',
      windowsHide: true,
      env: {
        ...process.env,
        ...extraEnv,
      },
    });

    child.on('error', reject);
    child.on('spawn', () => {
      child.unref();
      resolve();
    });
  });
}
