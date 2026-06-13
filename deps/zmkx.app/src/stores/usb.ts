import { onMounted, ref, toRef, watch } from 'vue';
import { defineStore } from 'pinia';

import type { IUsbCommDevice, UsbTransportTrace } from '@/utils/usb/usb';

import { UsbComm } from '@/proto/comm.proto';
import { useHelperCore } from './helperCore';
import { useVersionStore } from './version';
import { useKnobStore } from './knob';
import { useRgbStore } from './rgb';
import { useEinkStore } from './eink';
import { useTouchbarStore } from './touchbar';
import { useFunctionSlotStore } from './function-slots';

export enum TransportType {
  USB_VENDOR,
  USB_HID,
}

/*
 * Pure-frontend transport. The web app no longer opens WebHID itself; every
 * request now goes through the 中枢 (helper-core) over WebSocket, which owns the
 * HID session to BOTH boards and routes by action. This store keeps the original
 * useUsbComm API so the existing stores/routes are untouched — only the
 * transport beneath it moved from WebHID to the 中枢.
 */

// The 中枢 owns the real device; routes only read `device` as a truthy
// "connected" flag, so a sentinel object is sufficient.
const CONNECTED_DEVICE = {} as unknown as IUsbCommDevice;

export const useUsbComm = defineStore('usb', () => {
  const core = useHelperCore();

  const device = ref<IUsbCommDevice>();
  const devices = ref<IUsbCommDevice[]>([]);
  const messageListeners = new Set<(res: UsbComm.MessageD2H) => void>();
  const disconnectListeners = new Set<() => void>();
  // Kept for the Debug page binding; per-packet tracing lived in the WebHID
  // transport and is not surfaced over the 中枢 link.
  const transportTrace = ref<UsbTransportTrace[]>([]);
  const transportStats = ref({
    txMessages: 0,
    txPackets: 0,
    rxPackets: 0,
    decodedMessages: 0,
    droppedMessages: 0,
    overflowCount: 0,
  });

  const versionStore = useVersionStore();
  const knobStore = useKnobStore();
  const rgbStore = useRgbStore();
  const einkStore = useEinkStore();
  const touchbarStore = useTouchbarStore();
  const functionSlotStore = useFunctionSlotStore();

  function handleTransferIn(res: UsbComm.MessageD2H): void {
    if (res.payload == 'version' && res.version) {
      versionStore.$patch({ version: res.version });
    }
    if (res.payload == 'motorState' && res.motorState) {
      knobStore.$patch({ motorState: res.motorState });
    }
    if (res.payload == 'knobConfig' && res.knobConfig) {
      knobStore.$patch({ knobConfig: res.knobConfig });
    }
    if (res.payload == 'knobPref' && res.knobPref) {
      knobStore.$patchKnobPref(res.knobPref);
    }
    if (res.payload == 'rgbState' && res.rgbState) {
      rgbStore.$patch({ state: res.rgbState });
    }
    if (res.payload == 'rgbIndicator' && res.rgbIndicator) {
      rgbStore.$patch({ indicator: res.rgbIndicator });
    }
    if (res.payload == 'touchbarConfig' && res.touchbarConfig) {
      touchbarStore.$patch({ touchbarConfig: res.touchbarConfig });
    }

    functionSlotStore.$patchTransport(res);

    messageListeners.forEach((listener) => listener(res));
  }

  function handleDisconnected(): void {
    device.value = undefined;
    versionStore.$resetState();
    knobStore.$resetState();
    rgbStore.$resetState();
    einkStore.$resetState();
    touchbarStore.$resetState();
    functionSlotStore.$resetState();
    disconnectListeners.forEach((listener) => listener());
  }

  // Async device -> host events (e.g. function-slot triggers) arrive over the
  // 中枢 event channel; dispatch them through the same store-patch path.
  core.onKeyboardEvent((res) => handleTransferIn(res));

  // Reflect the 中枢 connection as the device connection.
  watch(
    () => core.connected,
    (connected) => {
      if (connected) {
        device.value = CONNECTED_DEVICE;
      } else if (device.value) {
        handleDisconnected();
      }
    },
    { immediate: true },
  );

  async function pick(_device?: IUsbCommDevice): Promise<void> {
    void _device; // the 中枢 owns device selection; nothing to pick on the web
    if (!core.connected) {
      throw new Error('中枢未连接');
    }
    device.value = CONNECTED_DEVICE;
  }

  async function request(): Promise<void> {
    if (!core.connected) {
      throw new Error('中枢未连接');
    }
    device.value = CONNECTED_DEVICE;
  }

  async function close(): Promise<void> {
    device.value = undefined;
  }

  async function send(
    message: UsbComm.IMessageH2D,
    options?: { responseMatcher?: (res: UsbComm.MessageD2H) => boolean },
  ): Promise<void> {
    void options; // the 中枢 resolves each request by id; no matcher needed
    if (!core.connected) {
      throw new Error('中枢未连接');
    }
    const res = await core.sendViaCore(message);
    handleTransferIn(res);
  }

  function $resetState(): void {
    device.value = core.connected ? CONNECTED_DEVICE : undefined;
    transportTrace.value = [];
  }

  function clearTransportTrace(): void {
    transportTrace.value = [];
  }

  function addMessageListener(listener: (res: UsbComm.MessageD2H) => void): () => void {
    messageListeners.add(listener);
    return () => messageListeners.delete(listener);
  }

  function addDisconnectListener(listener: () => void): () => void {
    disconnectListeners.add(listener);
    return () => disconnectListeners.delete(listener);
  }

  return {
    device,
    devices,
    transportTrace,
    transportStats,
    pick,
    request,
    close,
    send,
    $resetState,
    clearTransportTrace,
    addMessageListener,
    addDisconnectListener,
  };
});

type IUsbCommStore = ReturnType<typeof useUsbComm>;

export function onDeviceConnected(store: IUsbCommStore, callback: (device: IUsbCommDevice) => void) {
  const device = toRef(store, 'device');

  function onConnected() {
    setTimeout(() => {
      if (device.value) {
        callback(device.value);
      }
    }, 10);
  }

  onMounted(onConnected);
  watch(device, onConnected);
}
