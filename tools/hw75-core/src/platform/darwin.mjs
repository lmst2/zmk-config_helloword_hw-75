import { runCapture, runWait, runDetached } from './exec.mjs';

/*
 * macOS platform adapter (AppleScript via osascript). Foreground and input are
 * straightforward; generic now-playing is a private framework on macOS, so we
 * best-effort the common players (Music / Spotify). Volume keys use `set
 * volume`; transport keys drive the active player. Unsupported bits degrade to
 * a logged no-op.
 */

function osa(script) {
  return runCapture('osascript', ['-e', script]);
}
function osaWait(script) {
  return runWait('osascript', ['-e', script]);
}

const FOREGROUND_SCRIPT = `
tell application "System Events"
  set p to first application process whose frontmost is true
  set appName to name of p
  set winTitle to ""
  try
    set winTitle to title of front window of p
  end try
  return appName & "|" & winTitle
end tell`;

const NOWPLAYING_SCRIPT = `
on tryApp(appName)
  tell application "System Events"
    if (exists process appName) then
      tell application appName
        try
          if player state is playing or player state is paused then
            return (name of current track) & "|" & (artist of current track) & "|" & (player state as text) & "|" & appName
          end if
        end try
      end tell
    end if
  end tell
  return ""
end tryApp
set r to tryApp("Music")
if r is "" then set r to tryApp("Spotify")
return r`;

function normalizeStatus(s) {
  const v = String(s || '').toLowerCase();
  if (v.includes('play')) return 'Playing';
  if (v.includes('pause')) return 'Paused';
  if (v.includes('stop')) return 'Stopped';
  return '';
}

/* SendKeys -> AppleScript keystroke modifiers (best-effort). */
function sendKeysToAppleScript(keys) {
  const mods = [];
  let rest = keys;
  while (rest.length && '^%+'.includes(rest[0])) {
    if (rest[0] === '^') mods.push('control down');
    else if (rest[0] === '%') mods.push('option down');
    else if (rest[0] === '+') mods.push('shift down');
    rest = rest.slice(1);
  }
  const using = mods.length ? ` using {${mods.join(', ')}}` : '';
  const named = { '{TAB}': 48, '{LEFT}': 123, '{RIGHT}': 124, '{UP}': 126, '{DOWN}': 125,
    '{ENTER}': 36, '{ESC}': 53, '{DELETE}': 51 };
  const code = named[rest.toUpperCase()];
  if (code !== undefined) {
    return `tell application "System Events" to key code ${code}${using}`;
  }
  const ch = rest.replace(/"/g, '\\"');
  return `tell application "System Events" to keystroke "${ch}"${using}`;
}

export function createPlatform() {
  return {
    name: 'darwin',

    async foregroundApp() {
      const out = await osa(FOREGROUND_SCRIPT);
      if (!out) {
        return undefined;
      }
      const [app = '', title = ''] = out.split('|');
      return { process: app.trim().toLowerCase(), title: title.trim() };
    },

    async nowPlaying() {
      const out = await osa(NOWPLAYING_SCRIPT);
      if (!out) {
        return { title: '', artist: '', status: '', app: '' };
      }
      const [title = '', artist = '', status = '', app = ''] = out.split('|');
      return {
        title: title.trim(), artist: artist.trim(),
        status: normalizeStatus(status), app: app.trim(),
      };
    },

    injectKeys(keys) {
      return osaWait(sendKeysToAppleScript(keys));
    },

    injectMediaKey(name) {
      switch (name) {
      case 'volume_up':
        return osaWait('set volume output volume (output volume of (get volume settings) + 6)');
      case 'volume_down':
        return osaWait('set volume output volume (output volume of (get volume settings) - 6)');
      case 'mute':
        return osaWait('set volume with output muted');
      case 'play_pause':
        return osaWait('tell application "System Events" to key code 16 using {function down}'); // F8/play
      case 'next':
        return osaWait('tell application "System Events" to key code 17 using {function down}');
      case 'prev':
        return osaWait('tell application "System Events" to key code 18 using {function down}');
      default:
        return Promise.reject(new Error(`Unknown media key ${name}`));
      }
    },

    showDesktop() {
      // Mission Control "Show Desktop" — F11 by default.
      return osaWait('tell application "System Events" to key code 103');
    },

    lockScreen() {
      return osaWait('tell application "System Events" to keystroke "q" using {control down, command down}');
    },

    taskManager() {
      return runDetached('open', ['-a', 'Activity Monitor']);
    },

    openTarget(target) {
      return runWait('open', [target]);
    },

    runApp(path, args) {
      // A .app bundle opens via `open -a`; a plain executable spawns directly.
      if (path.endsWith('.app')) {
        return runDetached('open', ['-a', path, ...(args && args.length ? ['--args', ...args] : [])]);
      }
      return runDetached(path, args);
    },

    runCommand(command, args, cwd) {
      return runDetached(command, args, cwd);
    },
  };
}
