import { computed, ref, watch } from 'vue';
import { defineStore } from 'pinia';

import { UsbComm } from '@/proto/comm.proto';
import { useUsbComm } from './usb';
import { levelLabel, renderLogEvent } from './debug_decode';

const POLL_INTERVAL_MS = 120;
const MAX_BATCH = 4;
const RESPONSE_TIMEOUT_MS = 1500;

type LogEvent = UsbComm.ILogEvent;
type LogSnapshot = UsbComm.ILogSnapshot;
type PendingResponseKind = 'logState' | 'logEvents';

type PendingResponse = {
  kind: PendingResponseKind;
  resolve: () => void;
  reject: (error: Error) => void;
  timer: number;
};

export const useDebugStore = defineStore('debug', () => {
  const comm = useUsbComm();

  const localEvents = ref<LogEvent[]>([]);
  const bootEvents = ref<LogEvent[]>([]);
  const snapshots = ref<LogSnapshot[]>([]);
  const remoteConfig = ref<UsbComm.ILogConfig>();
  const droppedCount = ref(0);
  const oldestSeq = ref(0);
  const newestSeq = ref(0);
  const polling = ref(false);
  const requestCount = ref(0);
  const responseCount = ref(0);
  const otherResponseCount = ref(0);
  const lastRequest = ref('');
  const lastResponse = ref('');
  const lastOtherResponse = ref('');
  const lastError = ref('');
  const pendingResponse = ref<PendingResponseKind>();
  const timeoutCount = ref(0);
  const lastEventBatchCount = ref(0);

  const viewLevel = ref(UsbComm.LogLevel.TRACE);
  const moduleFilter = ref<number[]>([]);
  const keyword = ref('');
  const autoScroll = ref(true);

  let pollTimer: number | undefined;
  let pollStartInFlight = false;
  let activePendingResponse: PendingResponse | undefined;

  function clearPendingResponse(error?: Error): void {
    const pending = activePendingResponse;
    if (!pending) {
      pendingResponse.value = undefined;
      return;
    }

    window.clearTimeout(pending.timer);
    activePendingResponse = undefined;
    pendingResponse.value = undefined;
    if (error) {
      pending.reject(error);
      return;
    }
    pending.resolve();
  }

  function waitForResponse(kind: PendingResponseKind, actionLabel: string): Promise<void> {
    if (activePendingResponse) {
      return Promise.reject(
        new Error(`Debug transport busy waiting for ${activePendingResponse.kind}`),
      );
    }

    pendingResponse.value = kind;

    return new Promise<void>((resolve, reject) => {
      const timer = window.setTimeout(() => {
        if (!activePendingResponse || activePendingResponse.kind !== kind) {
          return;
        }

        activePendingResponse = undefined;
        pendingResponse.value = undefined;
        timeoutCount.value++;
        const error = new Error(`${actionLabel} response timed out`);
        lastError.value = error.message;
        reject(error);
      }, RESPONSE_TIMEOUT_MS);

      activePendingResponse = {
        kind,
        resolve,
        reject,
        timer,
      };
    });
  }

  function scheduleNextPoll(delay = POLL_INTERVAL_MS): void {
    if (!polling.value || !comm.device) {
      return;
    }

    if (pollTimer !== undefined) {
      window.clearTimeout(pollTimer);
    }

    pollTimer = window.setTimeout(() => {
      void runPollLoop();
    }, delay);
  }

  async function runPollLoop(): Promise<void> {
    if (!polling.value || !comm.device) {
      return;
    }

    let nextDelay = POLL_INTERVAL_MS;
    try {
      const batchCount = await pollOnce();
      if (batchCount >= MAX_BATCH) {
        nextDelay = 0;
      }
    } catch {
      // Keep polling alive, but never allow more than one outstanding debug request.
    } finally {
      scheduleNextPoll(nextDelay);
    }
  }

  function applyLogState(logState: UsbComm.ILogState): void {
    remoteConfig.value = logState.config;
    droppedCount.value = logState.droppedCount ?? 0;
    oldestSeq.value = logState.oldestSeq ?? 0;
    newestSeq.value = logState.newestSeq ?? 0;
    bootEvents.value = [...(logState.bootEvents ?? [])];
    snapshots.value = [...(logState.snapshots ?? [])].sort((lhs, rhs) => lhs.module - rhs.module);
  }

  function appendEvents(events: LogEvent[]): void {
    if (!events.length) {
      return;
    }

    const bySeq = new Map(localEvents.value.map((event) => [event.seq, event]));
    events.forEach((event) => bySeq.set(event.seq, event));
    localEvents.value = [...bySeq.values()].sort((lhs, rhs) => lhs.seq - rhs.seq);
  }

  function handleMessage(res: UsbComm.MessageD2H): void {
    if (res.payload == 'logState' && res.logState) {
      responseCount.value++;
      lastResponse.value = 'logState';
      applyLogState(res.logState);
      if (activePendingResponse?.kind === 'logState') {
        clearPendingResponse();
      }
      return;
    }
    if (res.payload == 'logEvents' && res.logEvents) {
      responseCount.value++;
      lastResponse.value = 'logEvents';
      lastEventBatchCount.value = res.logEvents.events?.length ?? 0;
      droppedCount.value = res.logEvents.droppedCount ?? droppedCount.value;
      oldestSeq.value = res.logEvents.oldestSeq ?? oldestSeq.value;
      newestSeq.value = res.logEvents.newestSeq ?? newestSeq.value;
      appendEvents(res.logEvents.events ?? []);
      if (activePendingResponse?.kind === 'logEvents') {
        clearPendingResponse();
      }
      return;
    }

    otherResponseCount.value++;
    lastOtherResponse.value = UsbComm.Action[res.action] ?? `Action ${res.action}`;
  }

  function stopPolling(): void {
    polling.value = false;
    pollStartInFlight = false;
    if (pollTimer !== undefined) {
      window.clearTimeout(pollTimer);
      pollTimer = undefined;
    }
    clearPendingResponse(new Error('Debug polling stopped'));
  }

  async function pollOnce(): Promise<number> {
    if (!comm.device || activePendingResponse) {
      return 0;
    }

    const responsePromise = waitForResponse('logEvents', 'LOG_GET_EVENTS');
    try {
      const afterSeq = localEvents.value.length
        ? localEvents.value[localEvents.value.length - 1].seq
        : 0;
      await comm.send({
        action: UsbComm.Action.LOG_GET_EVENTS,
        logRequest: {
          afterSeq,
          maxCount: MAX_BATCH,
        },
      });
      requestCount.value++;
      lastRequest.value = 'LOG_GET_EVENTS';
      lastError.value = '';
      await responsePromise;
      return lastEventBatchCount.value;
    } catch (error) {
      clearPendingResponse();
      lastError.value = error instanceof Error ? error.message : String(error);
      throw error;
    }
  }

  async function refreshState(): Promise<void> {
    if (!comm.device) {
      return;
    }

    const responsePromise = waitForResponse('logState', 'LOG_GET_STATE');
    try {
      await comm.send({
        action: UsbComm.Action.LOG_GET_STATE,
        logRequest: {},
      });
      requestCount.value++;
      lastRequest.value = 'LOG_GET_STATE';
      lastError.value = '';
      await responsePromise;
    } catch (error) {
      clearPendingResponse();
      lastError.value = error instanceof Error ? error.message : String(error);
      throw error;
    }
  }

  async function startPolling(): Promise<void> {
    if (!comm.device || pollTimer !== undefined || pollStartInFlight) {
      return;
    }

    pollStartInFlight = true;
    polling.value = true;
    try {
      await refreshState();
      if (!polling.value || !comm.device) {
        return;
      }

      await pollOnce();
      if (!polling.value || !comm.device) {
        return;
      }

      scheduleNextPoll();
    } catch (error) {
      polling.value = false;
      throw error;
    } finally {
      pollStartInFlight = false;
    }
  }

  async function setRemoteConfig(patch: UsbComm.ILogConfig): Promise<void> {
    const nextConfig = {
      ...remoteConfig.value,
      ...patch,
    };

    remoteConfig.value = nextConfig;
    await comm.send({
      action: UsbComm.Action.LOG_SET_CONFIG,
      logConfig: nextConfig,
    });
  }

  async function clearRemoteEvents(): Promise<void> {
    await comm.send({
      action: UsbComm.Action.LOG_CLEAR,
      nop: {},
    });
    localEvents.value = [];
  }

  function clearLocalEvents(): void {
    localEvents.value = [];
  }

  function resetRemoteState(): void {
    stopPolling();
    remoteConfig.value = undefined;
    droppedCount.value = 0;
    oldestSeq.value = 0;
    newestSeq.value = 0;
    snapshots.value = [];
    bootEvents.value = [];
    requestCount.value = 0;
    responseCount.value = 0;
    otherResponseCount.value = 0;
    lastRequest.value = '';
    lastResponse.value = '';
    lastOtherResponse.value = '';
    lastError.value = '';
    pendingResponse.value = undefined;
    timeoutCount.value = 0;
    lastEventBatchCount.value = 0;
  }

  const moduleOptions = computed(() => [
    { value: UsbComm.LogModule.SYSTEM, label: 'System' },
    { value: UsbComm.LogModule.USB_COMM, label: 'USB Comm' },
    { value: UsbComm.LogModule.KSCAN, label: 'KScan' },
    { value: UsbComm.LogModule.TOUCHBAR, label: 'TouchBar' },
    { value: UsbComm.LogModule.BEHAVIOR, label: 'Behavior' },
    { value: UsbComm.LogModule.HID, label: 'HID' },
    { value: UsbComm.LogModule.RGB, label: 'RGB' },
    { value: UsbComm.LogModule.INDICATOR, label: 'Indicator' },
    { value: UsbComm.LogModule.SETTINGS, label: 'Settings' },
  ]);

  const moduleLabelMap = computed(() =>
    Object.fromEntries(moduleOptions.value.map((option) => [option.value, option.label])),
  );

  const filteredEvents = computed(() => {
    const keywordValue = keyword.value.trim().toLowerCase();
    const allowedModules = new Set(moduleFilter.value);
    const filterByModules = allowedModules.size > 0;

    return localEvents.value.filter((event) => {
      if (event.level > viewLevel.value) {
        return false;
      }

      if (filterByModules && !allowedModules.has(event.module)) {
        return false;
      }

      if (!keywordValue) {
        return true;
      }

      const haystack = renderLogEvent(event).toLowerCase();
      return haystack.includes(keywordValue);
    });
  });

  function moduleLabel(module: UsbComm.LogModule): string {
    return moduleLabelMap.value[module] ?? `Module ${module}`;
  }

  function formatEvent(event: LogEvent): string {
    return renderLogEvent(event);
  }

  const filteredText = computed(() => filteredEvents.value.map(formatEvent).join('\n'));

  async function copyFilteredText(): Promise<void> {
    await navigator.clipboard.writeText(filteredText.value);
  }

  comm.addMessageListener(handleMessage);
  comm.addDisconnectListener(resetRemoteState);

  watch(
    () => comm.device,
    (device) => {
      if (!device) {
        resetRemoteState();
      }
    },
    { immediate: true },
  );

  return {
    localEvents,
    bootEvents,
    snapshots,
    remoteConfig,
    droppedCount,
    oldestSeq,
    newestSeq,
    polling,
    requestCount,
    responseCount,
    otherResponseCount,
    lastRequest,
    lastResponse,
    lastOtherResponse,
    lastError,
    pendingResponse,
    timeoutCount,
    viewLevel,
    moduleFilter,
    keyword,
    autoScroll,
    moduleOptions,
    filteredEvents,
    filteredText,
    refreshState,
    startPolling,
    stopPolling,
    setRemoteConfig,
    clearRemoteEvents,
    clearLocalEvents,
    copyFilteredText,
    moduleLabel,
    levelLabel,
    formatEvent,
  };
});
