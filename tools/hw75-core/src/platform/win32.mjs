import { spawn } from 'node:child_process';

/*
 * Windows platform adapter. Every OS-specific capability the 中枢 needs is
 * implemented here with PowerShell / P/Invoke; the core stays OS-agnostic and
 * talks to this through the common surface in ./index.mjs. (linux.mjs and
 * darwin.mjs implement the same surface for their OSes.)
 */

function encodePowerShell(script) {
  return Buffer.from(script, 'utf16le').toString('base64');
}

/* Run a hidden PowerShell script and resolve with trimmed stdout. */
function runPsCapture(script, extraEnv = {}) {
  return new Promise((resolve) => {
    const child = spawn('powershell.exe', [
      '-NoProfile', '-NonInteractive', '-WindowStyle', 'Hidden',
      '-EncodedCommand', encodePowerShell(script),
    ], { windowsHide: true, env: { ...process.env, ...extraEnv } });

    let stdout = '';
    child.stdout.on('data', (c) => { stdout += c.toString('utf8'); });
    child.on('error', () => resolve(''));
    child.on('close', () => resolve(stdout.trim()));
  });
}

/* Run a hidden PowerShell script to completion, rejecting on non-zero exit. */
function runPsWait(script, extraEnv = {}) {
  return new Promise((resolve, reject) => {
    const child = spawn('powershell.exe', [
      '-NoProfile', '-NonInteractive', '-WindowStyle', 'Hidden',
      '-EncodedCommand', encodePowerShell(script),
    ], { windowsHide: true, env: { ...process.env, ...extraEnv } });

    let stderr = '';
    child.stderr.on('data', (c) => { stderr += c.toString(); });
    child.on('error', reject);
    child.on('close', (code) => {
      if (code === 0) {
        resolve();
      } else {
        reject(new Error(stderr.trim() || `PowerShell exited with code ${code}`));
      }
    });
  });
}

function runDetached(command, args = [], cwd, extraEnv = {}) {
  if (!command || typeof command !== 'string') {
    throw new Error('Command is required');
  }
  return new Promise((resolve, reject) => {
    const child = spawn(command, args, {
      cwd, detached: true, shell: false, stdio: 'ignore', windowsHide: true,
      env: { ...process.env, ...extraEnv },
    });
    child.on('error', reject);
    child.on('spawn', () => { child.unref(); resolve(); });
  });
}

const FOREGROUND_SCRIPT = `
$ErrorActionPreference = 'SilentlyContinue'
Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class Hw75Fg {
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern int GetWindowThreadProcessId(IntPtr h, out int pid);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
}
"@
$h = [Hw75Fg]::GetForegroundWindow()
$procId = 0
[void][Hw75Fg]::GetWindowThreadProcessId($h, [ref]$procId)
$sb = New-Object System.Text.StringBuilder 512
[void][Hw75Fg]::GetWindowText($h, $sb, 512)
$p = Get-Process -Id $procId -ErrorAction SilentlyContinue
$name = if ($p) { $p.ProcessName + '.exe' } else { '' }
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
ConvertTo-Json -Compress @{ process = $name.ToLower(); title = $sb.ToString() }
`;

const NOWPLAYING_SCRIPT = `
$ErrorActionPreference = 'SilentlyContinue'
Add-Type -AssemblyName System.Runtime.WindowsRuntime
$asTask = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
  $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and
  $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation\`1' })[0]
function Await($op, $type) {
  $t = $asTask.MakeGenericMethod($type).Invoke($null, @($op))
  $null = $t.Wait(2000)
  $t.Result
}
[Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager, Windows.Media.Control, ContentType=WindowsRuntime] | Out-Null
$mgr = Await ([Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager]::RequestAsync()) ([Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager])
$s = $mgr.GetCurrentSession()
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
if ($s) {
  $p = Await ($s.TryGetMediaPropertiesAsync()) ([Windows.Media.Control.GlobalSystemMediaTransportControlsSessionMediaProperties])
  $pb = $s.GetPlaybackInfo()
  ConvertTo-Json -Compress @{ title = [string]$p.Title; artist = [string]$p.Artist; status = [string]$pb.PlaybackStatus; app = [string]$s.SourceAppUserModelId }
} else {
  '{}'
}
`;

/* Virtual-key codes for the media/volume keys. */
const MEDIA_VK = {
  volume_up: 0xAF, volume_down: 0xAE, mute: 0xAD,
  play_pause: 0xB3, next: 0xB0, prev: 0xB1,
};

export function createPlatform() {
  return {
    name: 'win32',

    async foregroundApp() {
      const text = await runPsCapture(FOREGROUND_SCRIPT);
      if (!text) {
        return undefined;
      }
      try {
        const parsed = JSON.parse(text);
        return {
          process: String(parsed.process || '').toLowerCase(),
          title: String(parsed.title || ''),
        };
      } catch {
        return undefined;
      }
    },

    async nowPlaying() {
      const text = await runPsCapture(NOWPLAYING_SCRIPT);
      try {
        const parsed = text ? JSON.parse(text) : {};
        return {
          title: String(parsed.title || ''),
          artist: String(parsed.artist || ''),
          status: String(parsed.status || ''),
          app: String(parsed.app || ''),
        };
      } catch {
        return { title: '', artist: '', status: '', app: '' };
      }
    },

    injectKeys(keys) {
      return runPsWait(
        '$ws = New-Object -ComObject WScript.Shell; [void]$ws.SendKeys($env:HW75_KEYS)',
        { HW75_KEYS: keys });
    },

    injectMediaKey(name) {
      const vk = MEDIA_VK[name];
      if (vk === undefined) {
        return Promise.reject(new Error(`Unknown media key ${name}`));
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
      return runPsWait(script, { HW75_VK: String(vk) });
    },

    showDesktop() {
      return runPsWait('$shell = New-Object -ComObject Shell.Application; $shell.ToggleDesktop()');
    },

    lockScreen() {
      return runDetached('rundll32.exe', ['user32.dll,LockWorkStation']);
    },

    taskManager() {
      return runPsWait(`
Start-Process -FilePath (Join-Path $env:WINDIR 'System32\\Taskmgr.exe') | Out-Null
Start-Sleep -Milliseconds 200
$ws = New-Object -ComObject WScript.Shell
try { [void]$ws.AppActivate('任务管理器') } catch { try { [void]$ws.AppActivate('Task Manager') } catch {} }
`);
    },

    openTarget(target) {
      return runPsWait('Start-Process -FilePath $env:HW75_TARGET | Out-Null', { HW75_TARGET: target });
    },

    runApp(path, args) {
      return runDetached(path, args);
    },

    runCommand(command, args, cwd) {
      return runDetached(command, args, cwd);
    },
  };
}
