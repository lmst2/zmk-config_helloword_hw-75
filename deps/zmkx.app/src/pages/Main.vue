<template>
  <a-layout :class="$style.container">
    <a-layout-sider v-model:collapsed="collapsed" breakpoint="lg" :style="{ background: 'none' }">
      <div :class="$style.logo" v-show="!collapsed">ZMKX</div>
      <div :class="$style.device">
        <a-button v-if="collapsed" shape="circle" size="large" @click="comm.close">
          <template #icon>
            <disconnect-outlined />
          </template>
        </a-button>
        <a-button v-else shape="round" size="large" block @click="comm.close">
          {{ t('disconnect') }}
        </a-button>
      </div>
      <a-menu :selected-keys="page" mode="inline" :class="$style.nav" @select="navigate">
        <a-menu-item key="about">
          <template #icon>
            <info-circle-outlined />
          </template>
          {{ t('about') }}
        </a-menu-item>
        <a-menu-item key="keyboard" v-if="showKeyboardPage">
          <template #icon>
            <appstore-outlined />
          </template>
          {{ t('keyboard') }}
        </a-menu-item>
        <a-menu-item key="touchbar" v-if="!!version?.features?.touchbarConfig">
          <template #icon>
            <deployment-unit-outlined />
          </template>
          {{ t('touchbar') }}
        </a-menu-item>
        <a-menu-item key="rgb" v-if="!version?.features || version.features.rgb">
          <template #icon>
            <alert-outlined />
          </template>
          {{ t('rgb') }}
        </a-menu-item>
        <a-menu-item key="eink" v-if="!!(version?.features?.eink || version?.features?.einkModes)">
          <template #icon>
            <project-outlined />
          </template>
          {{ t('eink') }}
        </a-menu-item>
        <a-menu-item key="motor" v-if="!!version?.features?.knob">
          <template #icon>
            <loading-outlined v-if="knobStore.knobConfig?.demo" />
            <loading3-quarters-outlined v-else />
          </template>
          {{ t('motor') }}
        </a-menu-item>
        <a-menu-item key="debug" v-if="!version?.features || version.features.debugLog">
          <template #icon>
            <bug-outlined />
          </template>
          {{ t('debug') }}
        </a-menu-item>
      </a-menu>
    </a-layout-sider>
    <a-layout-content :class="{ [$style.main]: true, [$style.collapsed]: collapsed }">
      <router-view v-slot="{ Component }">
        <component :is="Component" />
      </router-view>
    </a-layout-content>
  </a-layout>
</template>

<script lang="ts" setup>
import { computed, ref, watch } from 'vue';
import { storeToRefs } from 'pinia';
import { useRouter } from 'vue-router';
import { useI18n } from 'vue-i18n';
import {
  DisconnectOutlined,
  InfoCircleOutlined,
  AppstoreOutlined,
  AlertOutlined,
  DeploymentUnitOutlined,
  ProjectOutlined,
  LoadingOutlined,
  Loading3QuartersOutlined,
  BugOutlined,
} from '@ant-design/icons-vue';
import { useUsbComm, onDeviceConnected } from '@/stores/usb';
import { useVersionStore } from '@/stores/version';
import { useKnobStore } from '@/stores/knob';
import { useTouchbarStore } from '@/stores/touchbar';
import { useFunctionSlotStore } from '@/stores/function-slots';

const { t } = useI18n();

const collapsed = ref(false);

const router = useRouter();

const page = computed(() => [router.currentRoute.value.name]);
function navigate({ key }: { key: string }) {
  router.replace({ name: key });
}

const comm = useUsbComm();
const versionStore = useVersionStore();
const knobStore = useKnobStore();
const touchbarStore = useTouchbarStore();
const functionSlotStore = useFunctionSlotStore();

const { version } = storeToRefs(versionStore);
const { device } = storeToRefs(comm);

type DeviceKind = 'keyboard' | 'dynamic' | 'unknown';

const deviceKind = computed<DeviceKind>(() => {
  const productName = device.value?.productName?.toLowerCase() ?? '';
  if (productName.includes('dynamic')) {
    return 'dynamic';
  }
  if (productName.includes('keyboard')) {
    return 'keyboard';
  }
  return 'unknown';
});

const showKeyboardPage = computed(() => deviceKind.value !== 'dynamic');

const availablePageKeys = computed(() => {
  const pages: string[] = ['about'];

  if (showKeyboardPage.value) {
    pages.push('keyboard');
  }
  if (version.value?.features?.touchbarConfig) {
    pages.push('touchbar');
  }
  if (!version.value?.features || version.value.features.rgb) {
    pages.push('rgb');
  }
  if (version.value?.features?.eink || version.value?.features?.einkModes) {
    pages.push('eink');
  }
  if (version.value?.features?.knob) {
    pages.push('motor');
  }
  if (!version.value?.features || version.value.features.debugLog) {
    pages.push('debug');
  }

  return pages;
});

watch([deviceKind, version], () => {
  const currentPage = String(router.currentRoute.value.name ?? 'about');
  if (!availablePageKeys.value.includes(currentPage)) {
    router.replace({ name: availablePageKeys.value[0] ?? 'about' });
  }
}, { immediate: true });

onDeviceConnected(comm, async () => {
  await versionStore.getVersion();
  if (versionStore.version?.features?.touchbarConfig) {
    await touchbarStore.getTouchbarConfig();
  }
  if (versionStore.version?.features?.functionSlots) {
    await functionSlotStore.refresh();
    functionSlotStore.startEventPolling();
  }
});
</script>

<style lang="scss" module>
.container {
  max-width: 1800px;
  margin: 0 auto;
  background: none;
}

.logo {
  height: 64px;
  font-family: 'Courier New', Courier, monospace;
  font-size: 60px;
  line-height: 64px;
  text-align: center;
  margin-top: 16px;
}

.device {
  padding: 32px 16px 48px;
  text-align: center;
}

.nav {
  padding-bottom: 100px;
  background: none;

  :global(.ant-menu-item),
  :global(.ant-menu-submenu-title) {
    height: 56px;
    line-height: 56px;
  }

  :global(.ant-menu-item-icon),
  :global(.ant-menu-title-content) {
    font-size: 18px;
    user-select: none;
  }
}

.main {
  padding: 32px 64px;

  &.collapsed {
    padding: 24px 16px;
  }
}
</style>

<i18n lang="yaml">
zh-Hans:
  disconnect: 断开设备
  about: 关于
  keyboard: 键盘总览
  touchbar: TouchBar
  rgb: 灯效
  eink: 墨水屏
  motor: 旋钮
  debug: 调试
zh-Hant:
  disconnect: 斷開裝置
  about: 關於
  keyboard: 鍵盤總覽
  touchbar: TouchBar
  rgb: 燈效
  eink: 墨水屏
  motor: 旋鈕
  debug: 偵錯
en:
  disconnect: Disconnect
  about: About
  keyboard: Keyboard Overview
  touchbar: TouchBar
  rgb: RGB
  eink: E-Ink
  motor: Knob
  debug: Debug
</i18n>
