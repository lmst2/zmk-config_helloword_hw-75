import { computed, ref } from 'vue';
import { defineStore } from 'pinia';
import { message } from 'ant-design-vue';

import { UsbComm } from '@/proto/comm.proto';
import { useUsbComm } from './usb';
import {
  cloneFunctionSlotDraft,
  draftToProtoSlot,
  protoSlotToDraft,
  type FunctionSlotDraft,
} from '@/utils/function-slots';
import {
  EXPECTED_HELPER_VERSION,
  executeHelperEvents,
  getHelperCatalog,
  getHelperHealthStatus,
  getHelperProfile,
  restartHelper,
  upsertHelperProfile,
  type HelperActionCatalogItem,
  type HelperExecutionResult,
} from '@/utils/helper-bridge';

const EVENT_POLL_INTERVAL_MS = 700;
const HELPER_ACTIVITY_LIMIT = 10;

type HelperActivityEntry = {
  seq: number;
  slotIndex: number;
  actionCode: number;
  summary: string;
  ok: boolean;
  error?: string;
  createdAt: number;
};

export const useFunctionSlotStore = defineStore('function-slots', () => {
  const comm = useUsbComm();

  const caps = ref<UsbComm.IFunctionSlotCaps>();
  const deviceSlots = ref<FunctionSlotDraft[]>([]);
  const draftSlots = ref<FunctionSlotDraft[]>([]);
  const helperAvailable = ref(false);
  const helperVersion = ref<string>();
  const helperCatalog = ref<HelperActionCatalogItem[]>([]);
  const helperError = ref<string>();
  const helperActivity = ref<HelperActivityEntry[]>([]);
  const helperRestarting = ref(false);
  const isApplying = ref(false);
  const lastEventSeq = ref(0);
  let eventPollTimer: number | undefined;
  let polling = false;

  const slotCount = computed(() => Number(caps.value?.slotCount ?? 5));
  const helperExpectedVersion = computed(() => EXPECTED_HELPER_VERSION);
  const helperVersionMatches = computed(() =>
    helperAvailable.value && !!helperVersion.value && helperVersion.value === helperExpectedVersion.value);

  async function refresh(): Promise<void> {
    await Promise.all([refreshHelperCatalog(), getCaps()]);
    const slots: FunctionSlotDraft[] = [];

    for (let slotIndex = 0; slotIndex < slotCount.value; slotIndex++) {
      const nextTransport = new Map(deviceSlotsFromTransport.value);
      nextTransport.delete(slotIndex);
      deviceSlotsFromTransport.value = nextTransport;

      await comm.send({
        action: UsbComm.Action.FUNCTION_SLOT_GET_CONFIG,
        functionSlotRequest: {
          slotIndex,
        },
      }, {
        responseMatcher: (res) =>
          res.payload === 'functionSlotConfig' &&
          Number(res.functionSlotConfig?.slotIndex ?? -1) === slotIndex,
      });

      const transportConfig = deviceSlotsFromTransport.value.get(slotIndex);
      if (!transportConfig) {
        throw new Error(`Function slot ${slotIndex} response missing`);
      }

      const current = protoSlotToDraft(transportConfig);
      if (current.slotType === UsbComm.FunctionSlotType.FUNCTION_SLOT_HELPER_ACTION && current.arg0) {
        current.helperProfileId = current.arg0;
        const profile = await getHelperProfile(current.arg0);
        if (profile) {
          current.helperPayload = {
            ...profile.payload,
            displayName: helperCatalog.value.find((item) => item.code === current.actionCode)?.displayName,
          };
        }
      }
      slots.push(current);
    }

    deviceSlots.value = slots.map(cloneFunctionSlotDraft);
    draftSlots.value = slots.map(cloneFunctionSlotDraft);
  }

  const deviceSlotsFromTransport = ref(new Map<number, UsbComm.IFunctionSlotConfig>());

  function normalizeSlotTransport(config: UsbComm.IFunctionSlotConfig): UsbComm.IFunctionSlotConfig {
    return {
      slotIndex: Number(config.slotIndex ?? 0),
      slotType: Number(config.slotType ?? 0),
      actionCode: Number(config.actionCode ?? 0),
      arg0: Number(config.arg0 ?? 0),
      arg1: Number(config.arg1 ?? 0),
      arg2: Number(config.arg2 ?? 0),
      flags: Number(config.flags ?? 0),
      macroStepsPacked: (config.macroStepsPacked ?? []).map((value) => Number(value)),
    };
  }

  function patchSlotFromTransport(config?: UsbComm.IFunctionSlotConfig): void {
    if (config?.slotIndex === undefined || config?.slotIndex === null) {
      return;
    }

    const next = new Map(deviceSlotsFromTransport.value);
    next.set(Number(config.slotIndex), normalizeSlotTransport(config));
    deviceSlotsFromTransport.value = next;
  }

  async function getCaps(): Promise<void> {
    await comm.send({
      action: UsbComm.Action.FUNCTION_SLOT_GET_FIRMWARE_CAPS,
      functionSlotRequest: {},
    });
  }

  async function refreshHelperCatalog(): Promise<void> {
    const health = await getHelperHealthStatus();
    helperAvailable.value = health.ok;
    helperVersion.value = health.version;

    if (!helperAvailable.value) {
      helperCatalog.value = [];
      helperError.value = '中枢未连接，请运行 start-hw75-dev.cmd';
      return;
    }

    helperCatalog.value = await getHelperCatalog();
    helperError.value = helperVersionMatches.value ? undefined : `中枢版本不匹配，期望 ${helperExpectedVersion.value}`;
  }

  async function restartLocalHelper(): Promise<void> {
    if (!helperAvailable.value) {
      throw new Error('当前浏览器版无法拉起已离线的中枢，请运行 start-hw75-dev.cmd');
    }

    helperRestarting.value = true;
    helperAvailable.value = false;
    helperError.value = '正在重启中枢...';

    try {
      await restartHelper();

      for (let attempt = 0; attempt < 20; attempt++) {
        await new Promise((resolve) => window.setTimeout(resolve, 350));
        const health = await getHelperHealthStatus();
        if (!health.ok) {
          continue;
        }

        helperAvailable.value = true;
        helperVersion.value = health.version;
        helperCatalog.value = await getHelperCatalog();
        helperError.value = helperVersionMatches.value ? undefined : `中枢版本不匹配，期望 ${helperExpectedVersion.value}`;
        return;
      }

      throw new Error('中枢重启后未在预期时间内恢复连接');
    } catch (error) {
      helperAvailable.value = false;
      helperError.value = error instanceof Error ? error.message : String(error);
      throw error;
    } finally {
      helperRestarting.value = false;
    }
  }

  function resetDraft(): void {
    draftSlots.value = deviceSlots.value.map(cloneFunctionSlotDraft);
  }

  function setDraftSlot(slotIndex: number, updater: (slot: FunctionSlotDraft) => FunctionSlotDraft): void {
    draftSlots.value = draftSlots.value.map((slot) =>
      slot.slotIndex === slotIndex ? updater(cloneFunctionSlotDraft(slot)) : slot);
  }

  function swapDraftSlots(fromIndex: number, toIndex: number): void {
    const from = draftSlots.value.find((slot) => slot.slotIndex === fromIndex);
    const to = draftSlots.value.find((slot) => slot.slotIndex === toIndex);
    if (!from || !to || fromIndex === toIndex) {
      return;
    }

    draftSlots.value = draftSlots.value.map((slot) => {
      if (slot.slotIndex === fromIndex) {
        return { ...cloneFunctionSlotDraft(to), slotIndex: fromIndex };
      }
      if (slot.slotIndex === toIndex) {
        return { ...cloneFunctionSlotDraft(from), slotIndex: toIndex };
      }
      return slot;
    });
  }

  function cloneSlotToTarget(sourceIndex: number, targetIndex: number): void {
    const source = draftSlots.value.find((slot) => slot.slotIndex === sourceIndex);
    if (!source) {
      return;
    }

    setDraftSlot(targetIndex, () => ({ ...cloneFunctionSlotDraft(source), slotIndex: targetIndex }));
  }

  async function applyDraft(): Promise<void> {
    isApplying.value = true;
    try {
      const appliedSlots: FunctionSlotDraft[] = [];

      for (const slot of draftSlots.value) {
        const next = cloneFunctionSlotDraft(slot);
        if (next.slotType === UsbComm.FunctionSlotType.FUNCTION_SLOT_HELPER_ACTION) {
          if (!helperAvailable.value) {
            throw new Error('中枢未连接，无法写入中枢动作');
          }

          const catalogEntry = helperCatalog.value.find((item) => item.code === next.actionCode);
          if (!catalogEntry) {
            throw new Error(`未知的中枢动作码: ${next.actionCode}`);
          }

          const profile = await upsertHelperProfile({
            profileId: next.helperProfileId,
            actionCode: next.actionCode,
            payload: next.helperPayload ?? {},
          });
          next.helperProfileId = profile.id;
          next.arg0 = profile.id;
        }

        const nextTransport = new Map(deviceSlotsFromTransport.value);
        nextTransport.delete(next.slotIndex);
        deviceSlotsFromTransport.value = nextTransport;

        await comm.send({
          action: UsbComm.Action.FUNCTION_SLOT_SET_CONFIG,
          functionSlotConfig: draftToProtoSlot(next),
        }, {
          responseMatcher: (res) =>
            res.payload === 'functionSlotConfig' &&
            Number(res.functionSlotConfig?.slotIndex ?? -1) === next.slotIndex,
        });

        const appliedTransport = deviceSlotsFromTransport.value.get(next.slotIndex);
        if (!appliedTransport) {
          throw new Error(`Function slot ${next.slotIndex} apply response missing`);
        }

        const applied = protoSlotToDraft(appliedTransport);
        if (applied.slotType === UsbComm.FunctionSlotType.FUNCTION_SLOT_HELPER_ACTION && applied.arg0) {
          applied.helperProfileId = applied.arg0;
          const profile = await getHelperProfile(applied.arg0);
          if (profile) {
            applied.helperPayload = {
              ...profile.payload,
              displayName: helperCatalog.value.find((item) => item.code === applied.actionCode)?.displayName,
            };
          }
        }

        appliedSlots.push(applied);
      }

      deviceSlots.value = appliedSlots.map(cloneFunctionSlotDraft);
      draftSlots.value = appliedSlots.map(cloneFunctionSlotDraft);
    } finally {
      isApplying.value = false;
    }
  }

  async function pollHelperEvents(): Promise<void> {
    if (!helperAvailable.value || polling || !comm.device) {
      return;
    }

    polling = true;
    try {
      await comm.send({
        action: UsbComm.Action.FUNCTION_SLOT_TRIGGER_EVENT_GET,
        functionSlotRequest: {
          afterSeq: lastEventSeq.value,
          maxCount: 4,
        },
      });
    } finally {
      polling = false;
    }
  }

  function pushHelperActivity(entries: HelperActivityEntry[]): void {
    if (!entries.length) {
      return;
    }

    const seenSeq = new Set(helperActivity.value.map((item) => item.seq));
    const nextEntries = entries.filter((item) => !seenSeq.has(item.seq));
    if (!nextEntries.length) {
      return;
    }

    helperActivity.value = [...nextEntries, ...helperActivity.value].slice(0, HELPER_ACTIVITY_LIMIT);
  }

  function clearHelperActivity(): void {
    helperActivity.value = [];
  }

  function helperActivitySummarySnapshot(slotIndex: number, actionCode: number): string {
    const slot = draftSlots.value.find((item) => item.slotIndex === slotIndex) ??
      deviceSlots.value.find((item) => item.slotIndex === slotIndex);

    if (slot?.slotType === UsbComm.FunctionSlotType.FUNCTION_SLOT_HELPER_ACTION &&
      slot.actionCode === actionCode) {
      const payloadName = slot.helperPayload?.displayName?.toString().trim();
      if (payloadName) {
        return payloadName;
      }
    }

    return helperCatalog.value.find((item) => item.code === actionCode)?.displayName ?? `Helper #${actionCode}`;
  }

  function summarizeFailures(results: HelperExecutionResult[]): string | undefined {
    const failures = results.filter((item) => !item.ok);
    if (!failures.length) {
      return undefined;
    }

    const first = failures[0];
    const base = `Slot ${first.slotIndex + 1}: ${first.error ?? 'Execution failed.'}`;
    return failures.length === 1 ? base : `${base} (${failures.length} failures in this batch)`;
  }

  function handleEventsFromTransport(events?: UsbComm.IFunctionSlotEvents): void {
    const payload = events?.events ?? [];
    if (!payload.length) {
      return;
    }

    const normalized = payload.map((event) => ({
      seq: Number(event.seq ?? 0),
      slotIndex: Number(event.slotIndex ?? 0),
      actionCode: Number(event.actionCode ?? 0),
      arg0: Number(event.arg0 ?? 0),
      arg1: Number(event.arg1 ?? 0),
      arg2: Number(event.arg2 ?? 0),
      flags: Number(event.flags ?? 0),
    }));

    lastEventSeq.value = normalized[normalized.length - 1]?.seq ?? lastEventSeq.value;
    void executeHelperEvents(normalized)
      .then((results) => {
        pushHelperActivity(results.map((result) => ({
          ...result,
          summary: helperActivitySummarySnapshot(result.slotIndex, result.actionCode),
          createdAt: Date.now(),
        })));

        const failureSummary = summarizeFailures(results);
        if (failureSummary) {
          message.error(failureSummary);
        }
      })
      .catch((error) => {
        const text = error instanceof Error ? error.message : String(error);
        helperAvailable.value = false;
        helperError.value = '中枢请求失败，请重启 tools/hw75-core';
        pushHelperActivity(normalized.map((event) => ({
          seq: event.seq,
          slotIndex: event.slotIndex,
          actionCode: event.actionCode,
          summary: helperActivitySummarySnapshot(event.slotIndex, event.actionCode),
          ok: false,
          error: text,
          createdAt: Date.now(),
        })));
        message.error(`中枢桥接请求失败: ${text}`);
      });
  }

  function startEventPolling(): void {
    // The 中枢 (helper-core) now drains and executes function-slot triggers
    // in-process (slots.mjs), so they fire even when this page is closed. The
    // web no longer polls, which would double-execute against the same seq.
    stopEventPolling();
    void pollHelperEvents; // kept for reference; the 中枢 owns the drain loop now
  }

  function stopEventPolling(): void {
    if (eventPollTimer !== undefined) {
      window.clearInterval(eventPollTimer);
      eventPollTimer = undefined;
    }
  }

  function $patchTransport(res: UsbComm.MessageD2H): void {
    if (res.payload === 'functionSlotConfig' && res.functionSlotConfig) {
      patchSlotFromTransport(res.functionSlotConfig);
    }
    if (res.payload === 'functionSlotCaps' && res.functionSlotCaps) {
      caps.value = JSON.parse(JSON.stringify(res.functionSlotCaps)) as UsbComm.IFunctionSlotCaps;
    }
    if (res.payload === 'functionSlotEvents' && res.functionSlotEvents) {
      handleEventsFromTransport(res.functionSlotEvents);
    }
  }

  function $resetState(): void {
    caps.value = undefined;
    deviceSlots.value = [];
    draftSlots.value = [];
    helperCatalog.value = [];
    helperAvailable.value = false;
    helperVersion.value = undefined;
    helperError.value = undefined;
    helperActivity.value = [];
    helperRestarting.value = false;
    lastEventSeq.value = 0;
    stopEventPolling();
    deviceSlotsFromTransport.value = new Map();
  }

  return {
    caps,
    slotCount,
    deviceSlots,
    draftSlots,
    helperAvailable,
    helperVersion,
    helperExpectedVersion,
    helperVersionMatches,
    helperCatalog,
    helperError,
    helperActivity,
    clearHelperActivity,
    helperRestarting,
    isApplying,
    refresh,
    refreshHelperCatalog,
    restartLocalHelper,
    resetDraft,
    setDraftSlot,
    swapDraftSlots,
    cloneSlotToTarget,
    applyDraft,
    startEventPolling,
    stopEventPolling,
    $patchTransport,
    $resetState,
  };
});
