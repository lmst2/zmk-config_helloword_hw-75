<template>
  <a-space direction="vertical" :size="16" :class="$style.page">
    <a-card title="键盘总览" size="small" :class="$style.card">
      <div :class="$style.overview">
        <a-descriptions :column="1" size="small" :label-style="{ width: '12em' }">
          <a-descriptions-item label="Keymap 源文件">
            <code>{{ keyboardConfig.sourcePath }}</code>
          </a-descriptions-item>
          <a-descriptions-item label="层数">
            <code>{{ keyboardConfig.layers.length }}</code>
          </a-descriptions-item>
          <a-descriptions-item label="快捷功能槽位">
            <a-space wrap>
              <a-tag v-for="slot in slotSummaries" :key="slot.slotIndex" :color="slot.color">
                {{ slot.title }}
              </a-tag>
            </a-space>
          </a-descriptions-item>
          <a-descriptions-item label="Helper 状态">
            <a-tag :color="helperAvailable ? 'green' : 'orange'">
              {{ helperAvailable ? '已连接' : '未连接' }}
            </a-tag>
            <span v-if="functionSlotStore.helperError" :class="$style.helperHint">{{ functionSlotStore.helperError }}</span>
          </a-descriptions-item>
        </a-descriptions>

        <a-space wrap :class="$style.actions">
          <a-button @click="copySource">复制源文件</a-button>
          <a-button @click="downloadSource">导出 keymap</a-button>
          <a-button @click="functionSlotStore.refresh">刷新槽位</a-button>
          <a-button :disabled="!helperAvailable" @click="exportProfiles">导出 Helper 配置</a-button>
          <a-button :disabled="!helperAvailable" @click="triggerProfileImport">导入 Helper 配置</a-button>
          <a-button @click="functionSlotStore.resetDraft">撤销草稿</a-button>
          <a-button type="primary" :loading="functionSlotStore.isApplying" @click="applyDraft">
            应用到键盘
          </a-button>
        </a-space>
        <input
          ref="profileImportInput"
          type="file"
          accept="application/json"
          :class="$style.hiddenInput"
          @change="onProfileImportChange"
        />
      </div>
    </a-card>

    <div :class="$style.workspace">
      <a-card title="Keymap 视图" size="small" :class="$style.card">
        <a-tabs v-model:activeKey="activeLayerId">
          <a-tab-pane v-for="layer in keyboardConfig.layers" :key="layer.id" :tab="layer.label">
            <div :class="$style.keyboardFrame">
              <div :class="$style.keyboardHint">整块键盘仅用于查看，只有右侧 5 个发光槽位可以替换。</div>
              <div :class="$style.layer">
              <div v-for="rowEntry in visibleRows(layer.rows)" :key="`${layer.id}-${rowEntry.rowIndex}`" :class="$style.row">
                <button
                  v-for="(binding, bindingIndex) in rowEntry.row"
                  :key="`${layer.id}-${rowEntry.rowIndex}-${bindingIndex}`"
                  type="button"
                  :draggable="!!slotMetaByPosition[`${rowEntry.rowIndex}-${bindingIndex}`]"
                  :class="[
                    $style.key,
                    slotMetaByPosition[`${rowEntry.rowIndex}-${bindingIndex}`] && $style.slotKey,
                    selectedSlotIndex === slotMetaByPosition[`${rowEntry.rowIndex}-${bindingIndex}`]?.slotIndex && $style.slotKeySelected,
                  ]"
                  :style="keyStyle(rowEntry.rowIndex, bindingIndex)"
                  @click="selectSlotByPosition(rowEntry.rowIndex, bindingIndex)"
                  @dragstart="onSlotDragStart(slotMetaByPosition[`${rowEntry.rowIndex}-${bindingIndex}`]?.slotIndex, $event)"
                  @dragover.prevent="onSlotDragOver(slotMetaByPosition[`${rowEntry.rowIndex}-${bindingIndex}`]?.slotIndex, $event)"
                  @drop.prevent="onSlotDrop(slotMetaByPosition[`${rowEntry.rowIndex}-${bindingIndex}`]?.slotIndex, $event)"
                >
                  <div v-if="slotMetaByPosition[`${rowEntry.rowIndex}-${bindingIndex}`]" :class="$style.slotBadge">
                    槽位 {{ slotMetaByPosition[`${rowEntry.rowIndex}-${bindingIndex}`]?.slotIndex! + 1 }}
                  </div>
                  <div :class="[$style.keyLegend, keyPrimaryLabel(rowEntry.rowIndex, bindingIndex, binding).length >= 7 && $style.keyLegendSmall]">
                    {{ keyPrimaryLabel(rowEntry.rowIndex, bindingIndex, binding) }}
                  </div>
                  <div v-if="keySecondaryText(rowEntry.rowIndex, bindingIndex, binding)" :class="$style.keyRaw">
                    {{ keySecondaryText(rowEntry.rowIndex, bindingIndex, binding) }}
                  </div>
                </button>
              </div>
              </div>
            </div>
          </a-tab-pane>
        </a-tabs>
      </a-card>

      <div :class="$style.sidebar">
        <a-card title="Helper 管理" size="small" :class="$style.card">
          <a-descriptions :column="1" size="small">
            <a-descriptions-item label="状态">
              <a-space wrap>
                <a-tag :color="helperAvailable ? 'green' : 'orange'">
                  {{ helperAvailable ? '已连接' : '未连接' }}
                </a-tag>
                <a-tag :color="!helperAvailable ? 'default' : (helperVersionMatches ? 'blue' : 'red')">
                  {{ !helperAvailable ? '未校验' : (helperVersionMatches ? '版本匹配' : '版本不匹配') }}
                </a-tag>
              </a-space>
            </a-descriptions-item>
            <a-descriptions-item label="当前版本">
              <code>{{ helperVersion ?? '未知' }}</code>
            </a-descriptions-item>
            <a-descriptions-item label="期望版本">
              <code>{{ helperExpectedVersion }}</code>
            </a-descriptions-item>
            <a-descriptions-item v-if="functionSlotStore.helperError" label="说明">
              <span :class="$style.helperHint">{{ functionSlotStore.helperError }}</span>
            </a-descriptions-item>
          </a-descriptions>
          <a-space wrap :class="$style.helperManagerActions">
            <a-button size="small" @click="functionSlotStore.refreshHelperCatalog()">刷新状态</a-button>
            <a-button size="small" :disabled="!helperAvailable || helperRestarting" :loading="helperRestarting" @click="restartHelperService">重启 Helper</a-button>
          </a-space>
        </a-card>

        <a-card title="快捷功能键编辑器" size="small" :class="$style.card">
          <template v-if="selectedSlot">
            <div :class="$style.editorHeader">
              <div>
                <div :class="$style.editorTitle">槽位 {{ selectedSlot.slotIndex + 1 }}</div>
                <div :class="$style.editorMeta">{{ selectedSlotActionLabel }}</div>
                <div :class="$style.editorSubMeta">物理键位 {{ selectedSlotPhysicalLabel }}</div>
              </div>
              <a-tag :color="selectedSlotColor">{{ selectedSlotTypeLabel }}</a-tag>
            </div>

            <a-form layout="vertical">
              <a-form-item label="动作类型">
                <a-segmented
                  :value="selectedSlotMode"
                  :options="slotTypeOptions"
                  @update:value="updateSelectedSlotType"
                />
              </a-form-item>
            </a-form>

            <template v-if="isSingleKeySlot(selectedSlot)">
              <div :class="$style.sectionTitle">标准功能键</div>
              <div :class="$style.quickKeyGrid">
                <button
                  v-for="option in singleKeyQuickOptions"
                  :key="`single-${option.usageId}`"
                  type="button"
                  :class="[$style.choiceKey, selectedSingleKeyUsage === option.usageId && $style.choiceKeySelected]"
                  @click="applySingleKeyUsage(option.usageId)"
                >
                  {{ option.label }}
                </button>
              </div>

              <div :class="$style.sectionTitle">其它标准键</div>
              <a-select
                :value="selectedSingleKeyUsage"
                show-search
                :options="comboKeyOptions"
                style="width: 100%"
                option-filter-prop="label"
                placeholder="选择一个标准键"
                @update:value="applySingleKeyUsage(Number($event))"
              />

              <div :class="$style.sectionTitle">HID 功能键</div>
              <div :class="$style.paletteGrid">
                <button
                  v-for="preset in hidPresets"
                  :key="preset.code"
                  type="button"
                  draggable="true"
                  :class="[$style.paletteCard, selectedSlot.actionCode === preset.code && $style.paletteCardSelected]"
                  :style="{ '--accent': preset.accent }"
                  @click="applyHidPreset(preset.code)"
                  @dragstart="onPresetDragStart({ kind: 'hid', code: preset.code }, $event)"
                >
                  <strong>{{ preset.label }}</strong>
                  <span>{{ preset.category }}</span>
                </button>
              </div>
            </template>

            <template v-else-if="selectedSlot.slotType === UsbComm.FunctionSlotType.FUNCTION_SLOT_KEY_COMBO">
              <div :class="$style.sectionTitle">组合键</div>
              <a-space wrap>
                <a-tag
                  v-for="modifier in modifierOptions"
                  :key="modifier.mask"
                  :color="(selectedSlot.arg0 & modifier.mask) !== 0 ? 'cyan' : 'default'"
                  :class="$style.toggleTag"
                  @click="toggleModifier(modifier.mask)"
                >
                  {{ modifier.label }}
                </a-tag>
              </a-space>
              <div :class="$style.keyPicker">
                <button
                  v-for="option in comboKeys"
                  :key="option.usageId"
                  type="button"
                  :class="[$style.choiceKey, selectedSlot.actionCode === option.usageId && $style.choiceKeySelected]"
                  @click="setComboKey(option.usageId)"
                >
                  {{ option.label }}
                </button>
              </div>
            </template>

            <template v-else-if="selectedSlot.slotType === UsbComm.FunctionSlotType.FUNCTION_SLOT_MACRO_SEQ">
              <div :class="$style.sectionTitle">宏序列</div>
              <div :class="$style.macroList">
                <div
                  v-for="(step, index) in selectedSlot.macroSteps"
                  :key="step.id"
                  draggable="true"
                  :class="[$style.macroStep, draggedMacroStepIndex === index && $style.macroStepDragging]"
                  @dragstart="onMacroStepDragStart(index, $event)"
                  @dragover.prevent
                  @drop.prevent="onMacroStepDrop(index)"
                >
                  <a-select
                    :value="step.type"
                    :options="macroStepTypeOptions"
                    style="width: 120px"
                    @update:value="updateMacroStep(index, 'type', Number($event))"
                  />
                  <template v-if="step.type !== 3">
                    <a-select
                      :value="step.usageId"
                      show-search
                      :options="comboKeyOptions"
                      style="width: 140px"
                      option-filter-prop="label"
                      @update:value="updateMacroStep(index, 'usageId', Number($event))"
                    />
                    <a-select
                      mode="multiple"
                      :value="selectedModifierMasks(step.modifiers)"
                      :options="modifierSelectOptions"
                      style="width: 180px"
                      @update:value="updateMacroStepModifiers(index, $event)"
                    />
                  </template>
                  <template v-else>
                    <a-input-number
                      :value="step.delayMs"
                      :min="1"
                      :max="255"
                      style="width: 120px"
                      @update:value="updateMacroStep(index, 'delayMs', Number($event || 0))"
                    />
                  </template>
                  <a-button danger ghost @click="removeMacroStep(index)">删除</a-button>
                </div>
              </div>
              <a-space wrap>
                <a-button @click="addMacroStep(0)">加点按</a-button>
                <a-button @click="addMacroStep(1)">加按下</a-button>
                <a-button @click="addMacroStep(2)">加抬起</a-button>
                <a-button @click="addMacroStep(3)">加延时</a-button>
              </a-space>
            </template>

            <template v-else-if="selectedSlot.slotType === UsbComm.FunctionSlotType.FUNCTION_SLOT_HELPER_ACTION">
              <a-alert
                v-if="!helperAvailable"
                type="warning"
                show-icon
                message="本地 helper 未连接"
                description="Helper 动作需要先启动 tools/hw75-helper。"
                :class="$style.helperAlert"
              />
              <div :class="$style.sectionTitle">Helper 动作</div>
              <div :class="$style.paletteGrid">
                <button
                  v-for="action in helperCatalog"
                  :key="action.code"
                  type="button"
                  draggable="true"
                  :class="[$style.paletteCard, selectedSlot.actionCode === action.code && $style.paletteCardSelected]"
                  :style="{ '--accent': '#4cd964' }"
                  @click="selectHelperAction(action.code)"
                  @dragstart="onPresetDragStart({ kind: 'helper', code: action.code }, $event)"
                >
                  <strong>{{ action.displayName }}</strong>
                  <span>{{ action.category }}</span>
                </button>
              </div>
              <div v-if="selectedHelperAction" :class="$style.helperForm">
                <div v-for="field in selectedHelperAction.schema" :key="field.key" :class="$style.helperField">
                  <label>{{ field.label }}</label>
                  <a-input
                    :value="String(selectedSlot.helperPayload?.[field.key] ?? '')"
                    :placeholder="field.placeholder"
                    @update:value="updateHelperPayload(field.key, $event)"
                  />
                </div>
              </div>
            </template>
          </template>
          <div v-else :class="$style.placeholder">点击右侧高亮的 5 个槽位键开始编辑。</div>
        </a-card>

        <a-card title="拖拽面板" size="small" :class="$style.card">
          <div :class="$style.dragHint">
            预设卡片可直接拖到发光槽位键上。槽位拖到槽位默认交换，按住 Ctrl 再放下会复制。
          </div>
          <div :class="$style.paletteGrid">
            <button
              v-for="preset in hidPresets.slice(0, 6)"
              :key="`mini-${preset.code}`"
              type="button"
              draggable="true"
              :class="$style.paletteCard"
              :style="{ '--accent': preset.accent }"
              @dragstart="onPresetDragStart({ kind: 'hid', code: preset.code }, $event)"
            >
              <strong>{{ preset.label }}</strong>
              <span>{{ preset.category }}</span>
            </button>
          </div>
        </a-card>

        <a-card size="small" :class="$style.card">
          <template #title>Helper 最近执行记录</template>
          <template #extra>
            <a-button size="small" @click="functionSlotStore.clearHelperActivity()">清空</a-button>
          </template>
          <div v-if="helperActivity.length" :class="$style.activityList">
            <div v-for="entry in helperActivity" :key="`${entry.seq}-${entry.createdAt}`" :class="$style.activityItem">
              <div :class="$style.activityHeader">
                <strong>槽位 {{ entry.slotIndex + 1 }}</strong>
                <a-tag :color="entry.ok ? 'green' : 'red'">{{ entry.ok ? '成功' : '失败' }}</a-tag>
              </div>
              <div :class="$style.activityBody">
                {{ entry.summary }}
              </div>
              <div v-if="entry.error" :class="$style.activityError">{{ entry.error }}</div>
            </div>
          </div>
          <div v-else :class="$style.placeholder">还没有 Helper 执行记录。</div>
        </a-card>
      </div>
    </div>
  </a-space>
</template>

<script lang="ts" setup>
import { computed, onMounted, ref } from 'vue';
import { storeToRefs } from 'pinia';
import { message } from 'ant-design-vue';

import { keyboardConfig } from '@/generated/keyboard-config';
import { UsbComm } from '@/proto/comm.proto';
import { useFunctionSlotStore } from '@/stores/function-slots';
import { exportHelperProfiles, importHelperProfiles } from '@/utils/helper-bridge';
import {
  FUNCTION_SLOT_HID_PRESETS,
  FUNCTION_SLOT_KEYS,
  FUNCTION_SLOT_KEY_OPTIONS,
  FUNCTION_SLOT_MACRO_STEP_TYPES,
  FUNCTION_SLOT_MODIFIERS,
  cloneFunctionSlotDraft,
  createMacroStep,
  createEmptyFunctionSlotDraft,
  describeFunctionSlot,
  formatModifierMask,
  slotTypeColor,
  usageLabel,
  type FunctionSlotDraft,
} from '@/utils/function-slots';

const functionSlotStore = useFunctionSlotStore();
const {
  deviceSlots,
  draftSlots,
  helperAvailable,
  helperVersion,
  helperExpectedVersion,
  helperVersionMatches,
  helperCatalog,
  helperActivity,
  helperRestarting,
} = storeToRefs(functionSlotStore);

const activeLayerId = ref(keyboardConfig.layers[0]?.id ?? 'base');
const selectedSlotIndex = ref(0);
const draggedMacroStepIndex = ref<number>();
const profileImportInput = ref<HTMLInputElement>();

const slotTypeOptions = [
  { label: '未配置', value: 'none' },
  { label: '单按键功能', value: 'single' },
  { label: '组合键', value: 'combo' },
  { label: '宏序列', value: 'macro' },
  { label: 'Helper 动作', value: 'helper' },
] as const;

const modifierOptions = FUNCTION_SLOT_MODIFIERS;
const hidPresets = FUNCTION_SLOT_HID_PRESETS;
const macroStepTypeOptions = FUNCTION_SLOT_MACRO_STEP_TYPES.map((item) => ({ label: item.label, value: item.value }));
const comboKeys = FUNCTION_SLOT_KEY_OPTIONS;
const comboKeyOptions = FUNCTION_SLOT_KEY_OPTIONS.map((item) => ({ label: item.label, value: item.usageId }));
const modifierSelectOptions = FUNCTION_SLOT_MODIFIERS.map((item) => ({ label: item.label, value: item.mask }));
const singleKeyQuickOptions = FUNCTION_SLOT_KEY_OPTIONS.filter((item) =>
  ['DELETE', 'INSERT', 'HOME', 'END', 'PGUP', 'PGDN', 'PAUSE', 'UP', 'DOWN', 'LEFT', 'RIGHT', 'ENTER', 'BSPC', 'TAB', 'SPACE']
    .includes(item.label));

const slotMetaByPosition = Object.fromEntries(FUNCTION_SLOT_KEYS.map((item) => [`${item.rowIndex}-${item.bindingIndex}`, item]));

const selectedSlot = computed<FunctionSlotDraft | undefined>(() =>
  draftSlots.value.find((slot) => slot.slotIndex === selectedSlotIndex.value));
const selectedSlotPhysicalLabel = computed(() =>
  FUNCTION_SLOT_KEYS.find((item) => item.slotIndex === selectedSlotIndex.value)?.label ?? '功能槽位');
const selectedSlotActionLabel = computed(() =>
  slotActionText(selectedSlot.value, selectedSlotPhysicalLabel.value));
const selectedSlotMode = computed(() => slotEditorMode(selectedSlot.value));
const selectedSlotColor = computed(() => slotAccentColor(selectedSlot.value));
const selectedSlotTypeLabel = computed(() => slotEditorModeLabel(selectedSlot.value));
const selectedHelperAction = computed(() =>
  helperCatalog.value.find((item) => item.code === selectedSlot.value?.actionCode));
const selectedSingleKeyUsage = computed(() =>
  selectedSlot.value && selectedSlot.value.slotType === UsbComm.FunctionSlotType.FUNCTION_SLOT_KEY_COMBO && selectedSlot.value.arg0 === 0
    ? selectedSlot.value.actionCode
    : undefined);
const slotSummaries = computed(() =>
  FUNCTION_SLOT_KEYS.map((item) => ({
    slotIndex: item.slotIndex,
    title: `S${item.slotIndex + 1} ${slotSummaryText(item.slotIndex)}`,
    color: slotAccentColor(draftSlots.value.find((slot) => slot.slotIndex === item.slotIndex)),
  })));

onMounted(async () => {
  if (!draftSlots.value.length) {
    await functionSlotStore.refresh();
  }
});

function slotSummaryText(slotIndex: number): string {
  return describeFunctionSlot(displaySlot(slotIndex) ?? createEmptyFunctionSlotDraft(slotIndex));
}

function isSingleKeySlot(slot?: FunctionSlotDraft): boolean {
  if (!slot) {
    return false;
  }

  return slot.slotType === UsbComm.FunctionSlotType.FUNCTION_SLOT_HID_PRESET ||
    (slot.slotType === UsbComm.FunctionSlotType.FUNCTION_SLOT_KEY_COMBO && slot.arg0 === 0);
}

function slotEditorMode(slot?: FunctionSlotDraft): 'none' | 'single' | 'combo' | 'macro' | 'helper' {
  if (!slot || slot.slotType === UsbComm.FunctionSlotType.FUNCTION_SLOT_NONE) {
    return 'none';
  }
  if (isSingleKeySlot(slot)) {
    return 'single';
  }
  if (slot.slotType === UsbComm.FunctionSlotType.FUNCTION_SLOT_KEY_COMBO) {
    return 'combo';
  }
  if (slot.slotType === UsbComm.FunctionSlotType.FUNCTION_SLOT_MACRO_SEQ) {
    return 'macro';
  }
  return 'helper';
}

function slotEditorModeLabel(slot?: FunctionSlotDraft): string {
  switch (slotEditorMode(slot)) {
    case 'none':
      return '未配置';
    case 'single':
      return '单按键功能';
    case 'combo':
      return '组合键';
    case 'macro':
      return '宏序列';
    case 'helper':
      return 'Helper 动作';
  }
}

function slotAccentColor(slot?: FunctionSlotDraft): string {
  if (!slot) {
    return slotTypeColor(0);
  }

  if (isSingleKeySlot(slot)) {
    return '#f5a623';
  }

  return slotTypeColor(slot.slotType);
}

function deviceSlot(slotIndex: number): FunctionSlotDraft | undefined {
  return deviceSlots.value.find((slot) => slot.slotIndex === slotIndex);
}

function draftSlot(slotIndex: number): FunctionSlotDraft | undefined {
  return draftSlots.value.find((slot) => slot.slotIndex === slotIndex);
}

function displaySlot(slotIndex: number): FunctionSlotDraft | undefined {
  return draftSlot(slotIndex) ?? deviceSlot(slotIndex);
}

function visibleRows(rows: { raw: string; label: string }[][]): Array<{ row: { raw: string; label: string }[]; rowIndex: number }> {
  return rows
    .map((row, rowIndex) => ({ row, rowIndex }))
    .filter(({ row }) => row.some((binding) => binding.raw !== '&none'));
}

function keyUnits(rowIndex: number, bindingIndex: number): number {
  return Number(keyboardConfig.geometry?.rowUnits?.[rowIndex]?.[bindingIndex] ?? 1);
}

function keyGapBefore(rowIndex: number, bindingIndex: number): number {
  return Number(keyboardConfig.geometry?.rowGapBefore?.[rowIndex]?.[bindingIndex] ?? 0);
}

function keyStyle(rowIndex: number, bindingIndex: number): Record<string, string> {
  const slotIndex = slotMetaByPosition[`${rowIndex}-${bindingIndex}`]?.slotIndex;
  if (slotIndex === undefined) {
    return {
      '--key-units': `${keyUnits(rowIndex, bindingIndex)}`,
      '--key-gap-before': `${keyGapBefore(rowIndex, bindingIndex)}`,
    };
  }
  return {
    '--key-units': `${keyUnits(rowIndex, bindingIndex)}`,
    '--key-gap-before': `${keyGapBefore(rowIndex, bindingIndex)}`,
    '--slot-accent': slotAccentColor(displaySlot(slotIndex)),
  };
}

function keyPrimaryLabel(rowIndex: number, bindingIndex: number, binding: { raw: string; label: string }): string {
  const slotMeta = slotMetaByPosition[`${rowIndex}-${bindingIndex}`];
  if (slotMeta) {
    return slotActionText(displaySlot(slotMeta.slotIndex), slotMeta.label);
  }

  const raw = binding.raw.trim();
  const kpMap: Record<string, string> = {
    ESC: 'ESC',
    F1: 'F1',
    F2: 'F2',
    F3: 'F3',
    F4: 'F4',
    F5: 'F5',
    F6: 'F6',
    F7: 'F7',
    F8: 'F8',
    F9: 'F9',
    F10: 'F10',
    F11: 'F11',
    F12: 'F12',
    GRAVE: '`',
    N1: '1',
    N2: '2',
    N3: '3',
    N4: '4',
    N5: '5',
    N6: '6',
    N7: '7',
    N8: '8',
    N9: '9',
    N0: '0',
    MINUS: '-',
    EQUAL: '=',
    LBKT: '[',
    RBKT: ']',
    BSLH: '\\',
    SEMI: ';',
    SQT: '\'',
    COMMA: ',',
    DOT: '.',
    FSLH: '/',
    BSPC: 'Backspace',
    TAB: 'Tab',
    CLCK: 'Caps',
    RET: 'Enter',
    LSHFT: 'Shift',
    RSHFT: 'Shift',
    LCTRL: 'Ctrl',
    RCTRL: 'Ctrl',
    LGUI: 'Win',
    LALT: 'Alt',
    RALT: 'Alt',
    SPACE: 'Space',
    LEFT: 'Left',
    RIGHT: 'Right',
    UP: 'Up',
    DOWN: 'Down',
  };

  if (raw.startsWith('&kp ')) {
    const code = raw.slice(4).trim();
    return kpMap[code] ?? code;
  }

  if (raw.startsWith('&mo ')) {
    return raw.slice(4).trim();
  }

  if (raw === '&trans') {
    return 'TRNS';
  }

  if (raw === '&none') {
    return '';
  }

  if (raw.startsWith('&rgb_ug ')) {
    return 'RGB';
  }

  if (raw === '&tb_mode') {
    return 'TouchBar';
  }

  return binding.label;
}

function keySecondaryText(rowIndex: number, bindingIndex: number, binding: { raw: string; label: string }): string {
  const slotMeta = slotMetaByPosition[`${rowIndex}-${bindingIndex}`];
  if (slotMeta) {
    return `物理 ${slotMeta.label}`;
  }

  const raw = binding.raw.trim();
  if (raw === '&trans' || raw === '&none') {
    return '';
  }

  if (raw.startsWith('&rgb_ug ')) {
    return raw.replace('&rgb_ug ', '').replaceAll('_', ' ');
  }

  if (raw.startsWith('&mo ')) {
    return `切到 ${raw.slice(4).trim()}`;
  }

  if (raw === '&tb_mode') {
    return '切换 TouchBar 模式';
  }

  return '';
}

function slotActionText(slot: FunctionSlotDraft | undefined, fallbackLabel: string): string {
  if (!slot || slot.slotType === UsbComm.FunctionSlotType.FUNCTION_SLOT_NONE) {
    return '未配置';
  }

  if (slot.slotType === UsbComm.FunctionSlotType.FUNCTION_SLOT_HID_PRESET) {
    return hidPresets.find((item) => item.code === slot.actionCode)?.label ?? fallbackLabel;
  }

  if (slot.slotType === UsbComm.FunctionSlotType.FUNCTION_SLOT_KEY_COMBO) {
    const mods = formatModifierMask(slot.arg0);
    const key = usageLabel(slot.actionCode);
    return [mods, key].filter(Boolean).join('+') || fallbackLabel;
  }

  if (slot.slotType === UsbComm.FunctionSlotType.FUNCTION_SLOT_MACRO_SEQ) {
    return slot.macroSteps.length ? `宏 ${slot.macroSteps.length}` : '宏';
  }

  if (slot.slotType === UsbComm.FunctionSlotType.FUNCTION_SLOT_HELPER_ACTION) {
    return selectedHelperActionLabel(slot);
  }

  return fallbackLabel;
}

function selectedHelperActionLabel(slot: FunctionSlotDraft): string {
  const payloadName = slot.helperPayload?.displayName?.toString().trim();
  if (payloadName) {
    return payloadName;
  }

  return helperCatalog.value.find((item) => item.code === slot.actionCode)?.displayName ?? `Helper ${slot.actionCode}`;
}

function selectSlotByPosition(rowIndex: number, bindingIndex: number): void {
  const meta = slotMetaByPosition[`${rowIndex}-${bindingIndex}`];
  if (meta) {
    selectedSlotIndex.value = meta.slotIndex;
  }
}

function updateSelectedSlotType(value: string | number): void {
  if (!selectedSlot.value) {
    return;
  }

  functionSlotStore.setDraftSlot(selectedSlot.value.slotIndex, (slot) => {
    const next = { ...slot };
    switch (String(value)) {
      case 'none':
        next.slotType = UsbComm.FunctionSlotType.FUNCTION_SLOT_NONE;
        return next;
      case 'single':
        if (slot.slotType === UsbComm.FunctionSlotType.FUNCTION_SLOT_HID_PRESET) {
          return next;
        }
        next.slotType = UsbComm.FunctionSlotType.FUNCTION_SLOT_KEY_COMBO;
        next.arg0 = 0;
        next.actionCode = slot.actionCode || FUNCTION_SLOT_KEY_OPTIONS.find((item) => item.label === 'DELETE')?.usageId || 0x4c;
        next.helperPayload = undefined;
        next.macroSteps = [];
        return next;
      case 'combo':
        next.slotType = UsbComm.FunctionSlotType.FUNCTION_SLOT_KEY_COMBO;
        next.arg0 = slot.arg0 || 0x01;
        next.actionCode = slot.actionCode || 0x04;
        next.helperPayload = undefined;
        next.macroSteps = [];
        return next;
      case 'macro':
        next.slotType = UsbComm.FunctionSlotType.FUNCTION_SLOT_MACRO_SEQ;
        next.macroSteps = slot.macroSteps.length ? slot.macroSteps : [createMacroStep()];
        next.helperPayload = undefined;
        return next;
      case 'helper':
        next.slotType = UsbComm.FunctionSlotType.FUNCTION_SLOT_HELPER_ACTION;
        next.helperPayload = slot.helperPayload ?? {};
        next.macroSteps = [];
        return next;
      default:
        return next;
    }
  });
}

function applyHidPreset(code: number): void {
  if (!selectedSlot.value) {
    return;
  }
  functionSlotStore.setDraftSlot(selectedSlot.value.slotIndex, (slot) => ({
    ...slot,
    slotType: UsbComm.FunctionSlotType.FUNCTION_SLOT_HID_PRESET,
    actionCode: code,
    arg0: 0,
    arg1: 0,
    arg2: 0,
  }));
}

function applySingleKeyUsage(usageId: number): void {
  if (!selectedSlot.value || !usageId) {
    return;
  }

  functionSlotStore.setDraftSlot(selectedSlot.value.slotIndex, (slot) => ({
    ...slot,
    slotType: UsbComm.FunctionSlotType.FUNCTION_SLOT_KEY_COMBO,
    actionCode: usageId,
    arg0: 0,
    arg1: 0,
    arg2: 0,
  }));
}

function toggleModifier(mask: number): void {
  if (!selectedSlot.value) {
    return;
  }
  functionSlotStore.setDraftSlot(selectedSlot.value.slotIndex, (slot) => ({
    ...slot,
    slotType: UsbComm.FunctionSlotType.FUNCTION_SLOT_KEY_COMBO,
    arg0: (slot.arg0 & mask) !== 0 ? (slot.arg0 & ~mask) : (slot.arg0 | mask),
  }));
}

function setComboKey(usageId: number): void {
  if (!selectedSlot.value) {
    return;
  }
  functionSlotStore.setDraftSlot(selectedSlot.value.slotIndex, (slot) => ({
    ...slot,
    slotType: UsbComm.FunctionSlotType.FUNCTION_SLOT_KEY_COMBO,
    actionCode: usageId,
  }));
}

function addMacroStep(type: number): void {
  if (!selectedSlot.value) {
    return;
  }
  functionSlotStore.setDraftSlot(selectedSlot.value.slotIndex, (slot) => ({
    ...slot,
    slotType: UsbComm.FunctionSlotType.FUNCTION_SLOT_MACRO_SEQ,
    macroSteps: [...slot.macroSteps, createMacroStep(type)],
  }));
}

function updateMacroStep(index: number, key: 'type' | 'usageId' | 'delayMs', value: number): void {
  if (!selectedSlot.value) {
    return;
  }
  functionSlotStore.setDraftSlot(selectedSlot.value.slotIndex, (slot) => {
    const next = cloneFunctionSlotDraft(slot);
    next.macroSteps[index] = { ...next.macroSteps[index], [key]: value };
    return next;
  });
}

function updateMacroStepModifiers(index: number, masks: number[]): void {
  if (!selectedSlot.value) {
    return;
  }
  const combined = masks.reduce((acc, value) => acc | Number(value), 0);
  functionSlotStore.setDraftSlot(selectedSlot.value.slotIndex, (slot) => {
    const next = cloneFunctionSlotDraft(slot);
    next.macroSteps[index] = { ...next.macroSteps[index], modifiers: combined };
    return next;
  });
}

function removeMacroStep(index: number): void {
  if (!selectedSlot.value) {
    return;
  }
  functionSlotStore.setDraftSlot(selectedSlot.value.slotIndex, (slot) => ({
    ...slot,
    macroSteps: slot.macroSteps.filter((_, currentIndex) => currentIndex !== index),
  }));
}

function selectedModifierMasks(mask: number): number[] {
  return modifierOptions.filter((item) => (mask & item.mask) !== 0).map((item) => item.mask);
}

function selectHelperAction(code: number): void {
  if (!selectedSlot.value) {
    return;
  }
  const action = helperCatalog.value.find((item) => item.code === code);
  functionSlotStore.setDraftSlot(selectedSlot.value.slotIndex, (slot) => ({
    ...slot,
    slotType: UsbComm.FunctionSlotType.FUNCTION_SLOT_HELPER_ACTION,
    actionCode: code,
    helperPayload: {
      ...(slot.helperPayload ?? {}),
      displayName: action?.displayName ?? 'Helper 动作',
    },
  }));
}

function updateHelperPayload(key: string, value: string): void {
  if (!selectedSlot.value) {
    return;
  }
  functionSlotStore.setDraftSlot(selectedSlot.value.slotIndex, (slot) => ({
    ...slot,
    helperPayload: {
      ...(slot.helperPayload ?? {}),
      [key]: value,
      displayName: selectedHelperAction.value?.displayName ?? slot.helperPayload?.displayName ?? 'Helper 动作',
    },
  }));
}

function onPresetDragStart(payload: { kind: 'hid' | 'helper'; code: number }, event: DragEvent): void {
  if (event.dataTransfer) {
    event.dataTransfer.effectAllowed = 'copy';
  }
  event.dataTransfer?.setData('application/x-hw75-slot', JSON.stringify(payload));
}

function onSlotDragStart(slotIndex: number | undefined, event: DragEvent): void {
  if (slotIndex === undefined) {
    return;
  }
  if (event.dataTransfer) {
    event.dataTransfer.effectAllowed = 'copyMove';
  }
  event.dataTransfer?.setData('application/x-hw75-slot', JSON.stringify({ kind: 'slot', slotIndex }));
}

function onSlotDragOver(slotIndex: number | undefined, event: DragEvent): void {
  if (slotIndex !== undefined) {
    selectedSlotIndex.value = slotIndex;
  }
  const raw = event.dataTransfer?.getData('application/x-hw75-slot');
  if (!raw || !event.dataTransfer) {
    return;
  }
  const payload = JSON.parse(raw) as { kind: 'slot' | 'hid' | 'helper' };
  event.dataTransfer.dropEffect = payload.kind === 'slot' && (event.ctrlKey || event.altKey) ? 'copy' : 'move';
}

function onSlotDrop(slotIndex: number | undefined, event: DragEvent): void {
  if (slotIndex === undefined) {
    return;
  }
  const raw = event.dataTransfer?.getData('application/x-hw75-slot');
  if (!raw) {
    return;
  }
  const payload = JSON.parse(raw) as { kind: 'slot' | 'hid' | 'helper'; slotIndex?: number; code?: number };
  if (payload.kind === 'slot' && payload.slotIndex !== undefined) {
    if ((event.ctrlKey || event.altKey) && payload.slotIndex !== slotIndex) {
      functionSlotStore.cloneSlotToTarget(payload.slotIndex, slotIndex);
    } else {
      functionSlotStore.swapDraftSlots(payload.slotIndex, slotIndex);
    }
    selectedSlotIndex.value = slotIndex;
    return;
  }
  if (payload.kind === 'hid' && payload.code !== undefined) {
    functionSlotStore.setDraftSlot(slotIndex, (slot) => ({
      ...slot,
      slotType: UsbComm.FunctionSlotType.FUNCTION_SLOT_HID_PRESET,
      actionCode: payload.code,
    }));
    selectedSlotIndex.value = slotIndex;
    return;
  }
  if (payload.kind === 'helper' && payload.code !== undefined) {
    const action = helperCatalog.value.find((item) => item.code === payload.code);
    functionSlotStore.setDraftSlot(slotIndex, (slot) => ({
      ...slot,
      slotType: UsbComm.FunctionSlotType.FUNCTION_SLOT_HELPER_ACTION,
      actionCode: payload.code,
      helperPayload: {
        ...(slot.helperPayload ?? {}),
        displayName: action?.displayName ?? 'Helper 动作',
      },
    }));
    selectedSlotIndex.value = slotIndex;
  }
}

function onMacroStepDragStart(index: number, event: DragEvent): void {
  draggedMacroStepIndex.value = index;
  event.dataTransfer?.setData('text/plain', String(index));
}

function onMacroStepDrop(targetIndex: number): void {
  if (draggedMacroStepIndex.value === undefined || !selectedSlot.value) {
    return;
  }
  const sourceIndex = draggedMacroStepIndex.value;
  draggedMacroStepIndex.value = undefined;
  if (sourceIndex === targetIndex) {
    return;
  }
  functionSlotStore.setDraftSlot(selectedSlot.value.slotIndex, (slot) => {
    const next = cloneFunctionSlotDraft(slot);
    const [step] = next.macroSteps.splice(sourceIndex, 1);
    next.macroSteps.splice(targetIndex, 0, step);
    return next;
  });
}

async function copySource(): Promise<void> {
  await navigator.clipboard.writeText(keyboardConfig.sourceText);
  message.success('已复制 keymap 源文件');
}

function downloadSource(): void {
  const blob = new Blob([keyboardConfig.sourceText], { type: 'text/plain;charset=utf-8' });
  const url = URL.createObjectURL(blob);
  const anchor = document.createElement('a');
  anchor.href = url;
  anchor.download = 'hw75_keyboard.keymap';
  anchor.click();
  URL.revokeObjectURL(url);
}

async function applyDraft(): Promise<void> {
  await functionSlotStore.applyDraft();
  message.success('快捷功能槽位已同步到键盘');
}

async function restartHelperService(): Promise<void> {
  try {
    await functionSlotStore.restartLocalHelper();
    message.success('Helper 已重启并恢复连接');
  } catch (error) {
    const text = error instanceof Error ? error.message : String(error);
    message.error(`Helper 重启失败: ${text}`);
  }
}

async function exportProfiles(): Promise<void> {
  const payload = await exportHelperProfiles();
  const blob = new Blob([JSON.stringify(payload, null, 2)], { type: 'application/json;charset=utf-8' });
  const url = URL.createObjectURL(blob);
  const anchor = document.createElement('a');
  anchor.href = url;
  anchor.download = 'hw75-helper-profiles.json';
  anchor.click();
  URL.revokeObjectURL(url);
  message.success('已导出 Helper 配置');
}

function triggerProfileImport(): void {
  profileImportInput.value?.click();
}

async function onProfileImportChange(event: Event): Promise<void> {
  const input = event.target as HTMLInputElement;
  const file = input.files?.[0];
  if (!file) {
    return;
  }
  try {
    const payload = JSON.parse(await file.text()) as Parameters<typeof importHelperProfiles>[0];
    const result = await importHelperProfiles(payload);
    await functionSlotStore.refresh();
    message.success(`已导入 ${result.importedCount} 条 Helper 配置`);
  } finally {
    input.value = '';
  }
}
</script>

<style lang="scss" module>
.page {
  width: 100%;
}

.card {
  background: linear-gradient(180deg, rgba(7, 24, 33, 0.92), rgba(9, 14, 22, 0.94));
  border: 1px solid rgba(49, 84, 113, 0.45);
  box-shadow: 0 20px 60px rgba(0, 0, 0, 0.18);

  :global(.ant-card-head) {
    border-bottom-color: rgba(49, 84, 113, 0.35);
  }
}

.overview {
  display: flex;
  justify-content: space-between;
  gap: 24px;
}

.actions {
  align-items: flex-start;
}

.hiddenInput {
  display: none;
}

.helperHint {
  margin-left: 8px;
  color: #6d7f90;
}

.helperManagerActions {
  margin-top: 12px;
}

.workspace {
  display: grid;
  grid-template-columns: minmax(0, 1.6fr) minmax(360px, 0.9fr);
  gap: 16px;
}

.sidebar {
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.layer {
  display: flex;
  flex-direction: column;
  gap: 8px;
  width: max-content;
}

.keyboardFrame {
  --key-unit-size: 56px;
  --key-gap: 8px;
  padding: 14px;
  border-radius: 28px;
  border: 1px solid rgba(56, 79, 101, 0.5);
  background:
    radial-gradient(circle at top, rgba(38, 77, 111, 0.18), transparent 36%),
    linear-gradient(180deg, rgba(9, 16, 27, 0.98), rgba(6, 11, 19, 0.98));
  box-shadow:
    inset 0 1px 0 rgba(255, 255, 255, 0.04),
    0 24px 50px rgba(0, 0, 0, 0.22);
  overflow-x: auto;
  overflow-y: hidden;
}

.keyboardHint {
  margin-bottom: 12px;
  color: #7f97ac;
  font-size: 0.88rem;
}

.row {
  display: flex;
  gap: var(--key-gap);
  width: max-content;
}

.key {
  position: relative;
  flex: 0 0 calc(var(--key-units, 1) * var(--key-unit-size));
  width: calc(var(--key-units, 1) * var(--key-unit-size));
  min-width: calc(var(--key-units, 1) * var(--key-unit-size));
  margin-left: calc(var(--key-gap-before, 0) * var(--key-unit-size));
  min-height: 100px;
  border-radius: 18px;
  border: 1px solid rgba(71, 99, 131, 0.65);
  background: linear-gradient(180deg, rgba(26, 39, 57, 0.96), rgba(14, 22, 35, 0.96));
  color: #f2f7fb;
  text-align: left;
  padding: 14px 12px 10px;
  transition: border-color 160ms ease, transform 160ms ease, box-shadow 160ms ease;
  cursor: default;
}

.slotKey {
  cursor: pointer;
  border-color: var(--slot-accent, rgba(39, 213, 255, 0.9));
  box-shadow: 0 0 0 1px color-mix(in srgb, var(--slot-accent, #27d5ff) 25%, transparent),
    0 0 24px color-mix(in srgb, var(--slot-accent, #27d5ff) 18%, transparent),
    inset 0 0 28px color-mix(in srgb, var(--slot-accent, #27d5ff) 16%, transparent);
}

.slotKey:hover,
.slotKeySelected {
  transform: translateY(-2px);
  box-shadow: 0 0 0 1px color-mix(in srgb, var(--slot-accent, #27d5ff) 35%, transparent),
    0 0 40px color-mix(in srgb, var(--slot-accent, #27d5ff) 24%, transparent),
    inset 0 0 40px color-mix(in srgb, var(--slot-accent, #27d5ff) 20%, transparent);
}

.slotBadge {
  position: absolute;
  right: 12px;
  top: 10px;
  padding: 2px 8px;
  border-radius: 999px;
  background: color-mix(in srgb, var(--slot-accent, #27d5ff) 22%, transparent);
  border: 1px solid color-mix(in srgb, var(--slot-accent, #27d5ff) 36%, transparent);
  font-size: 0.72rem;
  letter-spacing: 0.06em;
}

.keyLegend {
  font-size: clamp(1rem, 1.45vw, 1.55rem);
  font-weight: 700;
  line-height: 1.05;
}

.keyLegendSmall {
  font-size: clamp(0.92rem, 1.18vw, 1.2rem);
}

.keyRaw {
  margin-top: 10px;
  font-size: clamp(0.72rem, 0.88vw, 0.88rem);
  color: #8ea9c7;
  line-height: 1.35;
  word-break: break-word;
}

.editorHeader {
  display: flex;
  justify-content: space-between;
  gap: 12px;
  margin-bottom: 16px;
}

.editorTitle {
  font-size: 1.6rem;
  font-weight: 700;
}

.editorMeta {
  margin-top: 6px;
  color: #84a3bf;
}

.editorSubMeta {
  margin-top: 4px;
  color: #6f8498;
  font-size: 0.88rem;
}

.sectionTitle {
  margin: 12px 0 10px;
  font-size: 0.92rem;
  color: #9ab4cb;
  text-transform: uppercase;
  letter-spacing: 0.08em;
}

.paletteGrid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 10px;
}

.paletteCard {
  --accent: #15a4ad;
  display: flex;
  flex-direction: column;
  gap: 6px;
  border-radius: 18px;
  border: 1px solid rgba(83, 105, 128, 0.45);
  background: linear-gradient(180deg, rgba(22, 28, 42, 0.96), rgba(14, 20, 31, 0.96));
  color: #eff8ff;
  padding: 14px 14px 12px;
  text-align: left;
  cursor: pointer;
}

.paletteCard span {
  color: #8fa5b8;
  font-size: 0.86rem;
}

.paletteCardSelected {
  border-color: var(--accent);
  box-shadow: 0 0 22px color-mix(in srgb, var(--accent) 28%, transparent),
    inset 0 0 24px color-mix(in srgb, var(--accent) 12%, transparent);
}

.toggleTag {
  cursor: pointer;
}

.keyPicker {
  display: grid;
  grid-template-columns: repeat(4, minmax(0, 1fr));
  gap: 8px;
  max-height: 320px;
  overflow: auto;
  margin-top: 12px;
}

.quickKeyGrid {
  display: grid;
  grid-template-columns: repeat(5, minmax(0, 1fr));
  gap: 8px;
  margin-bottom: 12px;
}

.choiceKey {
  border-radius: 14px;
  border: 1px solid rgba(83, 105, 128, 0.45);
  background: rgba(13, 21, 34, 0.96);
  color: #eff8ff;
  min-height: 46px;
  cursor: pointer;
}

.choiceKeySelected {
  border-color: #15a4ad;
  box-shadow: 0 0 18px rgba(21, 164, 173, 0.25);
}

.macroList {
  display: flex;
  flex-direction: column;
  gap: 10px;
  margin-bottom: 12px;
}

.macroStep {
  display: grid;
  grid-template-columns: 120px 140px 180px minmax(0, auto);
  gap: 10px;
  align-items: center;
  padding: 12px;
  border-radius: 16px;
  border: 1px solid rgba(83, 105, 128, 0.45);
  background: rgba(13, 20, 31, 0.92);
}

.macroStepDragging {
  opacity: 0.55;
}

.helperAlert {
  margin-bottom: 12px;
}

.helperForm {
  margin-top: 14px;
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.helperField {
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.dragHint {
  color: #8ea9c7;
  line-height: 1.5;
  margin-bottom: 12px;
}

.activityList {
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.activityItem {
  padding: 12px;
  border-radius: 16px;
  border: 1px solid rgba(83, 105, 128, 0.45);
  background: rgba(13, 20, 31, 0.92);
}

.activityHeader {
  display: flex;
  justify-content: space-between;
  gap: 12px;
  align-items: center;
}

.activityBody {
  margin-top: 6px;
  color: #dce6ef;
}

.activityError {
  margin-top: 8px;
  color: #ff8b8b;
  font-size: 0.88rem;
  line-height: 1.4;
}

.placeholder {
  min-height: 140px;
  display: grid;
  place-items: center;
  color: #7d91a5;
}

@media (max-width: 1400px) {
  .workspace {
    grid-template-columns: 1fr;
  }
}

@media (max-width: 1180px) {
  .keyboardFrame {
    --key-unit-size: 52px;
    --key-gap: 7px;
  }
}

@media (max-width: 900px) {
  .keyboardFrame {
    --key-unit-size: 48px;
    --key-gap: 6px;
  }
}
</style>
