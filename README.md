# Growatt Solar Inverter Modbus Data to MQTT Gateway - modified version for 3 phase inverters

This sketch is based on the work of nygma2004 (Csongor Varga) and is modified for 3 phase Growatt inverters.

You can find all the details in the original repo: https://github.com/nygma2004/growatt2mqtt

## Home Assistant MQTT auto discovery

Home Assistant discovery is enabled by default. After the ESP8266 connects to MQTT, it publishes retained discovery messages under:

```text
homeassistant/<component>/<client-id>/<entity>/config
```

The entities use the existing runtime topics:

- `growatt/data` for inverter readings
- `growatt/status` for gateway status
- `growatt/settings` for writable inverter settings
- `growatt/connection` for availability (`online` / `offline`)

You can customize or disable discovery in `settings.h`:

```cpp
const bool haDiscoveryEnabled = true;
const char* haDiscoveryPrefix = "homeassistant";
const char* haDeviceName = "Growatt 3-phase inverter";
```

Keep `topicRoot` without a trailing slash because the sketch builds subtopics from it.
