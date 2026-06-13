import { ref } from 'vue';
import { defineStore } from 'pinia';

import { UsbComm } from '@/proto/comm.proto';
import { useHelperCore } from './helperCore';

export const useKnobCalibrationStore = defineStore('knobCalibration', () => {
  const zeroOffset = ref<number>(0);
  const direction = ref<number>(0);
  const calibrated = ref<boolean>(false);

  const core = useHelperCore();

  function ingest(res: UsbComm.IKnobCalibration | null | undefined): void {
    if (!res) {
      return;
    }
    if (typeof res.zeroOffset === 'number') {
      zeroOffset.value = res.zeroOffset;
    }
    if (typeof res.direction === 'number') {
      direction.value = res.direction;
    }
    if (typeof res.calibrated === 'boolean') {
      calibrated.value = res.calibrated;
    }
  }

  async function fetch(): Promise<void> {
    const res = await core.sendViaCore({
      action: UsbComm.Action.KNOB_GET_CALIBRATION,
      nop: {},
    });
    if (res.payload === 'knobCalibration') {
      ingest(res.knobCalibration);
    }
  }

  async function setCalibration(offset: number, dir: number): Promise<void> {
    const res = await core.sendViaCore({
      action: UsbComm.Action.KNOB_SET_CALIBRATION,
      knobCalibration: { zeroOffset: offset, direction: dir },
    });
    if (res.payload === 'knobCalibration') {
      ingest(res.knobCalibration);
    }
  }

  async function resetAuto(): Promise<void> {
    const res = await core.sendViaCore({
      action: UsbComm.Action.KNOB_SET_CALIBRATION,
      knobCalibration: { resetAuto: true },
    });
    if (res.payload === 'knobCalibration') {
      ingest(res.knobCalibration);
    }
  }

  return {
    zeroOffset,
    direction,
    calibrated,
    fetch,
    setCalibration,
    resetAuto,
  };
});
