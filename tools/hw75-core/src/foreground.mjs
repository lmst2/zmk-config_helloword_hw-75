import { EventEmitter } from 'node:events';
import { spawn } from 'node:child_process';

/*
 * Foreground-application watcher. Polls the Windows foreground window for its
 * owning process name and title via a P/Invoke PowerShell snippet, and emits
 * 'change' { process, title } whenever either changes. This is the sensor the
 * per-application context engine (F5) keys off — switch app, reshape the device.
 *
 * Polling (not an event hook) is deliberate: it needs no native addon, survives
 * focus storms with a debounce, and a ~600 ms cadence is imperceptible for the
 * "device follows your app" experience while staying cheap.
 */

const QUERY_SCRIPT = `
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

export class Foreground extends EventEmitter {
  constructor({ intervalMs = 600 } = {}) {
    super();
    this.intervalMs = intervalMs;
    this.timer = undefined;
    this.running = false;
    this.current = { process: '', title: '' };
  }

  start() {
    if (this.running) {
      return;
    }
    this.running = true;
    this.poll();
    console.log('[foreground] watcher started');
  }

  stop() {
    this.running = false;
    if (this.timer) {
      clearTimeout(this.timer);
      this.timer = undefined;
    }
  }

  snapshot() {
    return { ...this.current };
  }

  async poll() {
    if (!this.running) {
      return;
    }

    try {
      const info = await queryForeground();
      if (info && (info.process !== this.current.process || info.title !== this.current.title)) {
        const previous = this.current;
        this.current = info;
        this.emit('change', info, previous);
      }
    } catch {
      /* transient PowerShell/launch failures are ignored; next tick retries */
    }

    if (this.running) {
      this.timer = setTimeout(() => this.poll(), this.intervalMs);
    }
  }
}

function queryForeground() {
  return new Promise((resolve, reject) => {
    const child = spawn('powershell.exe', [
      '-NoProfile',
      '-NonInteractive',
      '-WindowStyle', 'Hidden',
      '-EncodedCommand', Buffer.from(QUERY_SCRIPT, 'utf16le').toString('base64'),
    ], { windowsHide: true });

    let stdout = '';
    child.stdout.on('data', (chunk) => { stdout += chunk.toString('utf8'); });
    child.on('error', reject);
    child.on('close', () => {
      const text = stdout.trim();
      if (!text) {
        resolve(undefined);
        return;
      }
      try {
        const parsed = JSON.parse(text);
        resolve({
          process: String(parsed.process || '').toLowerCase(),
          title: String(parsed.title || ''),
        });
      } catch {
        resolve(undefined);
      }
    });
  });
}
