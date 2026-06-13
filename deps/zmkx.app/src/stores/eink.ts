import { ref } from 'vue';
import { defineStore } from 'pinia';

import { UsbComm } from '@/proto/comm.proto';
import { useHelperCore } from './helperCore';

export const useEinkStore = defineStore('eink', () => {
  const modeConfig = ref<UsbComm.IEinkModeConfig>();
  const capacity = ref<number>(8);

  const core = useHelperCore();

  async function getConfig(): Promise<UsbComm.IEinkModeConfig | undefined> {
    const res = await core.sendViaCore({
      action: UsbComm.Action.EINK_GET_CONFIG,
      nop: {},
    });
    if (res.payload === 'einkModeConfig' && res.einkModeConfig) {
      modeConfig.value = res.einkModeConfig;
      if (res.einkModeConfig.capacity) {
        capacity.value = res.einkModeConfig.capacity;
      }
    }
    return modeConfig.value;
  }

  async function setConfig(modes: UsbComm.IEinkModeEntry[], activeIndex: number): Promise<void> {
    const res = await core.sendViaCore({
      action: UsbComm.Action.EINK_SET_CONFIG,
      einkModeConfig: { modes, activeIndex },
    });
    if (res.payload === 'einkModeConfig' && res.einkModeConfig) {
      modeConfig.value = res.einkModeConfig;
    }
  }

  async function setActive(activeIndex: number): Promise<void> {
    const res = await core.sendViaCore({
      action: UsbComm.Action.EINK_SET_ACTIVE,
      einkActive: { activeIndex },
    });
    if (res.payload === 'einkModeConfig' && res.einkModeConfig) {
      modeConfig.value = res.einkModeConfig;
    }
  }

  async function pushFrame(modeId: number, frameIndex: number, bits: Uint8Array): Promise<void> {
    await core.sendViaCore({
      action: UsbComm.Action.EINK_PUSH_FRAME,
      einkFrame: { modeId, frameIndex, bits },
    });
  }

  /**
   * Push a single full-screen image straight to the panel via the legacy
   * EINK_SET_IMAGE action (firmware handler_eink.c::handle_eink_set_image).
   * The modes/frames system (setConfig + pushFrame, used by StaticEditor.vue)
   * is the primary path; this is the direct one-shot. Routed through helper-core
   * like every other eink call. No UI caller today — kept as a ready API.
   */
  async function setEinkImage(id: number, bits: Uint8Array): Promise<void> {
    await core.sendViaCore({
      action: UsbComm.Action.EINK_SET_IMAGE,
      einkImage: { id, bits },
    });
  }

  function $resetState(): void {
    modeConfig.value = undefined;
  }

  return {
    modeConfig,
    capacity,
    getConfig,
    setConfig,
    setActive,
    pushFrame,
    setEinkImage,
    $resetState,
  };
});
