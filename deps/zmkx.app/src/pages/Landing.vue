<template>
  <div :class="$style.landing">
    <div :class="$style.logo">ZMKX</div>

    <a-space direction="vertical" align="center" :class="$style.connect">
      <a-spin size="large" />
      <div :class="$style.title">{{ t('connecting') }}</div>
      <div :class="$style.hint" v-html="t('hint')" />
    </a-space>

    <footer :class="$style.footer">
      Copyright © XiNGRZ 2022-2023
    </footer>
  </div>
</template>

<script lang="ts" setup>
import { useI18n } from 'vue-i18n';

import { useUsbComm } from '@/stores/usb';

const { t } = useI18n();

// Touch the store so the helper-core WebSocket connects; App.vue advances to the
// main page automatically once the 中枢 is reachable (device becomes truthy).
useUsbComm();
</script>

<style lang="scss" module>
.landing {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  height: 100vh;
}

.logo {
  height: 120px;
  font-family: 'Courier New', Courier, monospace;
  font-size: 120px;
  line-height: 120px;
  text-align: center;
  margin-top: 16px;
}

.connect {
  margin-top: 50px;
  width: 400px;
  text-align: center;
}

.title {
  margin-top: 16px;
  font-size: 16px;
}

.hint {
  color: #999;
  font-size: 90%;
}

.footer {
  position: absolute;
  bottom: 16px;
  font-size: 80%;
  color: #999;
}
</style>

<i18n lang="yaml">
zh-Hans:
  connecting: 正在连接「中枢」…
  hint: |
    <p>请确保 HW-75「中枢」正在后台运行。</p>
zh-Hant:
  connecting: 正在連線「中樞」…
  hint: |
    <p>請確保 HW-75「中樞」正在背景執行。</p>
en:
  connecting: Connecting to the host…
  hint: |
    <p>Make sure the HW-75 host (中枢) is running in the background.</p>
</i18n>
