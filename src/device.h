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
#elif defined(DEVICE_PLUGS)
  // Shelly Plug S (Gen1), Tasmota-Template {"NAME":"Shelly Plug S","GPIO":[320,1,576,1,1,2720,0,0,2624,32,2656,224,1,4736]}
  #define DEV_PREFIX   "hermit-plugs"
  #define DEV_MODEL    "HERMIT-PLUGS"
  #define DEV_APP      "PlugS"
  #define GEN1_TYPE    "SHPLG-S"
  #define PIN_RELAY    15
  #define PIN_INPUT    13     // Taster am Geraet, nach GND, Pull-up
  #define PIN_LED      0      // Relais-LED, invertiert
  #define PIN_LED_LINK 2      // WLAN-LED, invertiert
  #define PIN_CF       5      // BL0937 CF  (Wirkleistung)
  #define PIN_CF1      14     // BL0937 CF1 (Spannung/Strom)
  #define PIN_SEL      12     // BL0937 SEL: LOW = Spannung, HIGH = Strom
  #define PWR_CAL_DEFAULT 1.706586f
  #define HAS_PM       1
  #define HAS_CF1      1
  #define HAS_TEMP     1
  #define HAS_LED      1
  #define INPUT_ACTIVE_LOW 1
  #define DEF_IN_TYPE   "button"
  #define DEF_IN_MODE   "momentary"
  #define DEF_IN_FILTER "dc"
  #define DEF_POWER_LIMIT 2500     // Plug S: max. 10 A
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

// Variante mit Shelly-Temperatur-Add-on (Gen1), genutzt als externer Tastereingang input:100.
// Das Add-on fuehrt die Datenleitung ueber Optokoppler: GPIO0 = Ausgang (Leitung freigeben),
// GPIO3 = Eingang. Taster zwischen DATA und GND des Add-ons.
#if defined(WITH_ADDON) && defined(DEVICE_PLUGS)
  #error "Plug S hat keinen Add-on-Anschluss"
#endif
#if defined(WITH_ADDON)
  #define HAS_ADDON      1
  #define PIN_ADDON_OUT  0
  #define PIN_ADDON_IN   3
  #undef  HAS_LED
  #define HAS_LED        0     // GPIO0 wird vom Add-on belegt
  #define DEV_MODEL_STR  DEV_MODEL "-ADDON"
  #define DEV_VARIANT    DEV_PREFIX "-addon"
#else
  #define HAS_ADDON      0
  #define DEV_MODEL_STR  DEV_MODEL
  #define DEV_VARIANT    DEV_PREFIX
#endif
// DEV_VARIANT = Schluessel in update.json und Name der .bin (z. B. hermit-1pm-addon)

// Standardwerte fuer Geraete ohne eigene Vorgabe
#ifndef HAS_CF1
  #define HAS_CF1 0
#endif
#ifndef INPUT_ACTIVE_LOW
  #define INPUT_ACTIVE_LOW 0
#endif
#ifndef DEF_IN_TYPE
  #define DEF_IN_TYPE "switch"
#endif
#ifndef DEF_IN_MODE
  #define DEF_IN_MODE "follow"
#endif
#ifndef DEF_IN_FILTER
  #define DEF_IN_FILTER "ac"
#endif
#ifndef DEF_POWER_LIMIT
  #define DEF_POWER_LIMIT 3500
#endif
#define V_CAL_DEFAULT 0.16029f      // Tasmota BL0937: 822 * 1950 / 10 / T_us  -> V pro Hz
#define I_CAL_DEFAULT 0.01155f      // Tasmota BL0937: 3300 * 3500 / 1000 / T_us -> A pro Hz
