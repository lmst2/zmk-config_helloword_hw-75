import { spawn } from 'node:child_process';

/* Shared POSIX exec helpers for the linux/darwin adapters. */

const warned = new Set();

/* Run a command and resolve with trimmed stdout; resolve '' on any failure
 * (missing tool, non-zero exit). Logs a one-time hint when a tool is absent. */
export function runCapture(cmd, args = [], { hint } = {}) {
  return new Promise((resolve) => {
    let child;
    try {
      child = spawn(cmd, args, { windowsHide: true });
    } catch {
      resolve('');
      return;
    }
    let stdout = '';
    child.stdout.on('data', (c) => { stdout += c.toString('utf8'); });
    child.on('error', (err) => {
      if (err.code === 'ENOENT' && hint && !warned.has(cmd)) {
        warned.add(cmd);
        console.warn(`[platform] '${cmd}' not found — ${hint}`);
      }
      resolve('');
    });
    child.on('close', () => resolve(stdout.trim()));
  });
}

/* Run to completion; reject on non-zero exit or missing tool. */
export function runWait(cmd, args = [], { cwd } = {}) {
  return new Promise((resolve, reject) => {
    let child;
    try {
      child = spawn(cmd, args, { cwd, windowsHide: true });
    } catch (err) {
      reject(err);
      return;
    }
    let stderr = '';
    child.stderr.on('data', (c) => { stderr += c.toString(); });
    child.on('error', reject);
    child.on('close', (code) => {
      if (code === 0) {
        resolve();
      } else {
        reject(new Error(stderr.trim() || `${cmd} exited with code ${code}`));
      }
    });
  });
}

export function runDetached(cmd, args = [], cwd) {
  return new Promise((resolve, reject) => {
    let child;
    try {
      child = spawn(cmd, args, { cwd, detached: true, stdio: 'ignore', windowsHide: true });
    } catch (err) {
      reject(err);
      return;
    }
    child.on('error', reject);
    child.on('spawn', () => { child.unref(); resolve(); });
  });
}
