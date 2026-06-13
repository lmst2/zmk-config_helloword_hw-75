<template>
  <a-row :gutter="[24, 24]" :style="{ marginTop: '24px' }">
    <a-col :xs="24" :md="9">
      <a-card size="small" :title="t('modes-title')">
        <template #extra>
          <a-button size="small" type="primary" @click="addMode" :disabled="modes.length >= capacity">
            {{ t('add-mode') }}
          </a-button>
        </template>
        <a-list :data-source="modes" size="small">
          <template #renderItem="{ item, index }">
            <a-list-item
              :class="{ [$style.modeItem]: true, [$style.active]: index === selectedIndex }"
              @click="selectedIndex = index"
            >
              <a-list-item-meta>
                <template #title>
                  <span :style="{ fontWeight: index === activeIndex ? 'bold' : 'normal' }">
                    {{ item.label || defaultLabel(item.type) }}
                  </span>
                  <a-tag v-if="index === activeIndex" color="blue" style="margin-left: 8px">
                    {{ t('current') }}
                  </a-tag>
                </template>
                <template #description>
                  <a-typography-text type="secondary">
                    {{ typeLabel(item.type) }}
                  </a-typography-text>
                </template>
              </a-list-item-meta>
              <template #actions>
                <a-button size="small" type="link" @click.stop="makeActive(index)">
                  {{ t('activate') }}
                </a-button>
                <a-button size="small" type="link" danger @click.stop="removeMode(index)">
                  {{ t('remove') }}
                </a-button>
              </template>
            </a-list-item>
          </template>
        </a-list>
        <div :style="{ marginTop: '16px' }">
          <a-space>
            <a-button type="primary" :loading="saving" @click="saveModes">{{ t('save-modes') }}</a-button>
            <a-button @click="fetchConfig">{{ t('reload') }}</a-button>
          </a-space>
        </div>
      </a-card>
    </a-col>

    <a-col :xs="24" :md="15">
      <a-card size="small" :title="t('editor-title', { label: currentMode?.label || '-' })" v-if="currentMode">
        <a-form layout="vertical">
          <a-form-item :label="t('label')">
            <a-input v-model:value="currentMode.label" :maxlength="22" />
          </a-form-item>
          <a-form-item :label="t('type')">
            <a-select v-model:value="currentMode.type" @change="onTypeChange">
              <a-select-option :value="MODE_OFF">{{ t('type-off') }}</a-select-option>
              <a-select-option :value="MODE_STATIC">{{ t('type-static') }}</a-select-option>
              <a-select-option :value="MODE_SLIDESHOW">{{ t('type-slideshow') }}</a-select-option>
              <a-select-option :value="MODE_CLOCK_WEATHER">{{ t('type-clock-weather') }}</a-select-option>
            </a-select>
          </a-form-item>
          <a-form-item :label="t('refresh-interval')" v-if="currentMode.type === MODE_SLIDESHOW || currentMode.type === MODE_CLOCK_WEATHER">
            <a-input-number v-model:value="currentMode.refreshIntervalS" :min="30" :max="86400" :step="30" /> s
          </a-form-item>
        </a-form>

        <!-- static editor -->
        <template v-if="currentMode.type === MODE_STATIC">
          <a-divider>{{ t('image') }}</a-divider>
          <static-editor
            :device-size="DEVICE_SIZE"
            :mode-id="currentMode.id"
            :frame-index="0"
            @pushed="onFramePushed(0)"
          />
        </template>

        <!-- slideshow editor -->
        <template v-else-if="currentMode.type === MODE_SLIDESHOW">
          <a-divider>{{ t('slideshow-frames') }}</a-divider>
          <a-form-item :label="t('frame-count')">
            <a-input-number v-model:value="currentMode.frameCount" :min="1" :max="8" />
          </a-form-item>
          <static-editor
            v-for="i in (currentMode.frameCount || 1)"
            :key="`frame-${i - 1}`"
            :device-size="DEVICE_SIZE"
            :mode-id="currentMode.id"
            :frame-index="i - 1"
            :style="{ marginBottom: '16px' }"
            @pushed="onFramePushed(i - 1)"
          />
        </template>

        <!-- clock/weather editor -->
        <template v-else-if="currentMode.type === MODE_CLOCK_WEATHER">
          <a-divider>{{ t('weather-config') }}</a-divider>
          <a-alert v-if="!core.connected" :message="t('helper-offline')" type="warning" show-icon />
          <a-form layout="vertical" v-if="core.coreConfig">
            <a-form-item :label="t('city')">
              <a-input v-model:value="weatherCfg.city" :maxlength="22" />
            </a-form-item>
            <a-form-item :label="t('latitude')">
              <a-input-number v-model:value="weatherCfg.lat" :step="0.01" />
            </a-form-item>
            <a-form-item :label="t('longitude')">
              <a-input-number v-model:value="weatherCfg.lon" :step="0.01" />
            </a-form-item>
            <a-form-item :label="t('refresh-minutes')">
              <a-input-number v-model:value="weatherCfg.refreshMinutes" :min="1" :max="120" />
            </a-form-item>
            <a-space>
              <a-button type="primary" @click="saveWeatherCfg">{{ t('save-core-cfg') }}</a-button>
              <a-button @click="core.refreshWeatherNow">{{ t('refresh-weather') }}</a-button>
            </a-space>
          </a-form>
          <div :style="{ marginTop: '16px' }">
            <a-descriptions size="small" :column="1" bordered>
              <a-descriptions-item :label="t('last-weather')">
                <template v-if="core.lastWeather">
                  {{ core.lastWeather.tempC.toFixed(1) }} °C, icon {{ core.lastWeather.icon }},
                  code {{ core.lastWeather.weatherCode }}
                </template>
                <template v-else>-</template>
              </a-descriptions-item>
              <a-descriptions-item :label="t('last-clock')">
                <template v-if="core.lastClock">
                  {{ padded(core.lastClock.hour) }}:{{ padded(core.lastClock.minute) }}
                  {{ core.lastClock.month }}/{{ core.lastClock.day }}
                </template>
                <template v-else>-</template>
              </a-descriptions-item>
            </a-descriptions>
          </div>
        </template>

        <template v-else>
          <a-empty :description="t('off-hint')" />
        </template>
      </a-card>
      <a-empty v-else :description="t('no-mode-selected')" />
    </a-col>
  </a-row>
</template>

<script lang="ts" setup>
import { computed, onMounted, ref, watch } from 'vue';
import { useI18n } from 'vue-i18n';

import { UsbComm } from '@/proto/comm.proto';
import { useHelperCore } from '@/stores/helperCore';
import { useEinkStore } from '@/stores/eink';
import StaticEditor from '@/components/eink/StaticEditor.vue';

const { t } = useI18n();

const MODE_OFF = UsbComm.EinkModeType.EINK_MODE_OFF;
const MODE_STATIC = UsbComm.EinkModeType.EINK_MODE_STATIC;
const MODE_SLIDESHOW = UsbComm.EinkModeType.EINK_MODE_SLIDESHOW;
const MODE_CLOCK_WEATHER = UsbComm.EinkModeType.EINK_MODE_CLOCK_WEATHER;

const DEVICE_SIZE = { width: 128, height: 296 };

type ModeEntry = {
  id: number;
  type: UsbComm.EinkModeType;
  label: string;
  refreshIntervalS: number;
  frameCount: number;
};

const core = useHelperCore();
const einkStore = useEinkStore();

const modes = ref<ModeEntry[]>([]);
const activeIndex = ref<number>(0);
const selectedIndex = ref<number>(0);
const capacity = computed(() => einkStore.capacity ?? 8);
const saving = ref(false);

const currentMode = computed(() =>
  modes.value[selectedIndex.value] as ModeEntry | undefined);

const weatherCfg = ref({ city: '', lat: 0, lon: 0, refreshMinutes: 10 });

watch(() => core.coreConfig, (cfg) => {
  if (cfg?.weather) {
    weatherCfg.value = {
      city: cfg.weather.city,
      lat: cfg.weather.lat,
      lon: cfg.weather.lon,
      refreshMinutes: cfg.weather.refresh_minutes,
    };
  }
}, { immediate: true, deep: true });

function defaultLabel(type: UsbComm.EinkModeType): string {
  switch (type) {
    case MODE_OFF: return t('type-off');
    case MODE_STATIC: return t('type-static');
    case MODE_SLIDESHOW: return t('type-slideshow');
    case MODE_CLOCK_WEATHER: return t('type-clock-weather');
    default: return '?';
  }
}

function typeLabel(type: UsbComm.EinkModeType): string {
  return defaultLabel(type);
}

function padded(value: number | undefined): string {
  return String(value ?? 0).padStart(2, '0');
}

function ingestConfig(config: UsbComm.IEinkModeConfig | undefined): void {
  if (!config) {
    return;
  }
  modes.value = (config.modes ?? []).map((m) => ({
    id: m.id ?? 0,
    type: (m.type ?? MODE_OFF) as UsbComm.EinkModeType,
    label: m.label ?? '',
    refreshIntervalS: m.refreshIntervalS ?? 0,
    frameCount: m.frameCount ?? 0,
  }));
  activeIndex.value = config.activeIndex ?? 0;
  if (selectedIndex.value >= modes.value.length) {
    selectedIndex.value = 0;
  }
}

async function fetchConfig(): Promise<void> {
  if (!core.connected) {
    return;
  }
  const cfg = await einkStore.getConfig();
  ingestConfig(cfg ?? undefined);
}

function addMode(): void {
  const nextId = (modes.value.reduce((acc, m) => Math.max(acc, m.id), 0) ?? 0) + 1;
  modes.value.push({
    id: nextId,
    type: MODE_STATIC,
    label: `Mode ${nextId}`,
    refreshIntervalS: 600,
    frameCount: 0,
  });
  selectedIndex.value = modes.value.length - 1;
}

function removeMode(index: number): void {
  modes.value.splice(index, 1);
  if (selectedIndex.value >= modes.value.length) {
    selectedIndex.value = Math.max(0, modes.value.length - 1);
  }
  if (activeIndex.value >= modes.value.length) {
    activeIndex.value = 0;
  }
}

async function makeActive(index: number): Promise<void> {
  activeIndex.value = index;
  await einkStore.setActive(index);
}

function onTypeChange(type: UsbComm.EinkModeType): void {
  if (!currentMode.value) {
    return;
  }
  if (type === MODE_STATIC) {
    currentMode.value.frameCount = 1;
  } else if (type === MODE_SLIDESHOW) {
    currentMode.value.frameCount = Math.max(1, currentMode.value.frameCount);
    if (!currentMode.value.refreshIntervalS) {
      currentMode.value.refreshIntervalS = 600;
    }
  } else {
    currentMode.value.frameCount = 0;
  }
}

function onFramePushed(_index: number): void {
  /* Refresh config after first frame push so frame_count reflects storage. */
  fetchConfig();
}

async function saveModes(): Promise<void> {
  saving.value = true;
  try {
    await einkStore.setConfig(modes.value.map((m) => ({
      id: m.id,
      type: m.type,
      label: m.label || undefined,
      refreshIntervalS: m.refreshIntervalS,
      frameCount: m.frameCount,
    })), activeIndex.value);
    await fetchConfig();
  } finally {
    saving.value = false;
  }
}

function saveWeatherCfg(): void {
  core.updateCoreConfig({
    weather: {
      enabled: true,
      provider: 'open-meteo',
      units: 'celsius',
      city: weatherCfg.value.city,
      lat: weatherCfg.value.lat,
      lon: weatherCfg.value.lon,
      refresh_minutes: weatherCfg.value.refreshMinutes,
    },
  });
}

onMounted(fetchConfig);
watch(() => core.keyboardConnected, (connected) => {
  if (connected) {
    fetchConfig();
  }
});
</script>

<style lang="scss" module>
.modeItem {
  cursor: pointer;
  padding: 8px 12px !important;

  &.active {
    background: rgba(24, 144, 255, 0.08);
  }

  &:hover {
    background: rgba(0, 0, 0, 0.04);
  }
}
</style>

<i18n lang="yaml">
zh-Hans:
  modes-title: 模式列表
  add-mode: 新建模式
  current: 当前
  activate: 激活
  remove: 删除
  save-modes: 保存配置
  reload: 从设备重新加载
  editor-title: 编辑 {label}
  label: 名称
  type: 类型
  type-off: 关闭
  type-static: 静态图片
  type-slideshow: 幻灯片
  type-clock-weather: 时间 + 天气
  refresh-interval: 刷新间隔
  image: 图片
  slideshow-frames: 幻灯片帧
  frame-count: 帧数
  weather-config: 天气配置
  helper-offline: 未连接到 helper-core，配置暂不可用
  city: 城市（ASCII）
  latitude: 纬度
  longitude: 经度
  refresh-minutes: 刷新间隔（分钟）
  save-core-cfg: 保存天气设置
  refresh-weather: 立即拉取
  last-weather: 最近天气
  last-clock: 最近时间推送
  off-hint: 关闭模式下墨水屏会显示空白。
  no-mode-selected: 请先选择左侧的一个模式
zh-Hant:
  modes-title: 模式列表
  add-mode: 新建模式
  current: 目前
  activate: 啟用
  remove: 刪除
  save-modes: 保存配置
  reload: 從裝置重新載入
  editor-title: 編輯 {label}
  label: 名稱
  type: 類型
  type-off: 關閉
  type-static: 靜態圖片
  type-slideshow: 幻燈片
  type-clock-weather: 時間 + 天氣
  refresh-interval: 刷新間隔
  image: 圖片
  slideshow-frames: 幻燈片畫面
  frame-count: 畫面數
  weather-config: 天氣配置
  helper-offline: 未連線到 helper-core
  city: 城市（ASCII）
  latitude: 緯度
  longitude: 經度
  refresh-minutes: 刷新間隔（分鐘）
  save-core-cfg: 儲存天氣設定
  refresh-weather: 立即重新整理
  last-weather: 最近天氣
  last-clock: 最近時間推送
  off-hint: 關閉模式下墨水屏會顯示空白。
  no-mode-selected: 請先選擇左側的一個模式
en:
  modes-title: Modes
  add-mode: Add mode
  current: current
  activate: Activate
  remove: Remove
  save-modes: Save modes
  reload: Reload from device
  editor-title: Edit {label}
  label: Label
  type: Type
  type-off: Off
  type-static: Static image
  type-slideshow: Slideshow
  type-clock-weather: Clock + Weather
  refresh-interval: Refresh interval
  image: Image
  slideshow-frames: Slideshow frames
  frame-count: Frames
  weather-config: Weather config
  helper-offline: Not connected to helper-core
  city: City (ASCII)
  latitude: Latitude
  longitude: Longitude
  refresh-minutes: Refresh minutes
  save-core-cfg: Save weather config
  refresh-weather: Refresh now
  last-weather: Latest weather
  last-clock: Latest clock push
  off-hint: In OFF mode the E-Ink stays blank.
  no-mode-selected: Select a mode on the left first
</i18n>
