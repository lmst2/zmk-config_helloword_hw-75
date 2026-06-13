/*
 * Default per-application context rules for the Chinese desktop ecosystem.
 * Data-driven: apps that aren't installed simply never match (harmless). The
 * user can override this set from the web UI later (F4). Premiere is omitted on
 * purpose; 剪映 is the creation-suite target.
 *
 * scene levers:
 *   knob.feel    { mode, ppr?, strength? }   mode: inertia|encoder|spring|damped|ratchet|...
 *   knob.detents { count, strength?, endstops? }
 *   rgb          { hsb:{h(0-360),s(0-100),b(0-100)}, effect?, on? }
 *   eink         <mode slot index>
 */

export const DEFAULT_RULES = [
  // ---- 音乐: notched volume + now-playing theme ----
  { id: '网易云音乐', match: { process: ['cloudmusic.exe', 'cloudmusicn.exe'] },
    scene: { knob: { detents: { count: 24, strength: 45 } }, rgb: { hsb: { h: 355, s: 90, b: 40 }, effect: 1 } } },
  { id: 'QQ音乐', match: { process: ['qqmusic.exe'] },
    scene: { knob: { detents: { count: 24, strength: 45 } }, rgb: { hsb: { h: 140, s: 85, b: 40 }, effect: 1 } } },
  { id: '酷狗音乐', match: { process: ['kugou.exe', 'kgmusic.exe'] },
    scene: { knob: { detents: { count: 24, strength: 45 } }, rgb: { hsb: { h: 210, s: 85, b: 40 }, effect: 1 } } },

  // ---- 视频: inertia jog/scrub timeline ----
  { id: '哔哩哔哩', match: { process: ['bilibili.exe', '哔哩哔哩.exe', 'bilibiliapp.exe'] },
    scene: { knob: { feel: { mode: 'inertia', strength: 40 } }, rgb: { hsb: { h: 333, s: 80, b: 45 }, effect: 0 } } },
  { id: '腾讯视频', match: { process: ['qqlive.exe', 'tenvideo.exe'] },
    scene: { knob: { feel: { mode: 'inertia', strength: 40 } }, rgb: { hsb: { h: 35, s: 80, b: 45 }, effect: 0 } } },
  { id: '爱奇艺', match: { process: ['qyclient.exe', 'iqiyiclient.exe'] },
    scene: { knob: { feel: { mode: 'inertia', strength: 40 } }, rgb: { hsb: { h: 135, s: 85, b: 45 }, effect: 0 } } },
  { id: '本地播放器', match: { process: ['potplayermini64.exe', 'potplayer64.exe', 'potplayermini.exe', 'vlc.exe', 'mpc-hc64.exe', 'mpc-be64.exe'] },
    scene: { knob: { feel: { mode: 'inertia', strength: 35 } }, rgb: { hsb: { h: 35, s: 55, b: 35 }, effect: 0 } } },

  // ---- 浏览器 / 阅读: momentum scroll ----
  { id: '浏览器', match: { process: ['chrome.exe', 'msedge.exe', '360se.exe', '360se6.exe', '360chrome.exe', 'qqbrowser.exe', 'firefox.exe'] },
    scene: { knob: { feel: { mode: 'inertia', strength: 30 } } } },
  { id: '微信读书', match: { process: ['weread.exe', 'wereadx.exe'] },
    scene: { knob: { feel: { mode: 'encoder', ppr: 8, strength: 45 } } } },

  // ---- 办公 / 文档 ----
  { id: 'WPS', match: { process: ['wps.exe', 'et.exe', 'wpp.exe', 'wpspdf.exe'] },
    scene: { knob: { feel: { mode: 'damped', strength: 40 } } } },
  { id: '腾讯会议', match: { process: ['wemeetapp.exe', 'txmeetinghost.exe'] },
    scene: { knob: { detents: { count: 30, strength: 55, endstops: true } }, rgb: { hsb: { h: 210, s: 80, b: 45 }, effect: 0 } } },
  { id: '钉钉', match: { process: ['dingtalk.exe', 'dingtalklauncher.exe'] },
    scene: { knob: { feel: { mode: 'encoder', ppr: 30, strength: 45 } } } },
  { id: '飞书', match: { process: ['feishu.exe', 'lark.exe'] },
    scene: { knob: { feel: { mode: 'encoder', ppr: 30, strength: 45 } } } },

  // ---- IM ----
  { id: '微信', match: { process: ['wechat.exe', 'weixin.exe', 'wechatappex.exe'] },
    scene: { knob: { feel: { mode: 'encoder', ppr: 24, strength: 40 } }, rgb: { hsb: { h: 120, s: 80, b: 35 }, effect: 0 } } },
  { id: 'QQ', match: { process: ['qq.exe', 'qqnt.exe'] },
    scene: { knob: { feel: { mode: 'encoder', ppr: 24, strength: 40 } }, rgb: { hsb: { h: 205, s: 85, b: 35 }, effect: 0 } } },

  // ---- 开发 / 创作 ----
  { id: 'VSCode', match: { process: ['code.exe'] },
    scene: { knob: { feel: { mode: 'encoder', ppr: 60, strength: 35 } }, rgb: { hsb: { h: 210, s: 75, b: 40 }, effect: 0 } } },
  { id: '终端', match: { process: ['windowsterminal.exe', 'powershell.exe', 'pwsh.exe', 'cmd.exe', 'openconsole.exe'] },
    scene: { knob: { feel: { mode: 'encoder', ppr: 36, strength: 40 } } } },
  { id: 'JetBrains', match: { process: ['idea64.exe', 'pycharm64.exe', 'webstorm64.exe', 'clion64.exe', 'goland64.exe', 'rider64.exe'] },
    scene: { knob: { feel: { mode: 'encoder', ppr: 48, strength: 40 } } } },
  { id: '剪映', match: { process: ['jianyingpro.exe'] },
    scene: { knob: { feel: { mode: 'spring', strength: 50 } }, rgb: { hsb: { h: 180, s: 70, b: 45 }, effect: 2 } } },

  // ---- fallback ----
  { id: '默认', default: true,
    scene: { knob: { feel: { mode: 'encoder', ppr: 24, strength: 40 } } } },
];
