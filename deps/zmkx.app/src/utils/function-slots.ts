import type { UsbComm } from '@/proto/comm.proto';

export const FUNCTION_SLOT_KEYS = [
  { slotIndex: 0, rowIndex: 0, bindingIndex: 13, label: 'PAUSE' },
  { slotIndex: 1, rowIndex: 1, bindingIndex: 14, label: 'INSERT' },
  { slotIndex: 2, rowIndex: 2, bindingIndex: 14, label: 'DELETE' },
  { slotIndex: 3, rowIndex: 3, bindingIndex: 13, label: 'PG UP' },
  { slotIndex: 4, rowIndex: 4, bindingIndex: 13, label: 'PG DN' },
] as const;

export const FUNCTION_SLOT_MODIFIERS = [
  { mask: 0x01, label: 'Ctrl', shortLabel: 'Ctrl' },
  { mask: 0x02, label: 'Shift', shortLabel: 'Shift' },
  { mask: 0x04, label: 'Alt', shortLabel: 'Alt' },
  { mask: 0x08, label: 'Win', shortLabel: 'Win' },
  { mask: 0x10, label: 'RCtrl', shortLabel: 'RCtrl' },
  { mask: 0x20, label: 'RShift', shortLabel: 'RShift' },
  { mask: 0x40, label: 'RAlt', shortLabel: 'RAlt' },
  { mask: 0x80, label: 'RWin', shortLabel: 'RWin' },
] as const;

export const FUNCTION_SLOT_HID_PRESETS = [
  { code: 1, label: '静音', category: '媒体', accent: '#f5a623' },
  { code: 2, label: '音量加', category: '媒体', accent: '#f5a623' },
  { code: 3, label: '音量减', category: '媒体', accent: '#f5a623' },
  { code: 4, label: '播放 / 暂停', category: '媒体', accent: '#f5a623' },
  { code: 5, label: '上一曲', category: '媒体', accent: '#f5a623' },
  { code: 6, label: '下一曲', category: '媒体', accent: '#f5a623' },
  { code: 7, label: '停止', category: '媒体', accent: '#f5a623' },
  { code: 8, label: '系统睡眠', category: '系统', accent: '#3cc4b7' },
  { code: 9, label: '系统唤醒', category: '系统', accent: '#3cc4b7' },
  { code: 10, label: '系统电源', category: '系统', accent: '#3cc4b7' },
  { code: 11, label: '计算器', category: '应用', accent: '#7f8cff' },
  { code: 12, label: '邮件', category: '应用', accent: '#7f8cff' },
  { code: 13, label: '浏览器', category: '应用', accent: '#7f8cff' },
  { code: 14, label: '文件管理器', category: '应用', accent: '#7f8cff' },
  { code: 15, label: '图片', category: '应用', accent: '#7f8cff' },
  { code: 16, label: '音乐', category: '应用', accent: '#7f8cff' },
] as const;

export const FUNCTION_SLOT_MACRO_STEP_TYPES = [
  { value: 0, label: 'Tap' },
  { value: 1, label: 'Key Down' },
  { value: 2, label: 'Key Up' },
  { value: 3, label: 'Delay' },
] as const;

export type FunctionSlotKeyOption = {
  label: string;
  usageId: number;
  group: string;
};

export type FunctionSlotMacroStepDraft = {
  id: string;
  type: number;
  usageId: number;
  modifiers: number;
  delayMs: number;
};

export type FunctionSlotDraft = {
  slotIndex: number;
  slotType: number;
  actionCode: number;
  flags: number;
  arg0: number;
  arg1: number;
  arg2: number;
  macroSteps: FunctionSlotMacroStepDraft[];
  helperProfileId?: number;
  helperPayload?: Record<string, unknown>;
};

export const FUNCTION_SLOT_KEY_OPTIONS: FunctionSlotKeyOption[] = [
  ...'ABCDEFGHIJKLMNOPQRSTUVWXYZ'.split('').map((label, index) => ({
    label,
    usageId: 0x04 + index,
    group: 'Letters',
  })),
  ...Array.from({ length: 10 }, (_, index) => ({
    label: `${index}`,
    usageId: 0x27 + ((index + 9) % 10),
    group: 'Numbers',
  })),
  ...Array.from({ length: 12 }, (_, index) => ({
    label: `F${index + 1}`,
    usageId: 0x3A + index,
    group: 'Function',
  })),
  { label: 'ESC', usageId: 0x29, group: 'Control' },
  { label: 'TAB', usageId: 0x2B, group: 'Control' },
  { label: 'ENTER', usageId: 0x28, group: 'Control' },
  { label: 'SPACE', usageId: 0x2C, group: 'Control' },
  { label: 'BSPC', usageId: 0x2A, group: 'Control' },
  { label: 'INSERT', usageId: 0x49, group: 'Navigation' },
  { label: 'DELETE', usageId: 0x4C, group: 'Navigation' },
  { label: 'HOME', usageId: 0x4A, group: 'Navigation' },
  { label: 'END', usageId: 0x4D, group: 'Navigation' },
  { label: 'PGUP', usageId: 0x4B, group: 'Navigation' },
  { label: 'PGDN', usageId: 0x4E, group: 'Navigation' },
  { label: 'LEFT', usageId: 0x50, group: 'Navigation' },
  { label: 'RIGHT', usageId: 0x4F, group: 'Navigation' },
  { label: 'UP', usageId: 0x52, group: 'Navigation' },
  { label: 'DOWN', usageId: 0x51, group: 'Navigation' },
  { label: 'PAUSE', usageId: 0x48, group: 'Navigation' },
  { label: '-', usageId: 0x2D, group: 'Symbols' },
  { label: '=', usageId: 0x2E, group: 'Symbols' },
  { label: '[', usageId: 0x2F, group: 'Symbols' },
  { label: ']', usageId: 0x30, group: 'Symbols' },
  { label: '\\', usageId: 0x31, group: 'Symbols' },
  { label: ';', usageId: 0x33, group: 'Symbols' },
  { label: '\'', usageId: 0x34, group: 'Symbols' },
  { label: '`', usageId: 0x35, group: 'Symbols' },
  { label: ',', usageId: 0x36, group: 'Symbols' },
  { label: '.', usageId: 0x37, group: 'Symbols' },
  { label: '/', usageId: 0x38, group: 'Symbols' },
];

export function cloneFunctionSlotDraft(slot: FunctionSlotDraft): FunctionSlotDraft {
  return JSON.parse(JSON.stringify(slot)) as FunctionSlotDraft;
}

export function createEmptyFunctionSlotDraft(slotIndex: number): FunctionSlotDraft {
  return {
    slotIndex,
    slotType: 0,
    actionCode: 0,
    flags: 0,
    arg0: 0,
    arg1: 0,
    arg2: 0,
    macroSteps: [],
  };
}

export function packMacroStep(step: FunctionSlotMacroStepDraft): number {
  return ((step.type & 0xff) |
    ((step.modifiers & 0xff) << 8) |
    ((step.usageId & 0xff) << 16) |
    ((Math.max(0, Math.min(255, step.delayMs)) & 0xff) << 24)) >>> 0;
}

export function unpackMacroStep(packed: number, fallbackId: string): FunctionSlotMacroStepDraft {
  return {
    id: fallbackId,
    type: packed & 0xff,
    modifiers: (packed >>> 8) & 0xff,
    usageId: (packed >>> 16) & 0xff,
    delayMs: (packed >>> 24) & 0xff,
  };
}

export function usageLabel(usageId: number): string {
  return FUNCTION_SLOT_KEY_OPTIONS.find((item) => item.usageId === usageId)?.label ?? `0x${usageId.toString(16).toUpperCase()}`;
}

export function formatModifierMask(mask: number): string {
  const labels = FUNCTION_SLOT_MODIFIERS
    .filter((item) => (mask & item.mask) !== 0)
    .map((item) => item.shortLabel);

  return labels.join('+');
}

export function describeFunctionSlot(slot: FunctionSlotDraft): string {
  switch (slot.slotType) {
    case 0:
      return '未配置';
    case 1:
      return FUNCTION_SLOT_HID_PRESETS.find((item) => item.code === slot.actionCode)?.label ?? 'HID 预设';
    case 2: {
      const mods = formatModifierMask(slot.arg0);
      return [mods, usageLabel(slot.actionCode)].filter(Boolean).join('+');
    }
    case 3:
      return `宏序列 (${slot.macroSteps.length})`;
    case 4:
      return slot.helperPayload?.displayName?.toString() || `Helper #${slot.actionCode}`;
    default:
      return '未知';
  }
}

export function slotTypeLabel(value: number): string {
  switch (value) {
    case 0:
      return '未配置';
    case 1:
      return 'HID 预设';
    case 2:
      return '组合键';
    case 3:
      return '宏序列';
    case 4:
      return '中枢动作';
    default:
      return '未知';
  }
}

export function slotTypeColor(value: number): string {
  switch (value) {
    case 1:
      return '#f5a623';
    case 2:
      return '#15a4ad';
    case 3:
      return '#7f8cff';
    case 4:
      return '#4cd964';
    default:
      return '#5f6b7a';
  }
}

export function protoSlotToDraft(config?: UsbComm.IFunctionSlotConfig): FunctionSlotDraft {
  const slotIndex = Number(config?.slotIndex ?? 0);
  return {
    slotIndex,
    slotType: Number(config?.slotType ?? 0),
    actionCode: Number(config?.actionCode ?? 0),
    flags: Number(config?.flags ?? 0),
    arg0: Number(config?.arg0 ?? 0),
    arg1: Number(config?.arg1 ?? 0),
    arg2: Number(config?.arg2 ?? 0),
    macroSteps: (config?.macroStepsPacked ?? []).map((packed, index) =>
      unpackMacroStep(Number(packed), `${slotIndex}-${index}-${Date.now()}`)),
    helperProfileId: Number(config?.arg0 ?? 0) || undefined,
  };
}

export function draftToProtoSlot(slot: FunctionSlotDraft): UsbComm.IFunctionSlotConfig {
  return {
    slotIndex: slot.slotIndex,
    slotType: slot.slotType,
    actionCode: slot.actionCode,
    flags: slot.flags,
    arg0: slot.arg0,
    arg1: slot.arg1,
    arg2: slot.arg2,
    macroStepsPacked: slot.macroSteps.map(packMacroStep),
  };
}

export function createMacroStep(type = 0): FunctionSlotMacroStepDraft {
  return {
    id: `${Date.now()}-${Math.random().toString(16).slice(2)}`,
    type,
    usageId: 0x04,
    modifiers: 0,
    delayMs: 40,
  };
}

