import { UsbComm } from '@/proto/comm.proto';

type LogEvent = UsbComm.ILogEvent;
type LogSnapshot = UsbComm.ILogSnapshot;

const touchbarModeLabels = ['Pan', 'App Switch', 'Desktop Switch'];
const touchbarPhaseLabels = ['Init', 'Mode Cycle', 'Touch Start', 'Active', 'Released'];
const usbErrLabels = ['unknown', 'varint', 'delimited', 'tag', 'field', 'submessage', 'encoding'];
const rgbEffectLabels = ['Solid', 'Breathe', 'Spectrum', 'Swirl', 'Rainbow Sweep', 'Reactive', 'Aurora', 'Ripple', 'Static'];

function u8(value: number | null | undefined, shift: number): number {
  return ((value ?? 0) >>> shift) & 0xff;
}

function nibble(value: number | null | undefined, shift: number): number {
  return ((value ?? 0) >>> shift) & 0x0f;
}

function u16lo(value: number | null | undefined): number {
  return (value ?? 0) & 0xffff;
}

function u16hi(value: number | null | undefined): number {
  return ((value ?? 0) >>> 16) & 0xffff;
}

function s16(raw: number): number {
  return raw >= 0x8000 ? raw - 0x10000 : raw;
}

function s8(raw: number): number {
  return raw >= 0x80 ? raw - 0x100 : raw;
}

function s16lo(value: number | null | undefined): number {
  return s16(u16lo(value));
}

function s16hi(value: number | null | undefined): number {
  return s16(u16hi(value));
}

function hex8(value: number): string {
  return `0x${value.toString(16).padStart(2, '0')}`;
}

function hexMask(value: number): string {
  return `0x${value.toString(16).padStart(8, '0')}`;
}

export function levelLabel(level: UsbComm.LogLevel): string {
  switch (level) {
    case UsbComm.LogLevel.ERROR:
      return 'ERROR';
    case UsbComm.LogLevel.WARN:
      return 'WARN';
    case UsbComm.LogLevel.INFO:
      return 'INFO';
    case UsbComm.LogLevel.DEBUG:
      return 'DEBUG';
    case UsbComm.LogLevel.TRACE:
      return 'TRACE';
    default:
      return `${level}`;
  }
}

export function moduleLabel(module: UsbComm.LogModule): string {
  switch (module) {
    case UsbComm.LogModule.SYSTEM:
      return 'System';
    case UsbComm.LogModule.USB_COMM:
      return 'USB Comm';
    case UsbComm.LogModule.KSCAN:
      return 'KScan';
    case UsbComm.LogModule.TOUCHBAR:
      return 'TouchBar';
    case UsbComm.LogModule.BEHAVIOR:
      return 'Behavior';
    case UsbComm.LogModule.HID:
      return 'HID';
    case UsbComm.LogModule.RGB:
      return 'RGB';
    case UsbComm.LogModule.INDICATOR:
      return 'Indicator';
    case UsbComm.LogModule.SETTINGS:
      return 'Settings';
    case UsbComm.LogModule.EINK:
      return 'E-Ink';
    case UsbComm.LogModule.KNOB:
      return 'Knob';
    case UsbComm.LogModule.HELPER_CORE:
      return 'Helper';
    default:
      return `Module ${module}`;
  }
}

function actionLabel(action: number): string {
  return UsbComm.Action[action] ?? `Action ${action}`;
}

function touchbarModeLabel(mode: number): string {
  return touchbarModeLabels[mode] ?? `Mode ${mode}`;
}

function touchbarPhaseLabel(phase: number): string {
  return touchbarPhaseLabels[phase] ?? `Phase ${phase}`;
}

function usbErrLabel(code: number): string {
  return usbErrLabels[code] ?? `err-${code}`;
}

function rgbEffectLabel(effect: number): string {
  return rgbEffectLabels[effect] ?? `Effect ${effect}`;
}

function stackUsedPercent(freeBytes: number, stackSize: number): number {
  if (stackSize <= 0) {
    return 0;
  }

  const usedBytes = Math.max(0, stackSize - freeBytes);
  return Math.min(100, Math.round((usedBytes * 100) / stackSize));
}

function touchbarFlagsText(flags: number): string {
  const parts: string[] = [];
  if (flags & 0x01) parts.push('touching');
  if (flags & 0x02) parts.push('gesture-active');
  if (flags & 0x04) parts.push('desktop-seek');
  if (flags & 0x08) parts.push('release-pending');
  return parts.length ? parts.join(', ') : 'idle';
}

function renderEventMessage(event: LogEvent): string {
  const data0 = event.data0 ?? 0;
  const data1 = event.data1 ?? 0;

  switch (event.eventId) {
    case UsbComm.LogEventId.LOG_EVENT_SYSTEM_INIT:
      return `Diagnostic log initialized with event ring ${u16lo(data0)} and boot buffer ${u16hi(data0)}.`;
    case UsbComm.LogEventId.LOG_EVENT_SYSTEM_LOG_CONFIG:
      return `Diagnostic capture config changed: minimum level ${levelLabel(u16lo(data0) as UsbComm.LogLevel)}, module mask ${hexMask(u16hi(data0))}.`;
    case UsbComm.LogEventId.LOG_EVENT_USB_INIT:
      return `USB comm initialized with TX ${u16lo(data0)} bytes, RX ${u16hi(data0)} bytes, bytes field ${data1} bytes.`;
    case UsbComm.LogEventId.LOG_EVENT_USB_REQUEST:
      return `USB comm received request ${actionLabel(data0)} (${data0}).`;
    case UsbComm.LogEventId.LOG_EVENT_USB_RX_OVERFLOW:
      return `USB comm RX overflowed: write index ${u16lo(data0)}, incoming chunk ${u16hi(data0)}, buffer limit ${data1}.`;
    case UsbComm.LogEventId.LOG_EVENT_USB_PACKET_HEADER_INVALID:
      return `USB comm packet header was invalid: header ${u16lo(data0)}, packet length ${u16hi(data0)}.`;
    case UsbComm.LogEventId.LOG_EVENT_USB_DECODE_FAIL:
      return `USB comm failed to decode host message of ${u16lo(data0)} bytes: ${usbErrLabel(u16hi(data0))}.`;
    case UsbComm.LogEventId.LOG_EVENT_USB_ENCODE_FAIL:
      return `USB comm failed to encode response for ${actionLabel(u16lo(data0))}: ${usbErrLabel(u16hi(data0))}.`;
    case UsbComm.LogEventId.LOG_EVENT_USB_RESPONSE_LARGE:
      return `USB comm response for ${actionLabel(u16lo(data0))} encoded to ${u16hi(data0)} bytes, larger than TX buffer ${data1}.`;
    case UsbComm.LogEventId.LOG_EVENT_USB_BYTES_OVERFLOW:
      return `USB comm bytes field overflowed: incoming ${data0} bytes, limit ${data1}.`;
    case UsbComm.LogEventId.LOG_EVENT_USB_BYTES_DECODE_FAIL:
      return `USB comm bytes field decode failed after ${u16lo(data0)} bytes: ${usbErrLabel(u16hi(data0))}.`;
    case UsbComm.LogEventId.LOG_EVENT_USB_TX_WAIT_TIMEOUT:
      return `USB comm timed out after ${u16lo(data0)} ms waiting for the IN endpoint while ${u16hi(data0)} bytes still needed sending.`;
    case UsbComm.LogEventId.LOG_EVENT_USB_TX_WRITE_FAIL:
      return `USB comm HID write failed with error ${s16lo(data0)} while writing ${u16hi(data0)} bytes.`;
    case UsbComm.LogEventId.LOG_EVENT_USB_TX_SHORT_WRITE:
      return `USB comm HID write was short: requested ${u16lo(data0)} bytes, sent ${u16hi(data0)}.`;
    case UsbComm.LogEventId.LOG_EVENT_KSCAN_TOUCH_MAP:
      return `TouchBar logical map is 0=r${nibble(data0, 0)}/c${nibble(data0, 4)} 1=r${nibble(data0, 8)}/c${nibble(data0, 12)} 2=r${nibble(data0, 16)}/c${nibble(data0, 20)} 3=r${nibble(data0, 24)}/c${nibble(data0, 28)} 4=r${nibble(data1, 0)}/c${nibble(data1, 4)} 5=r${nibble(data1, 8)}/c${nibble(data1, 12)}.`;
    case UsbComm.LogEventId.LOG_EVENT_KSCAN_TOUCH_STATE:
      return `TouchBar state changed: raw ${hex8(u8(data0, 0))}, debounced ${hex8(u8(data0, 8))}, logical ${hex8(u8(data0, 16))}.`;
    case UsbComm.LogEventId.LOG_EVENT_BEHAVIOR_TOUCHBAR_MODE:
      return 'TouchBar mode behavior was pressed.';
    case UsbComm.LogEventId.LOG_EVENT_HID_INIT:
      return `Mouse helper initialized on HID_${data0}.`;
    case UsbComm.LogEventId.LOG_EVENT_HID_INIT_FAIL:
      return `Mouse helper failed to initialize because HID_${data0} was not found.`;
    case UsbComm.LogEventId.LOG_EVENT_HID_WHEEL_SEND:
      return `Mouse helper sent wheel report with direction ${s8(u8(data0, 0))}.`;
    case UsbComm.LogEventId.LOG_EVENT_HID_WHEEL_SEND_FAIL:
      return `Mouse helper failed to send wheel report: direction ${s8(u8(data0, 0))}, pressed ${u8(data0, 8) === 1 ? 'yes' : 'no'}, error ${s16lo(data1)}.`;
    case UsbComm.LogEventId.LOG_EVENT_RGB_CONTROL:
      return `RGB control command executed: ${data0}.`;
    case UsbComm.LogEventId.LOG_EVENT_RGB_SET_STATE:
      return `RGB state updated: on ${u8(data0, 0) === 1 ? 'yes' : 'no'}, color ${u8(data0, 8) === 1 ? 'present' : 'unchanged'}, effect ${u8(data0, 16)}, speed ${u8(data1, 0)}, HSB ${u8(data1, 8)}/${u8(data1, 16)}/${u8(data1, 24)}.`;
    case UsbComm.LogEventId.LOG_EVENT_RGB_SET_INDICATOR:
      return `RGB indicator settings updated: enable flag ${u8(data0, 0)}, enable value ${u8(data0, 8)}, active brightness ${u8(data1, 0)}, inactive brightness ${u8(data1, 8)}.`;
    case UsbComm.LogEventId.LOG_EVENT_INDICATOR_INIT:
      return `Status indicator initialized: enabled ${u8(data0, 0) === 1 ? 'yes' : 'no'}, active brightness ${u8(data0, 8)}, inactive brightness ${u8(data0, 16)}, keyboard active ${u8(data0, 24) === 1 ? 'yes' : 'no'}.`;
    case UsbComm.LogEventId.LOG_EVENT_INDICATOR_ENABLE:
      return `Status indicator enable changed to ${u8(data0, 0) === 1 ? 'on' : 'off'}; active brightness ${u8(data0, 8)}, inactive brightness ${u8(data0, 16)}.`;
    case UsbComm.LogEventId.LOG_EVENT_INDICATOR_BRIGHTNESS_ACTIVE:
      return `Status indicator active brightness changed to ${u8(data0, 0)}; inactive brightness is ${u8(data0, 8)}.`;
    case UsbComm.LogEventId.LOG_EVENT_INDICATOR_BRIGHTNESS_INACTIVE:
      return `Status indicator inactive brightness changed to ${u8(data0, 8)}; active brightness is ${u8(data0, 0)}.`;
    case UsbComm.LogEventId.LOG_EVENT_TOUCHBAR_INIT:
      return `TouchBar polling initialized with ${data0} ms interval.`;
    case UsbComm.LogEventId.LOG_EVENT_TOUCHBAR_INIT_FAIL:
      return 'TouchBar initialization failed because the chosen kscan device was not ready.';
    case UsbComm.LogEventId.LOG_EVENT_TOUCHBAR_POLL_ERROR:
      return `TouchBar poll failed with error ${s16lo(data0)}.`;
    case UsbComm.LogEventId.LOG_EVENT_TOUCHBAR_POLL_RECOVERED:
      return `TouchBar poll recovered after previous error ${s16lo(data0)}.`;
    case UsbComm.LogEventId.LOG_EVENT_TOUCHBAR_MODE_CYCLE:
      return `TouchBar mode changed from ${touchbarModeLabel(u8(data0, 0))} to ${touchbarModeLabel(u8(data0, 8))}.`;
    case UsbComm.LogEventId.LOG_EVENT_TOUCHBAR_TOUCH_START:
      return `${touchbarModeLabel(u8(data0, 0))} touch started with mask ${hex8(u8(data0, 8))}, segment ${u8(data0, 16)}, touches ${u8(data0, 24)}, position ${s16lo(data1)}.`;
    case UsbComm.LogEventId.LOG_EVENT_TOUCHBAR_ACTIVATION_WAIT:
      return `TouchBar is waiting for activation: elapsed ${u16lo(data0)} ms of required ${u16hi(data0)} ms, mask ${hex8(data1 & 0xff)}.`;
    case UsbComm.LogEventId.LOG_EVENT_TOUCHBAR_GESTURE_ACTIVE:
      return `${touchbarModeLabel(u8(data0, 0))} gesture became active at position ${s16lo(data1)} with ${u8(data0, 8)} touches and mask ${hex8(u8(data0, 16))}.`;
    case UsbComm.LogEventId.LOG_EVENT_TOUCHBAR_PAN_WHEEL:
      return `TouchBar pan emitted mouse wheel step ${s8(u8(data0, 0))}.`;
    case UsbComm.LogEventId.LOG_EVENT_TOUCHBAR_APP_SWITCH_STEP:
      return `TouchBar emitted App Switch step ${s8(u8(data0, 0))}.`;
    case UsbComm.LogEventId.LOG_EVENT_TOUCHBAR_DESKTOP_SWITCH_STEP:
      return `TouchBar emitted Desktop Switch step ${s8(u8(data0, 0))}.`;
    case UsbComm.LogEventId.LOG_EVENT_TOUCHBAR_EDGE_HOLD_ARM:
      return `TouchBar armed edge repeat with direction ${s8(u8(data0, 0))} at position ${s16lo(data1)}.`;
    case UsbComm.LogEventId.LOG_EVENT_TOUCHBAR_DESKTOP_EDGE_ARM:
      return `TouchBar armed desktop edge hold with direction ${s8(u8(data0, 0))} at position ${s16lo(data1)}.`;
    case UsbComm.LogEventId.LOG_EVENT_TOUCHBAR_APP_RELEASE_GUARD:
      return `TouchBar App Switch release guard is active for another ${data0} ms.`;
    case UsbComm.LogEventId.LOG_EVENT_TOUCHBAR_DESKTOP_FINALIZE:
      return `TouchBar finalized desktop swipe with displacement ${s16lo(data0)}.`;
    case UsbComm.LogEventId.LOG_EVENT_TOUCHBAR_DESKTOP_HOLD_WAIT:
      return `TouchBar is waiting for desktop hold: elapsed ${u16lo(data0)} ms of required ${u16hi(data0)} ms.`;
    case UsbComm.LogEventId.LOG_EVENT_TOUCHBAR_DESKTOP_SEEK:
      return `TouchBar entered desktop seek mode with anchor position ${s16lo(data0)}.`;
    case UsbComm.LogEventId.LOG_EVENT_TOUCHBAR_RELEASE_PENDING:
      return `${touchbarModeLabel(u8(data0, 0))} touch lost contact and entered release grace for ${data1} ms with mask ${hex8(u8(data0, 8))}.`;
    case UsbComm.LogEventId.LOG_EVENT_TOUCHBAR_RELEASE_GRACE:
      return `TouchBar release grace is still active: elapsed ${u16lo(data0)} ms of ${u16hi(data0)} ms, current mask ${hex8(data1 & 0xff)}.`;
    case UsbComm.LogEventId.LOG_EVENT_TOUCHBAR_TOUCH_END:
      return `${touchbarModeLabel(u8(data0, 0))} gesture ended with mask ${hex8(u8(data0, 8))}, segment ${u8(data0, 16)}, touches ${u8(data0, 24)}.`;
    case UsbComm.LogEventId.LOG_EVENT_TOUCHBAR_APP_RELEASE_JITTER:
      return `TouchBar observed App Switch release-order jitter and armed a ${data0} ms settle guard for mask ${hex8(data1 & 0xff)}.`;
    case 110:
    case UsbComm.LogEventId.LOG_EVENT_USB_STACK_WATERMARK: {
      const action = u16lo(data0);
      const freeBytes = u16hi(data0);
      const stackSize = u16lo(data1);
      const stage = u8(data1, 16);
      const threshold = u8(data1, 24);
      const usedPercent = stackUsedPercent(freeBytes, stackSize);
      return `USB comm stack crossed the ${threshold}% used watermark at stage ${stage} while handling ${actionLabel(action)}: ${freeBytes} B free of ${stackSize} B (${usedPercent}% used).`;
    }
    case 111:
    case UsbComm.LogEventId.LOG_EVENT_RGB_WORKQ_STACK_WATERMARK: {
      let effect = u16lo(data0);
      const freeBytes = u16hi(data0);
      let stackSize = u16lo(data1);
      let stage = u8(data1, 16);
      let threshold = u8(data1, 24);

      // Older firmware packed RGB stack events as:
      // data0 = pack(stage, free_bytes), data1 = pack(stack_size, effect)
      // Newer firmware packs:
      // data0 = pack(effect, free_bytes), data1 = pack(stack_size, stage|threshold<<8)
      if (threshold === 0 && stage > 2) {
        stage = u16lo(data0);
        effect = u16hi(data1);
      }

      const usedPercent = stackUsedPercent(freeBytes, stackSize);
      if (threshold === 0) {
        return `RGB workqueue stack watermark hit at stage ${stage} while ${rgbEffectLabel(effect)} was active: ${freeBytes} B free of ${stackSize} B (${usedPercent}% used).`;
      }

      return `RGB workqueue stack crossed the ${threshold}% used watermark at stage ${stage} while ${rgbEffectLabel(effect)} was active: ${freeBytes} B free of ${stackSize} B (${usedPercent}% used).`;
    }
    case UsbComm.LogEventId.LOG_EVENT_EINK_MODE_CHANGED:
      return `E-Ink mode changed: from index ${u8(data0, 0)} to index ${u8(data0, 8)}, type ${u8(data0, 16)}, frame count ${u8(data0, 24)}.`;
    case UsbComm.LogEventId.LOG_EVENT_EINK_FRAME_RECEIVED:
      return `E-Ink frame stored: mode id ${u16lo(data0)}, frame index ${u16hi(data0)}, bytes ${data1}.`;
    case UsbComm.LogEventId.LOG_EVENT_EINK_RENDER:
      return `E-Ink render tick: active index ${u8(data0, 0)}, type ${u8(data0, 8)}, cursor ${u8(data0, 16)}, reason ${u8(data0, 24)}.`;
    case UsbComm.LogEventId.LOG_EVENT_KNOB_ZERO_OFFSET_APPLIED:
      return `Knob zero offset applied: raw ${data0} (x1e6), direction ${s16lo(data1)}.`;
    case UsbComm.LogEventId.LOG_EVENT_HELPER_CLOCK_SYNC:
      return `Helper pushed clock: ${u8(data0, 0).toString().padStart(2, '0')}:${u8(data0, 8).toString().padStart(2, '0')}, date ${u8(data0, 16)}/${u8(data0, 24)}, weekday ${u8(data1, 0)}.`;
    case UsbComm.LogEventId.LOG_EVENT_HELPER_WEATHER_SYNC:
      return `Helper pushed weather: icon ${u8(data0, 0)}, temp ${s16hi(data0) / 10} degC.`;
    default:
      return `Binary event ${event.eventId} data0=${data0} data1=${data1}.`;
  }
}

export function renderLogEvent(event: LogEvent): string {
  const prefix = `#${event.seq} t=${event.uptimeMs} ${levelLabel(event.level)} ${moduleLabel(event.module)}`;
  const trace = event.traceId !== undefined ? ` trace=${event.traceId}` : '';
  const repeat = event.repeatCount && event.repeatCount > 1 ? ` x${event.repeatCount}` : '';
  return `${prefix}${trace}${repeat} | ${renderEventMessage(event)}`;
}

export function renderSnapshot(snapshot: LogSnapshot): string {
  switch (snapshot.module) {
    case UsbComm.LogModule.SYSTEM:
      return `Diagnostic log snapshot: event ring ${u16lo(snapshot.state0)}, boot buffer ${u16hi(snapshot.state0)}.`;
    case UsbComm.LogModule.USB_COMM:
      return `USB comm snapshot: TX ${u16lo(snapshot.state0)} bytes, RX ${u16hi(snapshot.state0)} bytes, bytes field ${snapshot.state1} bytes.`;
    case UsbComm.LogModule.KSCAN:
      return `KScan snapshot: raw ${hex8(u8(snapshot.state0, 0))}, debounced ${hex8(u8(snapshot.state0, 8))}, logical ${hex8(u8(snapshot.state0, 16))}.`;
    case UsbComm.LogModule.TOUCHBAR:
      return `TouchBar snapshot: ${touchbarModeLabel(u8(snapshot.state0, 0))}, phase ${touchbarPhaseLabel(u8(snapshot.state0, 8))}, mask ${hex8(u8(snapshot.state0, 16))}, flags ${touchbarFlagsText(u8(snapshot.state0, 24))}, segment ${u8(snapshot.state1, 0)}, touches ${u8(snapshot.state1, 8)}, position ${s16lo(snapshot.state2)}, steps ${s16hi(snapshot.state2)}.`;
    case UsbComm.LogModule.HID:
      return `HID snapshot: mouse helper on HID_${u8(snapshot.state0, 0)}, ready ${u8(snapshot.state0, 8) === 1 ? 'yes' : 'no'}.`;
    case UsbComm.LogModule.RGB:
      return `RGB snapshot: on ${u8(snapshot.state0, 0) === 1 ? 'yes' : 'no'}, effect ${u8(snapshot.state0, 8)}, speed ${u8(snapshot.state0, 16)}, HSB ${u8(snapshot.state1, 0)}/${u8(snapshot.state1, 8)}/${u8(snapshot.state1, 16)}.`;
    case UsbComm.LogModule.INDICATOR:
      return `Indicator snapshot: enabled ${u8(snapshot.state0, 0) === 1 ? 'yes' : 'no'}, active brightness ${u8(snapshot.state0, 8)}, inactive brightness ${u8(snapshot.state0, 16)}, keyboard active ${u8(snapshot.state0, 24) === 1 ? 'yes' : 'no'}, indicator bits ${hexMask(snapshot.state1)}.`;
    default:
      return `Snapshot module ${snapshot.module}: state0=${snapshot.state0} state1=${snapshot.state1} state2=${snapshot.state2}.`;
  }
}
