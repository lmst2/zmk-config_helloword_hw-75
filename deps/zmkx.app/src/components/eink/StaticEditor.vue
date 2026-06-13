<template>
  <a-card size="small" :title="title">
    <div v-if="!selected" :class="$style.selector">
      <a-upload-dragger accept="image/*" :custom-request="handleFile" :show-upload-list="false">
        <p class="ant-upload-drag-icon">
          <picture-outlined />
        </p>
        <p class="ant-upload-text">{{ t('drag-hint') }}</p>
      </a-upload-dragger>
    </div>
    <a-row type="flex" :gutter="[16, 16]" v-else>
      <a-col flex="0">
        <img :src="preview" :class="$style.preview" />
      </a-col>
      <a-col flex="auto">
        <a-form layout="vertical">
          <a-form-item :label="t('threshold')">
            <a-slider v-model:value="threshold" :disabled="uploading" />
          </a-form-item>
          <a-space direction="vertical">
            <a-checkbox v-model:checked="inverted" :disabled="uploading">{{ t('inverted') }}</a-checkbox>
            <a-checkbox v-model:checked="dither" :disabled="uploading">{{ t('dither') }}</a-checkbox>
          </a-space>
          <a-space :style="{ marginTop: '16px' }">
            <a-button type="primary" :loading="uploading" @click="push">{{ t('push-frame') }}</a-button>
            <a-button type="text" @click="clear" :disabled="uploading">{{ t('clear') }}</a-button>
          </a-space>
        </a-form>
      </a-col>
    </a-row>
  </a-card>
</template>

<script lang="ts" setup>
import { computed, ref, watch } from 'vue';
import { useI18n } from 'vue-i18n';
import { PictureOutlined } from '@ant-design/icons-vue';

import { binarize, centerOf, scaleInside, toBits } from '@/utils/graphic';
import { useEinkStore } from '@/stores/eink';

const props = defineProps<{
  deviceSize: { width: number; height: number };
  modeId: number;
  frameIndex: number;
}>();

const emit = defineEmits<{ (e: 'pushed'): void }>();

const { t } = useI18n();
const einkStore = useEinkStore();

const selected = ref<File>();
const threshold = ref(50);
const inverted = ref(false);
const dither = ref(true);
const uploading = ref(false);
const preview = ref<string>();

const canvas = document.createElement('canvas');
canvas.width = props.deviceSize.width;
canvas.height = props.deviceSize.height;

const target = ref<ImageBitmap>();

function handleFile({ file }: { file: File }): void {
  selected.value = file;
}

function clear(): void {
  selected.value = undefined;
  target.value = undefined;
  preview.value = undefined;
  threshold.value = 50;
  inverted.value = false;
}

watch([selected, threshold, inverted, dither], async ([file, t, inv, dit]) => {
  if (!file) {
    target.value = undefined;
    return;
  }
  const source = await createImageBitmap(file);
  target.value = await binarize(source, scaleInside(canvas, source), t as number, inv as boolean, dit as boolean);
});

watch(target, (bmp) => {
  const ctx = canvas.getContext('2d')!;
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  ctx.fillStyle = 'white';
  ctx.fillRect(0, 0, canvas.width, canvas.height);
  if (bmp) {
    const rect = scaleInside(canvas, bmp);
    const ccenter = centerOf(canvas);
    const tcenter = centerOf(rect);
    ctx.save();
    ctx.translate(ccenter.x, ccenter.y);
    ctx.drawImage(bmp, -tcenter.x, -tcenter.y, rect.width, rect.height);
    ctx.restore();
  }
  preview.value = canvas.toDataURL();
});

const title = computed(() => `${t('frame-number', { index: props.frameIndex + 1 })}`);

async function push(): Promise<void> {
  const ctx = canvas.getContext('2d')!;
  const bits = toBits(ctx.getImageData(0, 0, canvas.width, canvas.height));
  uploading.value = true;
  try {
    await einkStore.pushFrame(props.modeId, props.frameIndex, bits);
    emit('pushed');
  } finally {
    uploading.value = false;
  }
}
</script>

<style lang="scss" module>
.selector {
  height: 180px;

  :global(.ant-upload-btn) {
    display: flex !important;
    flex-direction: column;
    justify-content: center;
  }
  :global(.ant-upload-text) {
    padding: 16px;
  }
}

.preview {
  width: 64px;
  height: 148px;
  border: 1px solid #ccc;
}
</style>

<i18n lang="yaml">
zh-Hans:
  frame-number: 第 {index} 帧
  drag-hint: 拖放图片或点击选择
  threshold: 阈值
  inverted: 反色
  dither: 抖动
  push-frame: 推送到设备
  clear: 重新选择
zh-Hant:
  frame-number: 第 {index} 幀
  drag-hint: 拖放圖片或點擊選擇
  threshold: 閾值
  inverted: 反相
  dither: 抖動
  push-frame: 推送到裝置
  clear: 重新選擇
en:
  frame-number: Frame {index}
  drag-hint: Drop image or click to choose
  threshold: Threshold
  inverted: Inverted
  dither: Dither
  push-frame: Push to device
  clear: Reset
</i18n>
