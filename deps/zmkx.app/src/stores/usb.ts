import { onMounted, ref, toRef, watch } from 'vue';
import { defineStore } from 'pinia';

import type {
  IUsbCommDevice,
  IUsbCommTransport,
  UsbTransportTrace,
} from '@/utils/usb/usb';
import { UsbCommHidTransport } from '@/utils/usb/usb-hid';

import { UsbComm } from '@/proto/comm.proto';
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

export const useUsbComm = defineStore('usb', () => {
  const MAX_TRACE_ENTRIES = 40;
  const RESPONSE_TIMEOUT_MS = 1500;

  const device = ref<IUsbCommDevice>();
  const devices = ref<IUsbCommDevice[]>([]);
  const messageListeners = new Set<(res: UsbComm.MessageD2H) => void>();
  const disconnectListeners = new Set<() => void>();
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
  let requestQueue = Promise.resolve();
  let activePendingRequest: PendingRequest | undefined;

  const comm: IUsbCommTransport<IUsbCommDevice> = new UsbCommHidTransport(
    handleList,
    handleTransferIn,
    handleDisconnected,
    handleTransportTrace);

  function handleList(devs: IUsbCommDevice[]): void {
    devices.value = devs;
  }

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

    if (activePendingRequest?.action === res.action &&
      (!activePendingRequest.matcher || activePendingRequest.matcher(res))) {
      clearPendingRequest();
    }
  }

  function handleDisconnected(): void {
    requestQueue = Promise.resolve();
    clearPendingRequest(new Error('USB device disconnected'));
    device.value = undefined;
    versionStore.$resetState();
    knobStore.$resetState();
    rgbStore.$resetState();
    einkStore.$resetState();
    touchbarStore.$resetState();
    functionSlotStore.$resetState();
    disconnectListeners.forEach((listener) => listener());
  }

  function handleTransportTrace(trace: UsbTransportTrace): void {
    const entries = transportTrace.value.slice();
    const lastEntry = entries[entries.length - 1];

    if (lastEntry && sameTrace(lastEntry, trace)) {
      entries[entries.length - 1] = {
        ...lastEntry,
        ts: trace.ts,
        repeatCount: (lastEntry.repeatCount ?? 1) + 1,
      };
      transportTrace.value = entries;
    } else {
      transportTrace.value = [...entries, { ...trace, repeatCount: 1 }].slice(-MAX_TRACE_ENTRIES);
    }

    if (trace.kind === 'tx-message') {
      transportStats.value.txMessages++;
      return;
    }
    if (trace.kind === 'tx-packet') {
      transportStats.value.txPackets++;
      return;
    }
    if (trace.kind === 'rx-packet') {
      transportStats.value.rxPackets++;
      return;
    }
    if (trace.kind === 'rx-decode') {
      transportStats.value.decodedMessages++;
      return;
    }
    if (trace.kind === 'rx-drop') {
      transportStats.value.droppedMessages++;
      return;
    }
    if (trace.kind === 'rx-overflow') {
      transportStats.value.overflowCount++;
    }
  }

  async function pick(dev: IUsbCommDevice): Promise<void> {
    device.value = await comm.pick(dev);
    if (!device.value) {
      throw new Error('Device open failed');
    }
  }

  async function request(): Promise<void> {
    device.value = await comm.request();
    if (!device.value) {
      throw new Error('Device not supported');
    }
  }

  async function close(): Promise<void> {
    await comm.close();
  }

  async function send(message: UsbComm.IMessageH2D, options?: { responseMatcher?: (res: UsbComm.MessageD2H) => boolean }) {
    const sendTask = async () => {
      if (!device.value) {
        throw new Error('Device not connected');
      }

      // Firmware USB comm currently exposes a single receive slot, so host requests
      // must stay strictly request-response serialized.
      const responsePromise = waitForResponse(message.action, options?.responseMatcher);
      try {
        await comm.send(message);
        await responsePromise;
      } catch (error) {
        clearPendingRequest(error instanceof Error ? error : new Error(String(error)));
        throw error;
      }
    };

    const queuedTask = requestQueue.then(sendTask, sendTask);
    requestQueue = queuedTask.catch(() => {});
    await queuedTask;
  }

  function $resetState(): void {
    device.value = undefined;
    transportTrace.value = [];
    transportStats.value = {
      txMessages: 0,
      txPackets: 0,
      rxPackets: 0,
      decodedMessages: 0,
      droppedMessages: 0,
      overflowCount: 0,
    };
  }

  function clearTransportTrace(): void {
    transportTrace.value = [];
    transportStats.value = {
      txMessages: 0,
      txPackets: 0,
      rxPackets: 0,
      decodedMessages: 0,
      droppedMessages: 0,
      overflowCount: 0,
    };
  }

  function clearPendingRequest(error?: Error): void {
    const pending = activePendingRequest;
    activePendingRequest = undefined;
    if (!pending) {
      return;
    }

    window.clearTimeout(pending.timer);
    if (error) {
      pending.reject(error);
      return;
    }
    pending.resolve();
  }

  function waitForResponse(action: UsbComm.Action, matcher?: (res: UsbComm.MessageD2H) => boolean): Promise<void> {
    if (activePendingRequest) {
      return Promise.reject(new Error(`USB transport busy waiting for ${pendingActionLabel(activePendingRequest.action)}`));
    }

    return new Promise<void>((resolve, reject) => {
      const timer = window.setTimeout(() => {
        if (!activePendingRequest || activePendingRequest.action !== action) {
          return;
        }

        activePendingRequest = undefined;
        reject(new Error(`${pendingActionLabel(action)} response timed out`));
      }, RESPONSE_TIMEOUT_MS);

      activePendingRequest = {
        action,
        matcher,
        resolve,
        reject,
        timer,
      };
    });
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

type PendingRequest = {
  action: UsbComm.Action;
  matcher?: (res: UsbComm.MessageD2H) => boolean;
  resolve: () => void;
  reject: (error: Error) => void;
  timer: number;
};

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

function sameTrace(lhs: UsbTransportTrace, rhs: UsbTransportTrace): boolean {
  return lhs.kind === rhs.kind &&
    lhs.summary === rhs.summary &&
    lhs.rawHex === rhs.rawHex &&
    lhs.action === rhs.action &&
    lhs.payload === rhs.payload &&
    lhs.packetLength === rhs.packetLength &&
    lhs.messageLength === rhs.messageLength &&
    lhs.queueLength === rhs.queueLength;
}

function pendingActionLabel(action: UsbComm.Action): string {
  return UsbComm.Action[action] ?? `Action ${action}`;
}
