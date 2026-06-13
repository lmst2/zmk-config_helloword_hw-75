<template>
  <a-space direction="vertical" :size="16" :class="$style.page">
    <a-card title="TouchBar Editor" size="small" :class="$style.shellCard">
      <template v-if="draftConfig">
        <div :class="$style.toolbar">
          <a-space wrap :size="12">
            <a-button @click="refreshFromDevice">Refresh</a-button>
            <a-button @click="resetDraft">Reset Draft</a-button>
            <a-button @click="addSegment" :disabled="segmentDrafts.length >= maxSegments">Add Segment</a-button>
            <a-button type="primary" :disabled="touchbarValidationErrors.length > 0" @click="applyDraft">
              Apply to Keyboard
            </a-button>
          </a-space>
          <a-radio-group :value="draftConfig.mode" button-style="solid" @update:value="updateModeDraft">
            <a-radio-button v-for="mode in modeOptions" :key="mode.value" :value="mode.value">
              {{ mode.label }}
            </a-radio-button>
          </a-radio-group>
        </div>

        <a-descriptions :column="1" size="small" :label-style="{ width: '15em' }" :class="$style.overview">
          <a-descriptions-item label="Current Firmware Mode">
            <strong>{{ currentModeLabel }}</strong>
          </a-descriptions-item>
          <a-descriptions-item label="Draft Mode">
            <strong>{{ draftModeLabel }}</strong>
          </a-descriptions-item>
          <a-descriptions-item label="Logical Points">
            <code>{{ draftConfig.logicalPointCount }}</code>
          </a-descriptions-item>
          <a-descriptions-item label="Segment Count">
            <code>{{ segmentDrafts.length }}</code>
          </a-descriptions-item>
          <a-descriptions-item label="Shared Points">
            <code>{{ sharedPointCount }}</code>
          </a-descriptions-item>
        </a-descriptions>

        <a-alert
          v-if="touchbarValidationErrors.length"
          type="warning"
          show-icon
          :message="touchbarValidationErrors[0]"
          :description="touchbarValidationErrors.slice(1).join(' / ')"
          :class="$style.alert"
        />
      </template>
      <div v-else :class="$style.placeholder">Waiting for TouchBar config from firmware.</div>
    </a-card>

    <div :class="$style.workspace">
      <a-card title="Touch Strip Mapping" size="small" :class="$style.visualCard">
        <template v-if="logicalPoints.length">
          <div :class="$style.stripShell">
            <div :class="$style.strip">
              <template v-for="segment in segmentVisuals" :key="`visual-${segment.index}`">
                <div
                  v-for="range in segment.touchRanges"
                  :key="`touch-${segment.index}-${range.key}`"
                  :class="$style.segmentBand"
                  :style="range.style"
                ></div>
                <div
                  v-for="range in segment.entryRanges"
                  :key="`entry-${segment.index}-${range.key}`"
                  :class="$style.entryBand"
                  :style="range.style"
                ></div>
              </template>
              <button
                v-for="point in logicalPoints"
                :key="point.index"
                type="button"
                :class="[
                  $style.touchPoint,
                  point.state === 'sharedEntry' && $style.pointSharedEntry,
                  point.state === 'shared' && $style.pointShared,
                  point.state === 'entry' && $style.pointEntry,
                  point.state === 'strip' && $style.pointStrip,
                  point.state === 'idle' && $style.pointIdle,
                ]"
                :style="{ left: point.left }"
              >
                <span :class="$style.touchPointIndex">P{{ point.index + 1 }}</span>
              </button>
            </div>
          </div>

          <div :class="$style.pointMetaRow">
            <div v-for="point in logicalPoints" :key="`meta-${point.index}`" :class="$style.pointMeta">
              <strong>P{{ point.index + 1 }}</strong>
              <span>r{{ point.row }} / c{{ point.col }}</span>
            </div>
          </div>

          <div :class="$style.segmentEditorList">
            <a-card
              v-for="segment in segmentDrafts"
              :key="`segment-editor-${segment.index}`"
              size="small"
              :class="$style.segmentEditor"
            >
              <template #title>
                <span>{{ segment.label }}</span>
              </template>
              <template #extra>
                <a-space :size="8">
                  <span :class="$style.segmentSwatch" :style="{ background: segment.fill }"></span>
                  <a-button
                    v-if="segmentDrafts.length > 1"
                    size="small"
                    danger
                    ghost
                    @click="removeSegment(segment.index)"
                  >
                    Remove
                  </a-button>
                </a-space>
              </template>

              <div :class="$style.zoneEditor">
                <div :class="$style.zoneRow">
                  <div :class="$style.zoneLabel">Touch Span</div>
                  <a-space wrap>
                    <a-button
                      v-for="point in logicalPoints"
                      :key="`touch-${segment.index}-${point.index}`"
                      size="small"
                      :type="point.memberships[segment.index]?.touch ? 'primary' : 'default'"
                      @click="toggleSegmentPoint(segment.index, point.index, 'touch')"
                    >
                      P{{ point.index + 1 }}
                    </a-button>
                  </a-space>
                </div>
                <div :class="$style.zoneRow">
                  <div :class="$style.zoneLabel">Entry Span</div>
                  <a-space wrap>
                    <a-button
                      v-for="point in logicalPoints"
                      :key="`entry-${segment.index}-${point.index}`"
                      size="small"
                      :type="point.memberships[segment.index]?.entry ? 'primary' : 'default'"
                      @click="toggleSegmentPoint(segment.index, point.index, 'entry')"
                    >
                      P{{ point.index + 1 }}
                    </a-button>
                  </a-space>
                </div>
              </div>
            </a-card>
          </div>
          <div :class="$style.pointHint">
            Segments now follow the real firmware masks. The strip preview above re-renders directly from the current
            draft instead of assuming a fixed left/right split, and each segment can span any combination of points.
          </div>
        </template>
        <div v-else :class="$style.placeholder">No logical map available yet.</div>
      </a-card>

      <a-card title="Mode Parameters" size="small" :class="$style.editorCard">
        <template v-if="draftConfig">
          <a-form layout="vertical">
            <a-form-item label="Mode Indicator">
              <a-switch
                :checked="!!draftConfig.modeIndicatorEnabled"
                checked-children="On"
                un-checked-children="Off"
                @update:checked="updateRootValue('modeIndicatorEnabled', $event)"
              />
            </a-form-item>
          </a-form>

          <a-tabs>
            <a-tab-pane key="pan" tab="Pan">
              <div :class="$style.formGrid">
                <a-form-item label="Activation (ms)">
                  <a-input-number :value="draftConfig.pan?.activationMs" :min="1" :step="1" @update:value="updateNestedValue('pan', 'activationMs', $event)" />
                </a-form-item>
                <a-form-item label="Release Grace (ms)">
                  <a-input-number :value="draftConfig.pan?.releaseGraceMs" :min="1" :step="1" @update:value="updateNestedValue('pan', 'releaseGraceMs', $event)" />
                </a-form-item>
                <a-form-item label="Poll Interval (ms)">
                  <a-input-number :value="draftConfig.pan?.pollIntervalMs" :min="1" :step="1" @update:value="updateNestedValue('pan', 'pollIntervalMs', $event)" />
                </a-form-item>
                <a-form-item label="Wheel Interval (ms)">
                  <a-input-number :value="draftConfig.pan?.intervalMs" :min="1" :step="1" @update:value="updateNestedValue('pan', 'intervalMs', $event)" />
                </a-form-item>
                <a-form-item label="Deadzone">
                  <a-input-number :value="draftConfig.pan?.deadzone" :min="1" :step="1" @update:value="updateNestedValue('pan', 'deadzone', $event)" />
                </a-form-item>
                <a-form-item label="Position Scale">
                  <a-input-number :value="draftConfig.pan?.positionScale" :min="1" :step="1" @update:value="updateNestedValue('pan', 'positionScale', $event)" />
                </a-form-item>
              </div>
              <div :class="$style.indicatorRow">
                <label :class="$style.colorField">
                  <span>Indicator Color</span>
                  <input type="color" :value="formatColor(draftConfig.panIndicator?.colorRgb)" @input="updateIndicatorColor('panIndicator', $event)" />
                </label>
                <a-form-item label="Indicator Duration (ms)">
                  <a-input-number :value="draftConfig.panIndicator?.durationMs" :min="1" :step="50" @update:value="updateNestedValue('panIndicator', 'durationMs', $event)" />
                </a-form-item>
              </div>
            </a-tab-pane>

            <a-tab-pane key="app" tab="App Switch">
              <div :class="$style.formGrid">
                <a-form-item label="Activation (ms)">
                  <a-input-number :value="draftConfig.appSwitch?.activationMs" :min="1" :step="1" @update:value="updateNestedValue('appSwitch', 'activationMs', $event)" />
                </a-form-item>
                <a-form-item label="Release Grace (ms)">
                  <a-input-number :value="draftConfig.appSwitch?.releaseGraceMs" :min="1" :step="1" @update:value="updateNestedValue('appSwitch', 'releaseGraceMs', $event)" />
                </a-form-item>
                <a-form-item label="Release Settle (ms)">
                  <a-input-number :value="draftConfig.appSwitch?.releaseSettleMs" :min="1" :step="1" @update:value="updateNestedValue('appSwitch', 'releaseSettleMs', $event)" />
                </a-form-item>
                <a-form-item label="Step Interval (ms)">
                  <a-input-number :value="draftConfig.appSwitch?.stepIntervalMs" :min="1" :step="1" @update:value="updateNestedValue('appSwitch', 'stepIntervalMs', $event)" />
                </a-form-item>
                <a-form-item label="Step Distance">
                  <a-input-number :value="draftConfig.appSwitch?.stepDistance" :min="1" :step="1" @update:value="updateNestedValue('appSwitch', 'stepDistance', $event)" />
                </a-form-item>
                <a-form-item label="Edge Repeat Delay (ms)">
                  <a-input-number :value="draftConfig.appSwitch?.edgeRepeatDelayMs" :min="1" :step="1" @update:value="updateNestedValue('appSwitch', 'edgeRepeatDelayMs', $event)" />
                </a-form-item>
              </div>
              <div :class="$style.indicatorRow">
                <label :class="$style.colorField">
                  <span>Indicator Color</span>
                  <input type="color" :value="formatColor(draftConfig.appIndicator?.colorRgb)" @input="updateIndicatorColor('appIndicator', $event)" />
                </label>
                <a-form-item label="Indicator Duration (ms)">
                  <a-input-number :value="draftConfig.appIndicator?.durationMs" :min="1" :step="50" @update:value="updateNestedValue('appIndicator', 'durationMs', $event)" />
                </a-form-item>
              </div>
            </a-tab-pane>

            <a-tab-pane key="desktop" tab="Desktop Switch">
              <div :class="$style.formGrid">
                <a-form-item label="Activation (ms)">
                  <a-input-number :value="draftConfig.desktopSwitch?.activationMs" :min="1" :step="1" @update:value="updateNestedValue('desktopSwitch', 'activationMs', $event)" />
                </a-form-item>
                <a-form-item label="Release Grace (ms)">
                  <a-input-number :value="draftConfig.desktopSwitch?.releaseGraceMs" :min="1" :step="1" @update:value="updateNestedValue('desktopSwitch', 'releaseGraceMs', $event)" />
                </a-form-item>
                <a-form-item label="Seek Hold (ms)">
                  <a-input-number :value="draftConfig.desktopSwitch?.holdMs" :min="1" :step="1" @update:value="updateNestedValue('desktopSwitch', 'holdMs', $event)" />
                </a-form-item>
                <a-form-item label="Step Interval (ms)">
                  <a-input-number :value="draftConfig.desktopSwitch?.stepIntervalMs" :min="1" :step="1" @update:value="updateNestedValue('desktopSwitch', 'stepIntervalMs', $event)" />
                </a-form-item>
                <a-form-item label="Step Distance">
                  <a-input-number :value="draftConfig.desktopSwitch?.stepDistance" :min="1" :step="1" @update:value="updateNestedValue('desktopSwitch', 'stepDistance', $event)" />
                </a-form-item>
                <a-form-item label="Edge Repeat Delay (ms)">
                  <a-input-number :value="draftConfig.desktopSwitch?.edgeRepeatDelayMs" :min="1" :step="1" @update:value="updateNestedValue('desktopSwitch', 'edgeRepeatDelayMs', $event)" />
                </a-form-item>
                <a-form-item label="Swipe Distance">
                  <a-input-number :value="draftConfig.desktopSwitch?.swipeDistance" :min="1" :step="1" @update:value="updateNestedValue('desktopSwitch', 'swipeDistance', $event)" />
                </a-form-item>
              </div>
              <div :class="$style.indicatorRow">
                <label :class="$style.colorField">
                  <span>Indicator Color</span>
                  <input type="color" :value="formatColor(draftConfig.desktopIndicator?.colorRgb)" @input="updateIndicatorColor('desktopIndicator', $event)" />
                </label>
                <a-form-item label="Indicator Duration (ms)">
                  <a-input-number :value="draftConfig.desktopIndicator?.durationMs" :min="1" :step="50" @update:value="updateNestedValue('desktopIndicator', 'durationMs', $event)" />
                </a-form-item>
              </div>
            </a-tab-pane>
          </a-tabs>
        </template>
      </a-card>
    </div>
  </a-space>
</template>

<script lang="ts" setup>
import { computed, onMounted, ref, watch } from 'vue';
import { storeToRefs } from 'pinia';
import { message } from 'ant-design-vue';

import { UsbComm } from '@/proto/comm.proto';
import { useTouchbarStore } from '@/stores/touchbar';
import { useUsbComm, onDeviceConnected } from '@/stores/usb';
import { useVersionStore } from '@/stores/version';

type TouchbarNestedKey =
  | 'pan'
  | 'appSwitch'
  | 'desktopSwitch'
  | 'panIndicator'
  | 'appIndicator'
  | 'desktopIndicator';

type SegmentDraft = {
  index: number;
  label: string;
  touchMask: number;
  entryMask: number;
  fill: string;
  glow: string;
  entryGlow: string;
};

type PointMembership = {
  touch: boolean;
  entry: boolean;
};

const maxSegments = 6;
const pointTrackStart = 8;
const pointTrackEnd = 92;
const pointBandRadius = 6.5;
const segmentPalette = [
  { fill: 'linear-gradient(90deg, rgba(45, 111, 219, 0.30), rgba(45, 111, 219, 0.18))', glow: 'rgba(71, 154, 255, 0.24)', entryGlow: 'rgba(145, 214, 255, 0.34)' },
  { fill: 'linear-gradient(90deg, rgba(228, 136, 42, 0.30), rgba(228, 136, 42, 0.18))', glow: 'rgba(255, 166, 84, 0.22)', entryGlow: 'rgba(255, 220, 132, 0.32)' },
  { fill: 'linear-gradient(90deg, rgba(92, 196, 126, 0.30), rgba(92, 196, 126, 0.18))', glow: 'rgba(122, 230, 159, 0.22)', entryGlow: 'rgba(181, 255, 180, 0.32)' },
  { fill: 'linear-gradient(90deg, rgba(158, 106, 245, 0.28), rgba(158, 106, 245, 0.16))', glow: 'rgba(188, 142, 255, 0.22)', entryGlow: 'rgba(222, 195, 255, 0.32)' },
  { fill: 'linear-gradient(90deg, rgba(47, 193, 176, 0.28), rgba(47, 193, 176, 0.16))', glow: 'rgba(89, 233, 212, 0.22)', entryGlow: 'rgba(170, 255, 236, 0.32)' },
  { fill: 'linear-gradient(90deg, rgba(219, 87, 109, 0.28), rgba(219, 87, 109, 0.16))', glow: 'rgba(255, 130, 152, 0.22)', entryGlow: 'rgba(255, 190, 203, 0.32)' },
];

const touchbarStore = useTouchbarStore();
const comm = useUsbComm();
const versionStore = useVersionStore();

const { touchbarConfig: config } = storeToRefs(touchbarStore);
const { version } = storeToRefs(versionStore);

const draftConfig = ref<UsbComm.ITouchbarConfig>();

const modeOptions = [
  { value: UsbComm.TouchbarMode.TOUCHBAR_PAN, label: 'Pan' },
  { value: UsbComm.TouchbarMode.TOUCHBAR_APP_SWITCH, label: 'App Switch' },
  { value: UsbComm.TouchbarMode.TOUCHBAR_DESKTOP_SWITCH, label: 'Desktop Switch' },
];

const currentModeLabel = computed(() =>
  modeOptions.find((mode) => mode.value === config.value?.mode)?.label ?? 'Unknown',
);

const draftModeLabel = computed(() =>
  modeOptions.find((mode) => mode.value === draftConfig.value?.mode)?.label ?? 'Unknown',
);

const segmentDrafts = computed<SegmentDraft[]>(() => {
  if (!draftConfig.value) {
    return [];
  }

  const count = normalizeDraftConfig(draftConfig.value);
  return Array.from({ length: count }, (_, index) => {
    const palette = segmentPalette[index % segmentPalette.length];
    return {
      index,
      label: `Segment ${index + 1}`,
      touchMask: draftConfig.value?.segmentTouchMasks?.[index] ?? 0,
      entryMask: draftConfig.value?.segmentEntryMasks?.[index] ?? 0,
      ...palette,
    };
  });
});

const logicalPoints = computed(() => {
  if (!draftConfig.value) {
    return [];
  }

  const pointCount = draftConfig.value.logicalPointCount ?? 0;
  const map0 = draftConfig.value.logicalMapPacked0 ?? 0;
  const map1 = draftConfig.value.logicalMapPacked1 ?? 0;

  function nibble(value: number, shift: number) {
    return (value >> shift) & 0xf;
  }

  return Array.from({ length: pointCount }, (_, index) => {
    const packed = index < 4 ? map0 : map1;
    const localIndex = index < 4 ? index : index - 4;
    const shift = localIndex * 8;
    const bit = pointBit(index, pointCount);
    const memberships = segmentDrafts.value.map<PointMembership>((segment) => ({
      touch: (segment.touchMask & bit) !== 0,
      entry: (segment.entryMask & bit) !== 0,
    }));
    const touchMembershipCount = memberships.filter((membership) => membership.touch).length;
    const entryMembershipCount = memberships.filter((membership) => membership.entry).length;

    return {
      index,
      row: nibble(packed, shift),
      col: nibble(packed, shift + 4),
      memberships,
      left: `${pointCenter(index, pointCount)}%`,
      state: resolvePointState(touchMembershipCount, entryMembershipCount),
    };
  });
});

const segmentVisuals = computed(() =>
  segmentDrafts.value.map((segment) => ({
    ...segment,
    touchRanges: buildMaskRanges(segment.touchMask, draftConfig.value?.logicalPointCount ?? 0).map((range) => ({
      key: `${range.start}-${range.end}`,
      style: buildRangeStyle(range.start, range.end, draftConfig.value?.logicalPointCount ?? 0, segment.fill, segment.glow),
    })),
    entryRanges: buildMaskRanges(segment.entryMask, draftConfig.value?.logicalPointCount ?? 0).map((range) => ({
      key: `${range.start}-${range.end}`,
      style: buildRangeStyle(range.start, range.end, draftConfig.value?.logicalPointCount ?? 0, segment.entryGlow, segment.entryGlow, true),
    })),
  })),
);

const sharedPointCount = computed(() => {
  const counts = new Array(draftConfig.value?.logicalPointCount ?? 0).fill(0);
  segmentDrafts.value.forEach((segment) => {
    counts.forEach((_, index) => {
      const bit = pointBit(index, draftConfig.value?.logicalPointCount ?? 0);
      if ((segment.touchMask & bit) !== 0) {
        counts[index] += 1;
      }
    });
  });
  return counts.filter((count) => count > 1).length;
});

const touchbarValidationErrors = computed(() => {
  if (!draftConfig.value) {
    return [];
  }

  const errors: string[] = [];
  if (!segmentDrafts.value.length) {
    errors.push('At least one segment is required.');
  }

  segmentDrafts.value.forEach((segment) => {
    if (popcount(segment.touchMask) === 0) {
      errors.push(`${segment.label} must contain at least one touch point.`);
    }
    if (popcount(segment.entryMask) === 0) {
      errors.push(`${segment.label} entry zone must contain at least one point.`);
    }
    if ((segment.entryMask & ~segment.touchMask) !== 0) {
      errors.push(`${segment.label} entry zone must stay inside its touch span.`);
    }
  });

  if (segmentDrafts.value.length > maxSegments) {
    errors.push(`TouchBar currently supports up to ${maxSegments} segments.`);
  }

  return errors;
});

async function loadTouchbarConfig(): Promise<void> {
  if (!comm.device || !version.value?.features?.touchbarConfig) {
    return;
  }

  await touchbarStore.getTouchbarConfig();
}

function cloneConfig(source?: UsbComm.ITouchbarConfig): UsbComm.ITouchbarConfig | undefined {
  if (!source) {
    return undefined;
  }

  return JSON.parse(JSON.stringify(source)) as UsbComm.ITouchbarConfig;
}

function syncDraft(): void {
  draftConfig.value = cloneConfig(config.value);
  if (draftConfig.value) {
    normalizeDraftConfig(draftConfig.value);
  }
}

function refreshFromDevice(): void {
  void loadTouchbarConfig();
}

function resetDraft(): void {
  syncDraft();
  message.success('TouchBar draft reset.');
}

async function applyDraft(): Promise<void> {
  if (!draftConfig.value || touchbarValidationErrors.value.length > 0) {
    return;
  }

  const normalized = cloneConfig(draftConfig.value);
  if (!normalized) {
    return;
  }

  normalizeDraftConfig(normalized);
  await touchbarStore.setConfig(normalized);
  message.success('TouchBar config applied.');
}

function updateModeDraft(mode: UsbComm.TouchbarMode): void {
  ensureDraft();
  if (!draftConfig.value) {
    return;
  }

  draftConfig.value.mode = mode;
}

function addSegment(): void {
  ensureDraft();
  if (!draftConfig.value) {
    return;
  }

  const count = normalizeDraftConfig(draftConfig.value);
  if (count >= maxSegments) {
    return;
  }

  draftConfig.value.segmentCount = count + 1;
  draftConfig.value.segmentTouchMasks?.push(0);
  draftConfig.value.segmentEntryMasks?.push(0);
  syncLegacyMasks(draftConfig.value);
}

function removeSegment(segmentIndex: number): void {
  ensureDraft();
  if (!draftConfig.value) {
    return;
  }

  const count = normalizeDraftConfig(draftConfig.value);
  if (count <= 1) {
    return;
  }

  draftConfig.value.segmentTouchMasks = (draftConfig.value.segmentTouchMasks ?? []).filter((_, index) => index !== segmentIndex);
  draftConfig.value.segmentEntryMasks = (draftConfig.value.segmentEntryMasks ?? []).filter((_, index) => index !== segmentIndex);
  draftConfig.value.segmentCount = draftConfig.value.segmentTouchMasks.length;
  syncLegacyMasks(draftConfig.value);
}

function updateRootValue(key: keyof UsbComm.ITouchbarConfig, value: unknown): void {
  ensureDraft();
  if (!draftConfig.value) {
    return;
  }

  (draftConfig.value as Record<string, unknown>)[key as string] = value;
}

function updateNestedValue(section: TouchbarNestedKey, key: string, value: number | null): void {
  ensureNestedSection(section);
  if (!draftConfig.value) {
    return;
  }

  const target = (draftConfig.value as Record<string, Record<string, number | undefined>>)[section];
  if (!target) {
    return;
  }

  target[key] = value ?? undefined;
}

function updateIndicatorColor(section: 'panIndicator' | 'appIndicator' | 'desktopIndicator', event: Event): void {
  ensureNestedSection(section);
  if (!draftConfig.value) {
    return;
  }

  const input = event.target as HTMLInputElement | null;
  if (!input) {
    return;
  }

  const colorRgb = Number.parseInt(input.value.replace('#', ''), 16);
  const target = (draftConfig.value as Record<string, Record<string, number | undefined>>)[section];
  if (!target) {
    return;
  }

  target.colorRgb = colorRgb;
}

function toggleSegmentPoint(segmentIndex: number, pointIndex: number, zone: 'touch' | 'entry'): void {
  ensureDraft();
  if (!draftConfig.value) {
    return;
  }

  const pointCount = draftConfig.value.logicalPointCount ?? 0;
  const bit = pointBit(pointIndex, pointCount);
  const touchMasks = draftConfig.value.segmentTouchMasks ?? [];
  const entryMasks = draftConfig.value.segmentEntryMasks ?? [];
  const currentTouch = touchMasks[segmentIndex] ?? 0;
  const currentEntry = entryMasks[segmentIndex] ?? 0;

  if (zone === 'touch') {
    const enabled = (currentTouch & bit) !== 0;
    touchMasks[segmentIndex] = enabled ? (currentTouch & ~bit) : (currentTouch | bit);
    if (enabled) {
      entryMasks[segmentIndex] = currentEntry & ~bit;
    }
  } else {
    const enabled = (currentEntry & bit) !== 0;
    entryMasks[segmentIndex] = enabled ? (currentEntry & ~bit) : (currentEntry | bit);
    if (!enabled) {
      touchMasks[segmentIndex] = currentTouch | bit;
    }
  }

  draftConfig.value.segmentTouchMasks = [...touchMasks];
  draftConfig.value.segmentEntryMasks = [...entryMasks];
  syncLegacyMasks(draftConfig.value);
}

function ensureDraft(): void {
  if (!draftConfig.value) {
    syncDraft();
  }
  if (draftConfig.value) {
    normalizeDraftConfig(draftConfig.value);
  }
}

function ensureNestedSection(section: TouchbarNestedKey): void {
  ensureDraft();
  if (!draftConfig.value) {
    return;
  }

  const map = draftConfig.value as Record<string, Record<string, number | undefined> | undefined>;
  map[section] ??= {};
}

function pointBit(index: number, pointCount: number): number {
  return 1 << (pointCount - 1 - index);
}

function pointCenter(index: number, pointCount: number): number {
  if (pointCount <= 1) {
    return (pointTrackStart + pointTrackEnd) / 2;
  }

  return pointTrackStart + (index / (pointCount - 1)) * (pointTrackEnd - pointTrackStart);
}

function normalizeDraftConfig(configValue: UsbComm.ITouchbarConfig): number {
  const arrayTouchCount = configValue.segmentTouchMasks?.length ?? 0;
  const arrayEntryCount = configValue.segmentEntryMasks?.length ?? 0;
  const legacyCount =
    configValue.leftTouchMask !== undefined ||
    configValue.leftEntryMask !== undefined ||
    configValue.rightTouchMask !== undefined ||
    configValue.rightEntryMask !== undefined
      ? 2
      : 0;
  const count = Math.min(
    maxSegments,
    Math.max(1, configValue.segmentCount ?? 0, arrayTouchCount, arrayEntryCount, legacyCount),
  );

  const touchMasks = Array.from({ length: count }, (_, index) => {
    if (configValue.segmentTouchMasks?.[index] !== undefined) {
      return configValue.segmentTouchMasks[index] ?? 0;
    }
    if (index === 0) {
      return configValue.leftTouchMask ?? 0;
    }
    if (index === 1) {
      return configValue.rightTouchMask ?? 0;
    }
    return 0;
  });
  const entryMasks = Array.from({ length: count }, (_, index) => {
    if (configValue.segmentEntryMasks?.[index] !== undefined) {
      return configValue.segmentEntryMasks[index] ?? 0;
    }
    if (index === 0) {
      return configValue.leftEntryMask ?? 0;
    }
    if (index === 1) {
      return configValue.rightEntryMask ?? 0;
    }
    return 0;
  });

  configValue.segmentCount = count;
  configValue.segmentTouchMasks = touchMasks;
  configValue.segmentEntryMasks = entryMasks;
  syncLegacyMasks(configValue);
  return count;
}

function syncLegacyMasks(configValue: UsbComm.ITouchbarConfig): void {
  configValue.leftTouchMask = configValue.segmentTouchMasks?.[0] ?? 0;
  configValue.leftEntryMask = configValue.segmentEntryMasks?.[0] ?? 0;
  configValue.rightTouchMask = configValue.segmentTouchMasks?.[1] ?? 0;
  configValue.rightEntryMask = configValue.segmentEntryMasks?.[1] ?? 0;
}

function buildMaskRanges(mask: number, pointCount: number): Array<{ start: number; end: number }> {
  const ranges: Array<{ start: number; end: number }> = [];
  let start = -1;
  let previous = -1;

  for (let index = 0; index < pointCount; index++) {
    const included = (mask & pointBit(index, pointCount)) !== 0;
    if (included && start < 0) {
      start = index;
      previous = index;
      continue;
    }
    if (included && index === previous + 1) {
      previous = index;
      continue;
    }
    if (!included && start >= 0) {
      ranges.push({ start, end: previous });
      start = -1;
      previous = -1;
    }
    if (included) {
      start = index;
      previous = index;
    }
  }

  if (start >= 0) {
    ranges.push({ start, end: previous });
  }

  return ranges;
}

function buildRangeStyle(
  start: number,
  end: number,
  pointCount: number,
  background: string,
  glow: string,
  isEntry = false,
) {
  const left = pointCenter(start, pointCount) - pointBandRadius;
  const right = pointCenter(end, pointCount) + pointBandRadius;

  return {
    left: `${left}%`,
    width: `${right - left}%`,
    background,
    boxShadow: isEntry ? `0 0 0 1px ${glow}, 0 0 22px ${glow}` : `0 0 24px ${glow}`,
  };
}

function formatColor(colorRgb?: number | null): string {
  if (colorRgb === undefined || colorRgb === null) {
    return '#000000';
  }

  return `#${colorRgb.toString(16).padStart(6, '0')}`;
}

function popcount(value: number): number {
  let remaining = value >>> 0;
  let count = 0;
  while (remaining !== 0) {
    remaining &= remaining - 1;
    count++;
  }
  return count;
}

function resolvePointState(touchMembershipCount: number, entryMembershipCount: number): 'sharedEntry' | 'shared' | 'entry' | 'strip' | 'idle' {
  if (touchMembershipCount > 1) {
    return entryMembershipCount > 0 ? 'sharedEntry' : 'shared';
  }
  if (entryMembershipCount > 0) {
    return 'entry';
  }
  return touchMembershipCount > 0 ? 'strip' : 'idle';
}

watch(config, () => {
  syncDraft();
}, { immediate: true });

onMounted(() => {
  void loadTouchbarConfig();
});

onDeviceConnected(comm, () => {
  void loadTouchbarConfig();
});
</script>

<style lang="scss" module>
.page {
  width: 100%;
}

.shellCard,
.visualCard,
.editorCard,
.segmentEditor {
  :global(.ant-card-body) {
    background:
      radial-gradient(circle at top, rgba(47, 196, 255, 0.09), transparent 30%),
      rgba(10, 14, 19, 0.94);
  }
}

.toolbar {
  display: flex;
  justify-content: space-between;
  gap: 12px;
  flex-wrap: wrap;
}

.overview {
  margin-top: 12px;
}

.alert {
  margin-top: 12px;
}

.workspace {
  display: grid;
  grid-template-columns: minmax(0, 1.1fr) minmax(340px, 0.9fr);
  gap: 16px;
}

.stripShell {
  padding: 28px 18px 20px;
}

.strip {
  position: relative;
  height: 148px;
  border-radius: 999px;
  border: 1px solid rgba(74, 96, 121, 0.85);
  background:
    linear-gradient(180deg, rgba(18, 25, 35, 0.98), rgba(8, 12, 19, 0.98));
  box-shadow:
    inset 0 0 0 1px rgba(255, 255, 255, 0.02),
    0 16px 32px rgba(0, 0, 0, 0.28);
  overflow: hidden;
}

.segmentBand,
.entryBand {
  position: absolute;
  top: 30px;
  height: 88px;
  border-radius: 999px;
}

.entryBand {
  top: 38px;
  height: 72px;
  opacity: 0.95;
}

.touchPoint {
  position: absolute;
  top: 50%;
  width: 58px;
  height: 58px;
  margin-left: -29px;
  transform: translateY(-50%);
  border-radius: 999px;
  border: 2px solid rgba(117, 145, 171, 0.35);
  background: linear-gradient(180deg, rgba(32, 41, 55, 1), rgba(13, 18, 25, 1));
  color: #eaf8ff;
  display: flex;
  align-items: center;
  justify-content: center;
  cursor: default;
}

.pointIdle {
  border-color: rgba(117, 145, 171, 0.35);
}

.pointStrip {
  border-color: rgba(97, 188, 255, 0.85);
  box-shadow: 0 0 0 1px rgba(97, 188, 255, 0.15);
}

.pointEntry {
  border-color: rgba(255, 185, 99, 0.92);
  box-shadow: 0 0 0 3px rgba(255, 185, 99, 0.16);
}

.pointShared {
  border-color: rgba(138, 230, 190, 0.92);
  box-shadow: 0 0 0 3px rgba(138, 230, 190, 0.12);
}

.pointSharedEntry {
  border-color: rgba(255, 220, 104, 0.92);
  box-shadow:
    0 0 0 3px rgba(255, 220, 104, 0.13),
    inset 0 0 0 1px rgba(129, 243, 217, 0.2);
}

.touchPointIndex {
  font-weight: 700;
  font-size: 14px;
}

.pointMetaRow {
  display: grid;
  grid-template-columns: repeat(6, minmax(0, 1fr));
  gap: 8px;
  margin-top: 8px;
}

.pointMeta {
  padding: 10px 8px;
  border-radius: 12px;
  background: rgba(16, 22, 31, 0.82);
  border: 1px solid rgba(61, 82, 104, 0.65);
  color: #9eb4c7;
  text-align: center;

  strong {
    display: block;
    color: #f0faff;
  }
}

.segmentEditorList {
  margin-top: 18px;
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.segmentSwatch {
  width: 16px;
  height: 16px;
  border-radius: 999px;
  display: inline-block;
  box-shadow: 0 0 12px rgba(255, 255, 255, 0.12);
}

.zoneEditor {
  display: flex;
  flex-direction: column;
  gap: 14px;
}

.zoneRow {
  display: grid;
  grid-template-columns: 92px minmax(0, 1fr);
  gap: 12px;
  align-items: center;
}

.zoneLabel {
  color: #d8edf7;
  font-weight: 600;
}

.pointHint {
  margin-top: 12px;
  color: #89a0b5;
  font-size: 12px;
  line-height: 1.6;
}

.formGrid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 0 14px;
}

.indicatorRow {
  display: grid;
  grid-template-columns: minmax(0, 1fr) minmax(180px, 220px);
  gap: 14px;
  align-items: end;
}

.colorField {
  display: flex;
  flex-direction: column;
  gap: 8px;
  color: #d8e9f7;

  input {
    width: 100%;
    height: 46px;
    padding: 0;
    border: 1px solid rgba(73, 95, 118, 0.72);
    border-radius: 12px;
    background: transparent;
    cursor: pointer;
  }
}

.placeholder {
  color: #7890a5;
}

@media (max-width: 1160px) {
  .workspace {
    grid-template-columns: 1fr;
  }
}

@media (max-width: 900px) {
  .pointMetaRow {
    grid-template-columns: repeat(3, minmax(0, 1fr));
  }

  .formGrid,
  .indicatorRow,
  .zoneRow {
    grid-template-columns: 1fr;
  }
}
</style>
