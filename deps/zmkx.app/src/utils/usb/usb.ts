import type { UsbComm } from '@/proto/comm.proto';

export interface IUsbCommTransport<T extends IUsbCommDevice> {
  pick(device: T): Promise<T | undefined>;
  request(): Promise<T | undefined>;
  close(): Promise<void>;
  send(req: UsbComm.IMessageH2D): Promise<void>;
}

export type IUsbCommDevice = USBDevice | HIDDevice;

export type OnList = (devices: IUsbCommDevice[]) => void;

export type OnMessage = (res: UsbComm.MessageD2H) => void;

export type OnDisconnected = () => void;

export type UsbTransportTraceKind =
  | 'tx-message'
  | 'tx-packet'
  | 'rx-packet'
  | 'rx-decode'
  | 'rx-drop'
  | 'rx-overflow';

export interface UsbTransportTrace {
  ts: number;
  kind: UsbTransportTraceKind;
  summary: string;
  repeatCount?: number;
  rawHex?: string;
  action?: string;
  payload?: string;
  packetLength?: number;
  messageLength?: number;
  queueLength?: number;
}

export type OnTransportTrace = (trace: UsbTransportTrace) => void;
