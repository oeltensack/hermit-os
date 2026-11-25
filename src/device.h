// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 D.Koch – Pagurus OS
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
#elif defined(DEVICE_RFBRIDGE)
  // Sonoff RF Bridge 433 (R2): ESP8285 mit 1 MB Flash + EFM8BB1 (Portisch- oder Original-Firmware)
  // UART0 (GPIO1/3, 19200 Baud) gehoert dem EFM8 – keine Debug-Ausgaben ueber Serial!
  #define DEV_PREFIX   "hermit-rfbridge"
  #define DEV_MODEL    "HERMIT-RFBRIDGE"
  #define DEV_APP      "RFBridge"
  #define GEN1_TYPE    "RFBRIDGE"
  #define HAS_RF       1
  #define FLASH_1M     1
  #define HAS_RELAY    0
  #define HAS_INPUT    0
  #define HAS_PM       0
  #define HAS_TEMP     0
  #define HAS_LED      1
  #define PIN_LED      13     // blaue WLAN-LED, invertiert
  #define PIN_BTN      0      // Taster (gedrueckt = LOW)
  #define HAS_MGOS     0      // keine Shelly-Original-Firmware als Vorgaenger
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
#elif defined(DEVICE_SHELLY25)
  // Shelly 2.5 (SHSW-25): zwei Relais, zwei Eingaenge, ADE7953 an I2C, NTC
  // Tasmota-Template {"NAME":"Shelly 2.5","GPIO":[320,0,32,0,224,193,0,0,640,192,608,225,3456,4736]}
  #define DEV_PREFIX   "hermit-25"
  #define DEV_MODEL    "HERMIT-25"
  #define DEV_APP      "Shelly25"
  #define GEN1_TYPE    "SHSW-25"
  #define NCH          2
  #define PIN_RELAY    4      // Relais 1 (Rollladen: auf)
  #define PIN_RELAY2   15     // Relais 2 (Rollladen: zu)
  #define PIN_INPUT    13     // SW1
  #define PIN_INPUT2   5      // SW2
  #define PIN_LED      0      // invertiert
  #define PIN_BTN      2      // Taster auf der Platine (gedrueckt = LOW)
  #define PIN_SDA      12     // ADE7953
  #define PIN_SCL      14
  #define HAS_PM       1
  #define PM_ADE       1
  #define PWR_CAL_DEFAULT 1.0f     // Korrekturfaktoren auf die ADE7953-Werte
  #define V_CAL_DEFAULT   1.0f
  #define I_CAL_DEFAULT   1.0f
  #define HAS_TEMP     1
  #define HAS_LED      1
  #define DEF_POWER_LIMIT 2300     // je Kanal (10 A)
  #if defined(COVER_MODE)
    #define IS_COVER   1
  #endif
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
#elif defined(DEVICE_PLUG1M)
  // Einfache WLAN-Steckdose mit ESP8285 (1 MB), Touch-Taste, ohne Leistungsmessung (z. B. „RowiSteckdose“)
  // Belegung aus der ESPHome-Konfiguration: Relais GPIO12, Taste GPIO13 (Pull-up, gedrueckt = LOW),
  // gruene LED GPIO4 (Relais), rote LED GPIO5 (WLAN), beide invertiert
  #define DEV_PREFIX   "hermit-plug1m"
  #define DEV_MODEL    "HERMIT-PLUG1M"
  #define DEV_APP      "Plug1M"
  #define GEN1_TYPE    "PLUG1M"
  #define FLASH_1M     1
  #define PIN_RELAY    12
  #define PIN_INPUT    13
  #define PIN_LED      4      // gruen = Relais
  #define PIN_LED_LINK 5      // rot = WLAN
  #define HAS_PM       0
  #define HAS_TEMP     0
  #define HAS_LED      1
  #define INPUT_ACTIVE_LOW 1
  #define DEF_IN_TYPE   "button"
  #define DEF_IN_MODE   "momentary"
  #define DEF_IN_FILTER "dc"
  #define HAS_MGOS     0      // keine Shelly-Original-Firmware als Vorgaenger
  #define PWR_CAL_DEFAULT 1.0f
#else
  #error "DEVICE_SHELLY1 oder DEVICE_SHELLY1PM definieren"
#endif

// Variante mit Shelly-Temperatur-Add-on (Gen1), genutzt als externer Tastereingang input:100.
// Das Add-on fuehrt die Datenleitung ueber Optokoppler: GPIO0 = Ausgang (Leitung freigeben),
// GPIO3 = Eingang. Taster zwischen DATA und GND des Add-ons.
#if defined(WITH_ADDON) && (defined(DEVICE_PLUGS) || defined(DEVICE_PLUG) || defined(DEVICE_DUO) || defined(DEVICE_DIMMER2) || defined(DEVICE_SHELLY25))
  #error "Plug / Plug S / Duo / Dimmer 2 / Shelly 2.5 haben keinen Add-on-Anschluss"
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
#elif defined(COVER_MODE)
  #define HAS_ADDON      0
  #define DEV_MODEL_STR  DEV_MODEL "-COVER"
  #define DEV_VARIANT    DEV_PREFIX "-cover"
  #undef  DEF_IN_MODE
  #define DEF_IN_MODE    "dual"
  #undef  DEF_IN_TYPE
  #define DEF_IN_TYPE    "switch"
  #undef  DEF_INITIAL
  #define DEF_INITIAL    "stopped"
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
#ifndef NCH
  #define NCH 1               // Anzahl Ausgangskanaele (Relais)
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
#ifndef V_CAL_DEFAULT
  #define V_CAL_DEFAULT 0.16029f    // Tasmota BL0937: 822 * 1950 / 10 / T_us  -> V pro Hz
#endif
#ifndef I_CAL_DEFAULT
  #define I_CAL_DEFAULT 0.01155f    // Tasmota BL0937: 3300 * 3500 / 1000 / T_us -> A pro Hz
#endif
#ifndef PM_ADE
  #define PM_ADE 0
#endif
#ifndef IS_COVER
  #define IS_COVER 0
#endif
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
#ifndef HAS_RF
  #define HAS_RF 0
#endif
#ifndef FLASH_1M
  #define FLASH_1M 0
#endif
#ifndef HAS_MGOS
  #define HAS_MGOS 1
#endif
#define SERIAL_BUSY (HAS_DIM || HAS_RF)   // UART0 gehoert einem Koprozessor
#if IS_COVER
  #define COMP     "cover"      // Gen2-Komponente wie Shelly Plus 2PM im Rollladenbetrieb
  #define EV_ON    "cover.open"
  #define EV_OFF   "cover.closed"
#elif HAS_LIGHT
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
