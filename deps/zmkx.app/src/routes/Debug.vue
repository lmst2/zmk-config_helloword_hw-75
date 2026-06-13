<template>
  <a-space direction="vertical" :size="16" :class="$style.page">
    <a-card title="Device Capture" size="small">
      <a-space wrap>
        <span>Remote Level</span>
        <a-select :value="remoteLevel" style="width: 140px" @update:value="updateRemoteLevel">
          <a-select-option v-for="level in levelOptions" :key="level.value" :value="level.value">
            {{ level.label }}
          </a-select-option>
        </a-select>
        <span>Remote Modules</span>
        <a-select mode="multiple" :value="remoteModules" style="min-width: 360px"
          @update:value="updateRemoteModules">
          <a-select-option v-for="option in moduleOptions" :key="option.value" :value="option.value">
            {{ option.label }}
          </a-select-option>
        </a-select>
        <a-button type="primary" @click="togglePolling">
          {{ debugStore.polling ? 'Stop Capture' : 'Start Capture' }}
        </a-button>
        <a-button @click="debugStore.refreshState">Refresh</a-button>
        <a-button danger @click="debugStore.clearRemoteEvents">Clear Device Buffer</a-button>
      </a-space>
      <a-space wrap :class="$style.meta">
        <span>Dropped: {{ debugStore.droppedCount }}</span>
        <span>Oldest: {{ debugStore.oldestSeq }}</span>
        <span>Newest: {{ debugStore.newestSeq }}</span>
        <span>Polling: {{ debugStore.polling ? 'On' : 'Off' }}</span>
      </a-space>
      <a-space wrap :class="$style.meta">
        <span>Requests: {{ debugStore.requestCount }}</span>
        <span>Responses: {{ debugStore.responseCount }}</span>
        <span>Other Responses: {{ debugStore.otherResponseCount }}</span>
        <span>Last Request: {{ debugStore.lastRequest || '-' }}</span>
        <span>Last Response: {{ debugStore.lastResponse || '-' }}</span>
        <span>Last Other: {{ debugStore.lastOtherResponse || '-' }}</span>
        <span>Pending: {{ debugStore.pendingResponse || '-' }}</span>
        <span>Timeouts: {{ debugStore.timeoutCount }}</span>
        <span>Error: {{ debugStore.lastError || '-' }}</span>
      </a-space>
    </a-card>

    <a-card title="Transport Trace" size="small">
      <a-space wrap>
        <span>TX Messages: {{ comm.transportStats.txMessages }}</span>
        <span>TX Packets: {{ comm.transportStats.txPackets }}</span>
        <span>RX Packets: {{ comm.transportStats.rxPackets }}</span>
        <span>Decoded: {{ comm.transportStats.decodedMessages }}</span>
        <span>Dropped: {{ comm.transportStats.droppedMessages }}</span>
        <span>Overflows: {{ comm.transportStats.overflowCount }}</span>
        <a-button @click="transportTraceExpanded = !transportTraceExpanded">
          {{ transportTraceExpanded ? 'Hide Trace' : 'Show Trace' }}
        </a-button>
        <a-button @click="comm.clearTransportTrace">Clear Trace</a-button>
      </a-space>
      <div v-if="transportTraceExpanded" :class="$style.transport">
        <div v-for="entry in transportTraceText" :key="entry" :class="$style.line">
          {{ entry }}
        </div>
      </div>
      <div v-else :class="$style.placeholder">
        Transport trace is collapsed by default. Expand it only when we need raw HID request/response details.
      </div>
    </a-card>

    <a-card title="View Filter" size="small">
      <a-space wrap>
        <span>View Level</span>
        <a-select :value="debugStore.viewLevel" style="width: 140px"
          @update:value="(value: UsbComm.LogLevel) => debugStore.viewLevel = value">
          <a-select-option v-for="level in levelOptions" :key="level.value" :value="level.value">
            {{ level.label }}
          </a-select-option>
        </a-select>
        <span>Modules</span>
        <a-select mode="multiple" :value="debugStore.moduleFilter" style="min-width: 360px"
          @update:value="(value: number[]) => debugStore.moduleFilter = value">
          <a-select-option v-for="option in moduleOptions" :key="option.value" :value="option.value">
            {{ option.label }}
          </a-select-option>
        </a-select>
        <a-input :value="debugStore.keyword" placeholder="Search rendered log text"
          style="width: 280px" @update:value="(value: string) => debugStore.keyword = value" />
        <a-switch :checked="debugStore.autoScroll"
          @update:checked="(value: boolean) => debugStore.autoScroll = value" />
        <span>Auto Scroll</span>
        <a-button @click="debugStore.clearLocalEvents">Clear Local</a-button>
        <a-button type="primary" @click="debugStore.copyFilteredText">Copy Filtered</a-button>
      </a-space>
    </a-card>

    <a-card title="Snapshots" size="small">
      <template v-if="debugStore.snapshots.length">
        <a-list size="small" :data-source="debugStore.snapshots">
          <template #renderItem="{ item }">
            <a-list-item>
              <code>{{ debugStore.moduleLabel(item.module) }}</code>
              <span :class="$style.snapshot">{{ renderSnapshot(item) }}</span>
            </a-list-item>
          </template>
        </a-list>
      </template>
      <div v-else :class="$style.placeholder">
        No snapshots captured yet.
      </div>
    </a-card>

    <a-card title="Boot Events" size="small">
      <pre v-if="bootText" :class="$style.boot">{{ bootText }}</pre>
      <div v-else :class="$style.placeholder">
        No boot events captured yet.
      </div>
    </a-card>

    <a-card :title="`Events (${debugStore.filteredEvents.length})`" size="small">
      <div ref="logContainer" :class="$style.log">
        <div v-for="event in debugStore.filteredEvents" :key="event.seq" :class="$style.line">
          {{ debugStore.formatEvent(event) }}
        </div>
      </div>
    </a-card>
  </a-space>
</template>

<script lang="ts" setup>
import { computed, nextTick, onMounted, onUnmounted, ref, watch } from 'vue';

import { UsbComm } from '@/proto/comm.proto';
import { useDebugStore } from '@/stores/debug';
import { useUsbComm } from '@/stores/usb';
import { renderSnapshot } from '@/stores/debug_decode';

const debugStore = useDebugStore();
const comm = useUsbComm();
const logContainer = ref<HTMLDivElement>();
const transportTraceExpanded = ref(false);

const moduleOptions = computed(() => debugStore.moduleOptions);
const levelOptions = [
  { value: UsbComm.LogLevel.ERROR, label: 'ERROR' },
  { value: UsbComm.LogLevel.WARN, label: 'WARN' },
  { value: UsbComm.LogLevel.INFO, label: 'INFO' },
  { value: UsbComm.LogLevel.DEBUG, label: 'DEBUG' },
  { value: UsbComm.LogLevel.TRACE, label: 'TRACE' },
];

const allModules = computed(() => moduleOptions.value.map((option) => option.value));

const remoteLevel = computed(() => debugStore.remoteConfig?.minLevel ?? UsbComm.LogLevel.DEBUG);
const remoteModules = computed(() => {
  const mask = debugStore.remoteConfig?.enabledModulesMask;
  if (mask === undefined) {
    return allModules.value;
  }

  return allModules.value.filter((moduleValue) => (mask & (1 << moduleValue)) !== 0);
});

const bootText = computed(() => debugStore.bootEvents.map(debugStore.formatEvent).join('\n'));
const transportTraceText = computed(() =>
  comm.transportTrace
    .slice()
    .reverse()
    .map((trace) => {
      const time = new Date(trace.ts).toLocaleTimeString();
      const details = [
        trace.action ? `action=${trace.action}` : '',
        trace.payload ? `payload=${trace.payload}` : '',
        trace.packetLength !== undefined ? `packet=${trace.packetLength}` : '',
        trace.messageLength !== undefined ? `message=${trace.messageLength}` : '',
        trace.queueLength !== undefined ? `queue=${trace.queueLength}` : '',
        trace.repeatCount && trace.repeatCount > 1 ? `x${trace.repeatCount}` : '',
      ]
        .filter(Boolean)
        .join(' ');

      return `${time} ${trace.kind} | ${trace.summary}${details ? ` | ${details}` : ''}${trace.rawHex ? ` | ${trace.rawHex}` : ''}`;
    }),
);

async function ensurePollingStarted(): Promise<void> {
  if (!comm.device) {
    return;
  }

  await debugStore.startPolling();
}

async function togglePolling(): Promise<void> {
  if (debugStore.polling) {
    debugStore.stopPolling();
    return;
  }

  await ensurePollingStarted();
}

onMounted(() => {
  void ensurePollingStarted();
});

onUnmounted(() => {
  debugStore.stopPolling();
});

watch(
  () => comm.device,
  (device) => {
    if (device) {
      void ensurePollingStarted();
    } else {
      debugStore.stopPolling();
    }
  },
  { immediate: true },
);

async function updateRemoteLevel(value: UsbComm.LogLevel): Promise<void> {
  await debugStore.setRemoteConfig({ minLevel: value });
}

async function updateRemoteModules(values: number[]): Promise<void> {
  const mask = values.reduce((result, value) => result | (1 << value), 0);
  await debugStore.setRemoteConfig({ enabledModulesMask: mask });
}

watch(
  () => debugStore.filteredEvents.length,
  async () => {
    if (!debugStore.autoScroll) {
      return;
    }

    await nextTick();
    if (logContainer.value) {
      logContainer.value.scrollTop = logContainer.value.scrollHeight;
    }
  },
);
</script>

<style lang="scss" module>
.page {
  width: 100%;
}

.meta {
  margin-top: 12px;
}

.snapshot {
  margin-left: 12px;
  font-family: 'Courier New', Courier, monospace;
}

.placeholder {
  color: #6b7280;
  font-family: 'Courier New', Courier, monospace;
  white-space: pre-wrap;
}

.boot {
  margin: 0;
  white-space: pre-wrap;
  word-break: break-word;
  font-family: 'Courier New', Courier, monospace;
}

.transport,
.log {
  max-height: 520px;
  overflow: auto;
  padding: 12px;
  background: #111827;
  color: #e5e7eb;
  border-radius: 8px;
  font-family: 'Courier New', Courier, monospace;
  white-space: pre-wrap;
  word-break: break-word;
}

.line + .line {
  margin-top: 8px;
}
</style>
