# Vendored: zmkx.app (HW-75 companion web app)

This directory is a **vendored, locally-patched** copy of the upstream project,
maintained as part of this repo's source (per AGENTS.md §2). It is no longer a
nested git repository.

- Upstream: https://github.com/xingrz/zmkx.app.git
- Vendored from commit: `6c96c76fad539664240cbd532ea988e72d32e683` ("i18n", 2023-10-19)
- Local divergence: protocol is single-sourced from `config/proto/usb_comm.proto`
  (see `build-proto.mjs`); helper-core integration, eink multimode, i18n and other
  HW-75-specific changes have been applied on top and are not upstream.

Generated products (`src/proto/comm.proto.{js,d.ts}`, `src/generated/keyboard-config.ts`)
are rebuilt by `npm ci` (postinstall runs `build:proto` + `build:keyboard`);
`src/proto` and `node_modules`/`dist` are gitignored.
