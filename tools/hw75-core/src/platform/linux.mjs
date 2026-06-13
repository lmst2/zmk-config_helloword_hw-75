import { readFile } from 'node:fs/promises';

import { runCapture, runWait, runDetached } from './exec.mjs';

/*
 * Linux platform adapter (X11). Foreground via xdotool, now-playing via
 * playerctl (MPRIS), input via xdotool, open via xdg-open. Anything whose tool
 * isn't installed degrades to a logged no-op rather than crashing.
 */

/* Minimal SendKeys -> xdotool translation for the common cases. The function
 * slot "inject key" payload is Windows SendKeys-shaped; this covers the usual
 * shortcuts and falls back to passing the literal through. */
function sendKeysToXdotool(keys) {
  const mod = [];
  let rest = keys;
  while (rest.length && '^%+'.includes(rest[0])) {
    if (rest[0] === '^') mod.push('ctrl');
    else if (rest[0] === '%') mod.push('alt');
    else if (rest[0] === '+') mod.push('shift');
    rest = rest.slice(1);
  }
  const named = { '{TAB}': 'Tab', '{LEFT}': 'Left', '{RIGHT}': 'Right', '{UP}': 'Up',
    '{DOWN}': 'Down', '{ENTER}': 'Return', '{ESC}': 'Escape', '{BACKSPACE}': 'BackSpace',
    '{DELETE}': 'Delete', '{HOME}': 'Home', '{END}': 'End' };
  let key = named[rest.toUpperCase()] || rest;
  if (!key) key = rest;
  return [...mod, key].join('+');
}

const MEDIA_KEY = {
  volume_up: 'XF86AudioRaiseVolume', volume_down: 'XF86AudioLowerVolume',
  mute: 'XF86AudioMute', play_pause: 'XF86AudioPlay',
  next: 'XF86AudioNext', prev: 'XF86AudioPrev',
};

export function createPlatform() {
  return {
    name: 'linux',

    async foregroundApp() {
      const out = await runCapture('xdotool',
        ['getactivewindow', 'getwindowpid', 'getwindowname'],
        { hint: 'install xdotool for the per-app context engine' });
      if (!out) {
        return undefined;
      }
      const lines = out.split('\n');
      const pid = (lines[0] || '').trim();
      const title = lines.slice(1).join(' ').trim();
      let proc = '';
      if (/^\d+$/.test(pid)) {
        try {
          proc = (await readFile(`/proc/${pid}/comm`, 'utf8')).trim().toLowerCase();
        } catch { /* process gone */ }
      }
      return { process: proc, title };
    },

    async nowPlaying() {
      const out = await runCapture('playerctl',
        ['metadata', '--format', '{{title}}|{{artist}}|{{status}}|{{playerName}}'],
        { hint: 'install playerctl for now-playing' });
      if (!out) {
        return { title: '', artist: '', status: '', app: '' };
      }
      const [title = '', artist = '', status = '', app = ''] = out.split('|');
      return { title: title.trim(), artist: artist.trim(), status: status.trim(), app: app.trim() };
    },

    injectKeys(keys) {
      return runWait('xdotool', ['key', '--clearmodifiers', sendKeysToXdotool(keys)]);
    },

    injectMediaKey(name) {
      const key = MEDIA_KEY[name];
      if (!key) {
        return Promise.reject(new Error(`Unknown media key ${name}`));
      }
      return runWait('xdotool', ['key', key]);
    },

    showDesktop() {
      return runWait('wmctrl', ['-k', 'on']);
    },

    lockScreen() {
      return runWait('loginctl', ['lock-session']);
    },

    taskManager() {
      return runDetached('gnome-system-monitor', []);
    },

    openTarget(target) {
      return runWait('xdg-open', [target]);
    },

    runApp(path, args) {
      return runDetached(path, args);
    },

    runCommand(command, args, cwd) {
      return runDetached(command, args, cwd);
    },
  };
}
