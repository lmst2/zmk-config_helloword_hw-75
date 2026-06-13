import { EventEmitter } from 'node:events';
import { spawn } from 'node:child_process';

/*
 * Now-Playing reader via the Windows System Media Transport Controls (SMTC).
 * Polls the current media session for title / artist / playback status using a
 * WinRT call from PowerShell, and emits 'change' when any of them changes. Feeds
 * the e-ink Now-Playing card and lets the context engine know media is playing.
 */

const QUERY_SCRIPT = `
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

export class Media extends EventEmitter {
  constructor({ intervalMs = 1500 } = {}) {
    super();
    this.intervalMs = intervalMs;
    this.timer = undefined;
    this.running = false;
    this.current = { title: '', artist: '', status: '', app: '' };
  }

  start() {
    if (this.running) {
      return;
    }
    this.running = true;
    this.poll();
    console.log('[media] now-playing watcher started');
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
      const info = await queryMedia();
      if (info && (info.title !== this.current.title || info.artist !== this.current.artist ||
                   info.status !== this.current.status || info.app !== this.current.app)) {
        this.current = info;
        this.emit('change', info);
      }
    } catch {
      /* ignore transient failures */
    }

    if (this.running) {
      this.timer = setTimeout(() => this.poll(), this.intervalMs);
    }
  }
}

function queryMedia() {
  return new Promise((resolve, reject) => {
    const child = spawn('powershell.exe', [
      '-NoProfile', '-NonInteractive', '-WindowStyle', 'Hidden',
      '-EncodedCommand', Buffer.from(QUERY_SCRIPT, 'utf16le').toString('base64'),
    ], { windowsHide: true });

    let stdout = '';
    child.stdout.on('data', (chunk) => { stdout += chunk.toString('utf8'); });
    child.on('error', reject);
    child.on('close', () => {
      const text = stdout.trim();
      try {
        const parsed = text ? JSON.parse(text) : {};
        resolve({
          title: String(parsed.title || ''),
          artist: String(parsed.artist || ''),
          status: String(parsed.status || ''),
          app: String(parsed.app || ''),
        });
      } catch {
        resolve(undefined);
      }
    });
  });
}
