/*
 * Platform abstraction loader. The 中枢 core is OS-agnostic; everything that
 * touches the host OS — the foreground app, now-playing media, key/media
 * injection, and system actions — goes through the adapter selected here by
 * process.platform. Each adapter (win32/linux/darwin) implements the same
 * surface; capabilities a platform can't provide degrade to a logged no-op, so
 * one build runs everywhere.
 */

let cachedPromise;

export function getPlatform() {
  if (!cachedPromise) {
    cachedPromise = (async () => {
      let mod;
      if (process.platform === 'win32') {
        mod = await import('./win32.mjs');
      } else if (process.platform === 'darwin') {
        mod = await import('./darwin.mjs');
      } else {
        mod = await import('./linux.mjs');
      }
      const platform = mod.createPlatform();
      console.log(`[platform] ${platform.name} adapter active`);
      return platform;
    })();
  }
  return cachedPromise;
}
