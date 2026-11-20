#pragma once
// Pinbelegung der Shelly-Gen1-Geraete (entspricht den Tasmota-Templates)

#if defined(DEVICE_SHELLY1PM)
  #define DEV_PREFIX   "shelly1pm"
  #define DEV_MODEL    "SHSW-PM-OS"
  #define DEV_APP      "Gen1PM"
  #define GEN1_TYPE    "SHSW-PM"
  #define PIN_RELAY    15
  #define PIN_INPUT    4
  #define PIN_LED      0      // invertiert
  #define PIN_CF       5      // HLW8012 CF (Wirkleistung)
  #define HAS_PM       1
  #define HAS_TEMP     1      // NTC am ADC
  #define HAS_LED      1
#elif defined(DEVICE_SHELLY1)
  #define DEV_PREFIX   "shelly1"
  #define DEV_MODEL    "SHSW-1-OS"
  #define DEV_APP      "Gen1Relay"
  #define GEN1_TYPE    "SHSW-1"
  #define PIN_RELAY    4
  #define PIN_INPUT    5
  #define HAS_PM       0
  #define HAS_TEMP     0
  #define HAS_LED      0
#else
  #error "DEVICE_SHELLY1 oder DEVICE_SHELLY1PM definieren"
#endif
