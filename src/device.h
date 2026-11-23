// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 D.Koch – Hermit OS
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
  #define PIN_SEL      12     // BL0937 SEL
  #define SEL_VOLT_LEVEL HIGH // Plug S: SEL HIGH = Spannung, LOW = Strom (am Geraet gemessen)
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
#elif defined(DEVICE_PLUG)
  // Shelly Plug (Gen1, EU, 16 A, SHPLG-1 / SHPLG2-1), Tasmota-Template
  // {"NAME":"Shelly Plug EU","GPIO":[0,0,0,0,224,2688,0,0,96,288,289,0,290,0],"FLAG":0,"BASE":18}
  #define DEV_PREFIX   "hermit-plug"
  #define DEV_MODEL    "HERMIT-PLUG"
  #define DEV_APP      "Plug"
  #define GEN1_TYPE    "SHPLG-1"
  #define PIN_RELAY    4
  #define PIN_INPUT    12     // Taster am Geraet, gedrueckt = HIGH
  #define PIN_LED      14     // rote LED = Relais (nicht invertiert)
  #define PIN_LED_LINK 16     // blaue LED = WLAN (nicht invertiert)
  #define PIN_LED_AUX  13     // gruene LED, bleibt aus
  #define LED_ON_LEVEL HIGH
  #define PIN_CF       5      // HLW8012 CF (nur Wirkleistung, kein CF1/SEL)
  #define PWR_TASMOTA_PREF 10000.0f   // HLW8012 (BL0937: 1362)
  #define PWR_CAL_DEFAULT 12.53f      // Tasmota HLW8012: 10000 * 12530 / 10 / T_us -> W pro Hz
  #define HAS_PM       1
  #define HAS_TEMP     0
  #define HAS_LED      1
  #define DEF_IN_TYPE   "button"
  #define DEF_IN_MODE   "momentary"
  #define DEF_IN_FILTER "dc"
  #define DEF_POWER_LIMIT 3500     // 16 A
#elif defined(DEVICE_DUO)
  // Shelly Duo (E27 / GU10), Gen1 SHBDUO-1: weisses Licht, Helligkeit + Farbtemperatur 2700-6500 K
  // Zwei Weisskanaele (wie ESPHome "cwww"): GPIO5 kalt, GPIO4 warm
  #define DEV_PREFIX   "hermit-duo"
  #define DEV_MODEL    "HERMIT-DUO"
  #define DEV_APP      "Duo"
  #define GEN1_TYPE    "SHBDUO-1"
  #define PIN_PWM_COLD 5      // Kaltweiss-Kanal (am Geraet gemessen)
  #define PIN_PWM_WARM 4      // Warmweiss-Kanal
  #define HAS_LIGHT    1
  #define HAS_RELAY    0
  #define HAS_INPUT    0
  #define HAS_PM       0
  #define HAS_TEMP     0
  #define HAS_LED      0
  #define DEF_INITIAL  "on"   // Lampe am Wandschalter: nach Strom an
  #define PWR_CAL_DEFAULT 1.0f
#elif defined(DEVICE_DIMMER2)
  // Shelly Dimmer 2 (SHDM-2): ESP8266 + STM32-Koprozessor (Phasenan-/abschnitt, Leistungsmessung).
  // Tasmota-Template {"NAME":"Shelly Dimmer 2","GPIO":[0,3200,0,3232,5568,5600,0,0,193,0,192,0,320,4736]}
  // UART0 (GPIO1/3, 115200) gehoert dem STM32 – keine Debug-Ausgaben ueber Serial!
  #define DEV_PREFIX   "hermit-dimmer2"
  #define DEV_MODEL    "HERMIT-DIMMER2"
  #define DEV_APP      "Dimmer2"
  #define GEN1_TYPE    "SHDM-2"
  #define HAS_DIM      1
  #define HAS_RELAY    0
  #define PIN_STM_BOOT0 4     // LOW = Anwendung starten
  #define PIN_STM_NRST  5     // LOW = STM32 im Reset
  #define PIN_INPUT    14     // SW1
  #define PIN_INPUT2   12     // SW2
  #define PIN_LED      16     // invertiert
  #define HAS_PM       1
  #define PM_STM       1      // Leistung kommt vom STM32 (kein CF-Pin)
  #define PWR_CAL_DEFAULT 1.0f   // Korrekturfaktor auf den STM32-Messwert
  #define HAS_TEMP     1      // NTC am ADC (wie 1PM)
  #define HAS_LED      1
  #define DEF_IN_TYPE   "button"
  #define DEF_IN_MODE   "dim"
  #define DEF_INITIAL   "restore_last"
  #define DEF_POWER_LIMIT 250      // Dimmer 2: 220 W bei 230 V
  #define DEF_TEMP_LIMIT  85
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
#if defined(WITH_ADDON) && (defined(DEVICE_PLUGS) || defined(DEVICE_PLUG) || defined(DEVICE_DUO) || defined(DEVICE_DIMMER2))
  #error "Plug / Plug S / Duo / Dimmer 2 haben keinen Add-on-Anschluss"
#endif
// Messfirmware: nur Energiemessung, Relais bleibt an. Abschaltung bei Ueberlast/Uebertemperatur,
// Wiedereinschalten nur von Hand am Taster bzw. SW-Eingang. Kein Schalten per RPC/HTTP.
#if defined(METER_ONLY)
  #if !(defined(DEVICE_SHELLY1PM) || defined(DEVICE_PLUGS) || defined(DEVICE_PLUG))
    #error "Messfirmware nur fuer Shelly 1PM, Plug S und Plug"
  #endif
  #if defined(WITH_ADDON)
    #error "Messfirmware ohne Add-on"
  #endif
  #define IS_METER 1
#else
  #define IS_METER 0
#endif
#if defined(WITH_ADDON)
  #define HAS_ADDON      1
  #define PIN_ADDON_OUT  0
  #define PIN_ADDON_IN   3
  #undef  HAS_LED
  #define HAS_LED        0     // GPIO0 wird vom Add-on belegt
  #define DEV_MODEL_STR  DEV_MODEL "-ADDON"
  #define DEV_VARIANT    DEV_PREFIX "-addon"
#elif IS_METER
  #define HAS_ADDON      0
  #define DEV_MODEL_STR  DEV_MODEL "-METER"
  #define DEV_VARIANT    DEV_PREFIX "-meter"
  #undef  DEF_IN_MODE
  #define DEF_IN_MODE    "detached"
  #undef  DEF_INITIAL
  #define DEF_INITIAL    "on"
#else
  #define HAS_ADDON      0
  #define DEV_MODEL_STR  DEV_MODEL
  #define DEV_VARIANT    DEV_PREFIX
#endif
// DEV_VARIANT = Schluessel in update.json und Name der .bin (z. B. hermit-1pm-addon)

// Standardwerte fuer Geraete ohne eigene Vorgabe
#ifndef LED_ON_LEVEL
  #define LED_ON_LEVEL LOW       // Shelly-LEDs meist invertiert
#endif
#define LED_OFF_LEVEL (LED_ON_LEVEL == LOW ? HIGH : LOW)
#ifndef PWR_TASMOTA_PREF
  #define PWR_TASMOTA_PREF 1362.0f    // Tasmota-Konstante BL0937
#endif
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
#ifndef DEF_TEMP_LIMIT
  #define DEF_TEMP_LIMIT 95
#endif
#ifndef HAS_DIM
  #define HAS_DIM 0
#endif
#ifndef PM_STM
  #define PM_STM 0
#endif
#ifdef PIN_INPUT2
  #define HAS_IN2 1
#else
  #define HAS_IN2 0
#endif
#define V_CAL_DEFAULT 0.16029f      // Tasmota BL0937: 822 * 1950 / 10 / T_us  -> V pro Hz
#define I_CAL_DEFAULT 0.01155f      // Tasmota BL0937: 3300 * 3500 / 1000 / T_us -> A pro Hz
#ifndef HAS_LIGHT
  #define HAS_LIGHT 0
#endif
#ifndef HAS_RELAY
  #define HAS_RELAY 1
#endif
#ifndef HAS_INPUT
  #define HAS_INPUT 1
#endif
#ifndef DEF_INITIAL
  #define DEF_INITIAL "off"
#endif
#if HAS_LIGHT
  #define COMP     "cct"        // Gen2-Komponente wie Shelly Duo Bulb Gen3
  #define EV_ON    "cct.on"
  #define EV_OFF   "cct.off"
#elif HAS_DIM
  #define COMP     "light"      // Gen2-Komponente wie Shelly Dimmer Gen3 / Plus Wall Dimmer
  #define EV_ON    "light.on"
  #define EV_OFF   "light.off"
#else
  #define COMP     "switch"
  #define EV_ON    "switch.on"
  #define EV_OFF   "switch.off"
#endif
#define EV_OVERPOWER COMP ".overpower"
#define EV_OVERTEMP  COMP ".overtemperature"
#define EV_PABOVE    COMP ".power_above"
#define EV_PBELOW    COMP ".power_below"
