import { UsbComm } from './protoLoader.mjs';

const USER_AGENT = 'hw75-core/0.3 (+https://github.com/hellowordkb/hw75)';

/*
 * Open-Meteo WMO weather code to our EinkWeatherIcon enum.
 * https://open-meteo.com/en/docs#weathervariables
 */
function mapWeatherCode(code, isNight) {
  if (code === undefined || code === null) {
    return UsbComm.EinkWeatherIcon.WEATHER_UNKNOWN;
  }
  if (isNight && code <= 3) {
    return UsbComm.EinkWeatherIcon.WEATHER_NIGHT;
  }
  if (code === 0) return UsbComm.EinkWeatherIcon.WEATHER_SUNNY;
  if (code === 1 || code === 2) return UsbComm.EinkWeatherIcon.WEATHER_CLOUDY;
  if (code === 3) return UsbComm.EinkWeatherIcon.WEATHER_OVERCAST;
  if (code === 45 || code === 48) return UsbComm.EinkWeatherIcon.WEATHER_FOGGY;
  if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) {
    return UsbComm.EinkWeatherIcon.WEATHER_RAINY;
  }
  if ((code >= 71 && code <= 77) || code === 85 || code === 86) {
    return UsbComm.EinkWeatherIcon.WEATHER_SNOWY;
  }
  if (code >= 95 && code <= 99) return UsbComm.EinkWeatherIcon.WEATHER_STORM;
  return UsbComm.EinkWeatherIcon.WEATHER_UNKNOWN;
}

async function fetchOpenMeteo(lat, lon) {
  const url = `https://api.open-meteo.com/v1/forecast?latitude=${lat}&longitude=${lon}&current=temperature_2m,weather_code,is_day&timezone=auto`;
  const response = await fetch(url, { headers: { 'User-Agent': USER_AGENT } });
  if (!response.ok) {
    throw new Error(`open-meteo http ${response.status}`);
  }
  const data = await response.json();
  const current = data?.current;
  if (!current) {
    throw new Error('open-meteo: missing current block');
  }
  return {
    tempC: Number(current.temperature_2m ?? 0),
    code: Number(current.weather_code ?? -1),
    isNight: current.is_day === 0,
    fetchedAt: Date.now(),
  };
}

export class Weather {
  constructor({ keyboard, coreConfig, bus }) {
    this.keyboard = keyboard;
    this.coreConfig = coreConfig;
    this.bus = bus;
    this.timer = undefined;
    this.lastPayload = undefined;
  }

  start() {
    this.scheduleNext(1000);
  }

  stop() {
    if (this.timer) {
      clearTimeout(this.timer);
      this.timer = undefined;
    }
  }

  scheduleNext(delayMs) {
    if (this.timer) {
      clearTimeout(this.timer);
    }
    this.timer = setTimeout(() => this.tick(), delayMs);
  }

  async refreshNow() {
    await this.tick(true);
  }

  async tick(manual = false) {
    const cfg = this.coreConfig.snapshot().weather;
    const intervalMs = Math.max(1, cfg.refresh_minutes) * 60 * 1000;

    if (!cfg.enabled) {
      this.scheduleNext(intervalMs);
      return;
    }

    try {
      const reading = await fetchOpenMeteo(cfg.lat, cfg.lon);
      const tempDeci = Math.round(reading.tempC * 10);
      const icon = mapWeatherCode(reading.code, reading.isNight);
      const city = (cfg.city ?? '').slice(0, 23);

      const payload = {
        tempC: reading.tempC,
        tempDeci,
        icon,
        city,
        fetchedAt: reading.fetchedAt,
        weatherCode: reading.code,
      };
      this.lastPayload = payload;
      this.bus?.broadcastWeather(payload);

      if (this.keyboard.isConnected()) {
        try {
          await this.keyboard.send({
            action: UsbComm.Action.EINK_PUSH_WEATHER,
            einkWeather: {
              tempDeciC: tempDeci,
              icon,
              city,
            },
          });
        } catch (err) {
          console.warn(`[weather] push failed: ${err.message}`);
        }
      }
    } catch (err) {
      console.warn(`[weather] fetch failed: ${err.message}`);
    }

    if (!manual) {
      this.scheduleNext(intervalMs);
    } else {
      this.scheduleNext(intervalMs);
    }
  }
}
