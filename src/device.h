#pragma once
// Pinbelegung der Shelly-Gen1-Geraete (entspricht den Tasmota-Templates)

#if defined(DEVICE_SHELLY1PM)
  #define DEV_PREFIX   "hermit-1pm"
  #define DEV_MODEL    "HERMIT-1PM"
  #define DEV_APP      "Gen1PM"
  #define GEN1_TYPE    "SHSW-PM"
  #define PIN_RELAY    15
  #define PIN_INPUT    4
  #define PIN_LED      0      // invertiert
  #define PIN_CF       5      // BL0937 CF (Wirkleistung)
  // Standardfaktor wie Tasmota fuer BL0937 (GPIO_HJL_CF): 1362 * 12530 / 10 / T_us
  #define PWR_CAL_DEFAULT 1.706586f   // W pro Hz
  #define HAS_PM       1
  #define HAS_TEMP     1      // NTC am ADC
  #define HAS_LED      1
#elif defined(DEVICE_SHELLY1)
  #define DEV_PREFIX   "hermit-1"
  #define DEV_MODEL    "HERMIT-1"
  #define DEV_APP      "Gen1Relay"
  #define GEN1_TYPE    "SHSW-1"
  #define PIN_RELAY    4
  #define PIN_INPUT    5
  #define HAS_PM       0
  #define PWR_CAL_DEFAULT 1.0f
  #define HAS_TEMP     0
  #define HAS_LED      0
#else
  #error "DEVICE_SHELLY1 oder DEVICE_SHELLY1PM definieren"
#endif
