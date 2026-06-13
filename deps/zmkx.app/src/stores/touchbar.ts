import { ref } from 'vue';
import { defineStore } from 'pinia';

import { UsbComm } from '@/proto/comm.proto';
import { useUsbComm } from './usb';

export const useTouchbarStore = defineStore('touchbar', () => {
  const touchbarConfig = ref<UsbComm.ITouchbarConfig>();

  const comm = useUsbComm();

  async function getTouchbarConfig(): Promise<void> {
    await comm.send({
      action: UsbComm.Action.TOUCHBAR_GET_CONFIG,
      nop: {},
    });
  }

  async function setMode(mode: UsbComm.TouchbarMode): Promise<void> {
    await setConfig({
      ...cloneConfig(touchbarConfig.value),
      mode,
    });
  }

  async function setConfig(config: UsbComm.ITouchbarConfig): Promise<void> {
    touchbarConfig.value = cloneConfig(config);

    await comm.send({
      action: UsbComm.Action.TOUCHBAR_SET_CONFIG,
      touchbarConfig: config,
    });
  }

  function $resetState(): void {
    touchbarConfig.value = undefined;
  }

  return {
    touchbarConfig,
    getTouchbarConfig,
    setMode,
    setConfig,
    $resetState,
  };
});

function cloneConfig(config?: UsbComm.ITouchbarConfig): UsbComm.ITouchbarConfig | undefined {
  if (!config) {
    return undefined;
  }

  return JSON.parse(JSON.stringify(config)) as UsbComm.ITouchbarConfig;
}
