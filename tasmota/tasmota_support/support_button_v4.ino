/*
  support_button.ino - button support for Tasmota

  Copyright (C) 2022  Federico Leoni and Theo Arends

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
  MODIFIED by nineti GmbH (stromleser), 2025: added custom reset button and status LED.
  Not the original Tasmota version. See NOTICE.md for the list of changes.
*/

#define BUTTON_V4
#ifdef BUTTON_V4
/*********************************************************************************************\
 * Button support with input filter
 *
 * Inspired by (https://github.com/OLIMEX/olimex-iot-firmware-esp8266/blob/master/olimex/user/user_switch2.c)
\*********************************************************************************************/

#define MAX_RELAY_BUTTON1       5            // Max number of relay controlled by BUTTON1

#ifndef DOUBLE_CLICK_WINDOW
 #define DOUBLE_CLICK_WINDOW 500             // Define Window size to recognize double clicks
#endif

const uint8_t BUTTON_PROBE_INTERVAL = 10;      // Time in milliseconds between button input probe
const uint8_t BUTTON_FAST_PROBE_INTERVAL = 2;  // Time in milliseconds between button input probe for AC detection
const uint8_t BUTTON_AC_PERIOD = (20 + BUTTON_FAST_PROBE_INTERVAL - 1) / BUTTON_FAST_PROBE_INTERVAL;   // Duration of an AC wave in probe intervals

const char kMultiPress[] PROGMEM = "|SINGLE|DOUBLE|TRIPLE|QUAD|PENTA|CLEAR|";

#include <Ticker.h>

Ticker TickerButton;

struct BUTTON {
  uint32_t debounce = 0;                        // Button debounce timer
  uint32_t no_pullup_mask = 0;                  // key no pullup flag (1 = no pullup)
  uint32_t pulldown_mask = 0;                   // key pulldown flag (1 = pulldown)
  uint32_t inverted_mask = 0;                   // Key inverted flag (1 = inverted)
  uint32_t used = 0;                            // Key used bitmask
  uint32_t virtual_pin = 0;                     // Key state bitmask
  uint16_t hold_timer[MAX_KEYS_SET] = { 0 };    // Timer for button hold
  uint16_t dual_code = 0;                       // Sonoff dual received code
  uint8_t state[MAX_KEYS_SET] = { 0 };
  uint8_t last_state[MAX_KEYS_SET];             // Last button states
  uint8_t debounced_state[MAX_KEYS_SET];        // Button debounced states
  uint8_t window_timer[MAX_KEYS_SET] = { 0 };   // Max time between button presses to record press count
  uint8_t press_counter[MAX_KEYS_SET] = { 0 };  // Number of button presses within Button.window_timer
  uint8_t dual_receive_count = 0;               // Sonoff dual input flag
  uint8_t first_change = 0;
  bool probe_mutex;
} Button;

#if defined(SOC_TOUCH_VERSION_1) || defined(SOC_TOUCH_VERSION_2)
struct TOUCH_BUTTON {
  uint32_t touch_mask = 0;                      // Touch flag (1 = enabled)
  uint32_t calibration = 0;                     // Bitfield
  uint8_t hits[MAX_KEYS_SET] = { 0 };           // Hits in a row to filter out noise
} TouchButton;
#endif  // ESP32 SOC_TOUCH_VERSION_1 or SOC_TOUCH_VERSION_2

/********************************************************************************************/

// Custom Reset Button function declarations
void CustomResetButtonInit(void);
void CustomResetButtonLoop(void);

// Status LED function declarations
void StatusLedInit(void);
void StatusLedSetStatus(uint8_t status);
void StatusLedLoop(void);
void StatusLedCheckSystemState(void);

void ButtonPullupFlag(uint32_t button_bit) {
  bitSet(Button.no_pullup_mask, button_bit);
}

void ButtonPulldownFlag(uint32_t button_bit) {
  bitSet(Button.pulldown_mask, button_bit);
}

void ButtonInvertFlag(uint32_t button_bit) {
  bitSet(Button.inverted_mask, button_bit);
}

#if defined(SOC_TOUCH_VERSION_1) || defined(SOC_TOUCH_VERSION_2)
void ButtonTouchFlag(uint32_t button_bit) {
  bitSet(TouchButton.touch_mask, button_bit);
}
#endif  // ESP32 SOC_TOUCH_VERSION_1 or SOC_TOUCH_VERSION_2

/*------------------------------------------------------------------------------------------*/

void ButtonSetVirtualPinState(uint32_t index, uint32_t state) {
  // Set virtual pin state to be debounced as used by early detected buttons
  bitWrite(Button.virtual_pin, index, state);
}

uint8_t ButtonGetState(uint32_t index) {
  // Get current state
  return Button.debounced_state[index];
}

uint8_t ButtonLastState(uint32_t index) {
  // Get last state
  return Button.last_state[index];
}

/*------------------------------------------------------------------------------------------*/

bool ButtonUsed(uint32_t index) {
  return (PinUsed(GPIO_KEY1, index) || bitRead(Button.used, index));
}

/*********************************************************************************************/

void ButtonProbe(void) {
  if (Button.probe_mutex || (TasmotaGlobal.uptime < 4)) { return; }  // Block GPIO for 4 seconds after poweron to workaround Wemos D1 / Obi RTS circuit
  Button.probe_mutex = true;

  uint32_t state_filter;
  uint32_t first_change = Button.first_change;
  uint32_t debounce_flags = Settings->button_debounce % 10;
  bool force_high = (debounce_flags &1);                       // 51, 101, 151 etc
  bool force_low = (debounce_flags &2);                        // 52, 102, 152 etc
  bool ac_detect = (debounce_flags == 9);                      // 39, 49, 59 etc

  if (ac_detect) {
    if (Settings->button_debounce < 2 * BUTTON_AC_PERIOD * BUTTON_FAST_PROBE_INTERVAL + 9) {
      state_filter = 2 * BUTTON_AC_PERIOD;
    } else if (Settings->button_debounce > (0x7f - 2 * BUTTON_AC_PERIOD) * BUTTON_FAST_PROBE_INTERVAL) {
      state_filter = 0x7f;
    } else {
      state_filter = (Settings->button_debounce - 9) / BUTTON_FAST_PROBE_INTERVAL;
    }
  } else {
    state_filter = Settings->button_debounce / BUTTON_PROBE_INTERVAL;	// 5, 10, 15
  }

  uint32_t not_activated;
  for (uint32_t i = 0; i < MAX_KEYS_SET; i++) {
    if (!bitRead(Button.used, i)) { continue; }

    if (PinUsed(GPIO_KEY1, i)) {
#if defined(SOC_TOUCH_VERSION_1) || defined(SOC_TOUCH_VERSION_2)
      if (bitRead(TouchButton.touch_mask, i)) {
        if (ac_detect || bitRead(TouchButton.calibration, i +1)) { continue; }  // Touch is slow. Takes 21mS to read
        uint32_t value = touchRead(Pin(GPIO_KEY1, i));
#ifdef SOC_TOUCH_VERSION_2
        not_activated = (value < Settings->touch_threshold);  // ESPS3 No touch = 24200, Touch > 40000
#else
        not_activated = ((value == 0) || (value > Settings->touch_threshold));  // ESP32 No touch = 74, Touch < 40
#endif
      } else
#endif  // ESP32 SOC_TOUCH_VERSION_1 or SOC_TOUCH_VERSION_2
      not_activated = (digitalRead(Pin(GPIO_KEY1, i)) != bitRead(Button.inverted_mask, i));
    } else {
      not_activated = (bitRead(Button.virtual_pin, i) != bitRead(Button.inverted_mask, i));
    }

    if (not_activated) {

      if (ac_detect) {                                         // Enabled with ButtonDebounce x9
        Button.state[i] |= 0x80;
        if (Button.state[i] > 0x80) {
          Button.state[i]--;
          if (0x80 == Button.state[i]) {
            Button.debounced_state[i] = 0;
            Button.first_change = false;
          }
        }
      } else {

        if (force_high) {                                      // Enabled with ButtonDebounce x1
          if (1 == Button.debounced_state[i]) {
            Button.state[i] = state_filter;                    // With noisy input keep current state 1 unless constant 0
          }
        }

        if (Button.state[i] < state_filter) {
          Button.state[i]++;
          if (state_filter == Button.state[i]) {
            Button.debounced_state[i] = 1;
          }
        }
      }
    } else {

      if (ac_detect) {                                         // Enabled with ButtonDebounce x9
        /*
          * Moes MS-104B and similar devices using an AC detection circuitry
          * on their switch inputs generating an ~4 ms long low pulse every
          * AC wave. We start the time measurement on the falling edge.
          *
          * state: bit7: previous state, bit6..0: counter
          */
        if (Button.state[i] & 0x80) {
          Button.state[i] &= 0x7f;
          if (Button.state[i] < state_filter - 2 * BUTTON_AC_PERIOD) {
            Button.state[i] += 2 * BUTTON_AC_PERIOD;
          } else {
            Button.state[i] = state_filter;
            Button.debounced_state[i] = 1;
            if (first_change) {
              Button.last_state[i] = 1;
              Button.first_change = false;
            }
          }
        } else {
          if (Button.state[i] > 0x00) {
            Button.state[i]--;
            if (0x00 == Button.state[i]) {
              Button.debounced_state[i] = 0;
              Button.first_change = false;
            }
          }
        }
      } else {

        if (force_low) {                                       // Enabled with ButtonDebounce x2
          if (0 == Button.debounced_state[i]) {
            Button.state[i] = 0;                               // With noisy input keep current state 0 unless constant 1
          }
        }

        if (Button.state[i] > 0) {
          Button.state[i]--;
          if (0 == Button.state[i]) {
            Button.debounced_state[i] = 0;
          }
        }
      }
    }
  }
  Button.probe_mutex = false;
}

void ButtonInit(void) {
  bool ac_detect = (Settings->button_debounce % 10 == 9);
  Button.used = 0;
/*
  uint32_t last_used = 0;
*/
  for (uint32_t i = 0; i < MAX_KEYS_SET; i++) {
    Button.last_state[i] = NOT_PRESSED;
#ifdef ESP8266
    if ((0 == i) && ((SONOFF_DUAL == TasmotaGlobal.module_type) || (CH4 == TasmotaGlobal.module_type))) {
      bitSet(Button.used, i);            // This pin is used
    } else
#endif  // ESP8266
    if (PinUsed(GPIO_KEY1, i)) {
      bitSet(Button.used, i);            // This pin is used
#ifdef ESP8266
      pinMode(Pin(GPIO_KEY1, i), bitRead(Button.no_pullup_mask, i) ? INPUT : ((16 == Pin(GPIO_KEY1, i)) ? INPUT_PULLDOWN_16 : INPUT_PULLUP));
#endif  // ESP8266
#ifdef ESP32
      pinMode(Pin(GPIO_KEY1, i), bitRead(Button.pulldown_mask, i) ? INPUT_PULLDOWN : bitRead(Button.no_pullup_mask, i) ? INPUT : INPUT_PULLUP);
#endif  // ESP32
      // Set global now so doesn't change the saved power state on first button check
      Button.last_state[i] = (digitalRead(Pin(GPIO_KEY1, i)) != bitRead(Button.inverted_mask, i));
      if (ac_detect) {
        Button.state[i] = 0x80 + 2 * BUTTON_AC_PERIOD;
        Button.last_state[i] = 0;				 // Will set later in the debouncing code
      }
    }
#ifdef USE_ADC
    else if (PinUsed(GPIO_ADC_BUTTON, i) || PinUsed(GPIO_ADC_BUTTON_INV, i)) {
      bitSet(Button.used, i);            // This pin is used
    }
#endif  // USE_ADC
    else {
      // Insert, Skip and Append virtual buttons
      XdrvMailbox.index = i;
      if (XdrvCall(FUNC_ADD_BUTTON)) {
        // At entry:
        //   XdrvMailbox.index = button index
        // At exit:
        //   XdrvMailbox.index bit 0 = current state
        bitSet(Button.used, i);                // This pin is used
        bool state = (XdrvMailbox.index &1);
        ButtonSetVirtualPinState(i, state);    // Virtual hardware pin state
        if (!state) { ButtonInvertFlag(i); }   // Set inverted flag
        // last_state[i] must be 1 to indicate no button pressed
        Button.last_state[i] = (bitRead(Button.virtual_pin, i) != bitRead(Button.inverted_mask, i));

        AddLog(LOG_LEVEL_DEBUG, PSTR("BTN: Add vButton%d, State %d"), i +1, Button.last_state[i]);
      }
    }
    Button.debounced_state[i] = Button.last_state[i];
/*
    if (bitRead(Button.used, i)) {
      last_used = i +1;
    }
*/
  }

/*
  // Append virtual buttons
  for (uint32_t i = last_used; i < MAX_KEYS_SET; i++) {
    Button.last_state[i] = NOT_PRESSED;

    XdrvMailbox.index = i;
    if (XdrvCall(FUNC_ADD_BUTTON)) {
      // At entry:
      //   XdrvMailbox.index = button index
      // At exit:
      //   XdrvMailbox.index bit 0 = current state
      bitSet(Button.used, i);                // This pin is used
      bool state = (XdrvMailbox.index &1);
      ButtonSetVirtualPinState(i, state);    // Virtual hardware pin state
      if (!state) { ButtonInvertFlag(i); }   // Set inverted flag
      // last_state[i] must be 1 to indicate no button pressed
      Button.last_state[i] = (bitRead(Button.virtual_pin, i) != bitRead(Button.inverted_mask, i));

      AddLog(LOG_LEVEL_DEBUG, PSTR("BTN: Add vButton%d, State %d"), i +1, Button.last_state[i]);
    }

    Button.debounced_state[i] = Button.last_state[i];
  }
*/

//  AddLog(LOG_LEVEL_DEBUG, PSTR("BTN: vPinUsed %08X, State %08X, Invert %08X"), Button.used, Button.virtual_pin, Button.inverted_mask);

  if (Button.used) {                     // Any bit set
    Button.first_change = true;
    TickerButton.attach_ms((ac_detect) ? BUTTON_FAST_PROBE_INTERVAL : BUTTON_PROBE_INTERVAL, ButtonProbe);
  }
}

uint8_t ButtonSerial(uint8_t serial_in_byte) {
  if (Button.dual_receive_count) {
    Button.dual_receive_count--;
    if (Button.dual_receive_count) {
      Button.dual_code = (Button.dual_code << 8) | serial_in_byte;
      serial_in_byte = 0;
    } else {
      if (serial_in_byte != 0xA1) {
        Button.dual_code = 0;                                  // 0xA1 - End of Sonoff dual button code
      }
    }
  }
  if (0xA0 == serial_in_byte) {                                // 0xA0 - Start of Sonoff dual button code
    serial_in_byte = 0;
    Button.dual_code = 0;
    Button.dual_receive_count = 3;
  }

  return serial_in_byte;
}

/*********************************************************************************************\
 * Button handler with single press only or multi-press and hold on all buttons
 *
 * ButtonDebounce (50) - Debounce time in mSec
 * SetOption1  (0)     - If set do not execute commands WifiConfig and Reset
 * SetOption11 (0)     - If set perform single press action on double press and reverse (on two relay devices only)
 * SetOption13 (0)     - If set act on single press only
 * SetOption32 (40)    - Button held for factor times longer
 * SetOption40 (0)     - Do not ignore button hold
 * SetOption73 (0)     - Decouple button from relay and send just mqtt topic
\*********************************************************************************************/

void ButtonHandler(void) {
  if (TasmotaGlobal.uptime < 4) { return; }                    // Block GPIO for 4 seconds after poweron to workaround Wemos D1 / Obi RTS circuit

  uint8_t hold_time_extent = IMMINENT_RESET_FACTOR;            // Extent hold time factor in case of iminnent Reset command
  uint16_t loops_per_second = 1000 / Settings->button_debounce;  // ButtonDebounce (50)
  char scmnd[20];

  for (uint32_t button_index = 0; button_index < MAX_KEYS_SET; button_index++) {
    if (!bitRead(Button.used, button_index)) { continue; }

    uint8_t button = Button.debounced_state[button_index];

#ifdef ESP8266
    if (!button_index && ((SONOFF_DUAL == TasmotaGlobal.module_type) || (CH4 == TasmotaGlobal.module_type))) {
      if (Button.dual_code) {
        AddLog(LOG_LEVEL_DEBUG, PSTR("BTN: Code %04X"), Button.dual_code);
        button = PRESSED;
        if (0xF500 == Button.dual_code) {                      // Button hold
          Button.hold_timer[button_index] = (loops_per_second * Settings->param[P_HOLD_TIME] / 10) -1;  // SetOption32 (40)
          hold_time_extent = 1;
        }
        Button.dual_code = 0;
      } else {
        button = NOT_PRESSED;
      }
    } else
#endif  // ESP8266
    if (PinUsed(GPIO_KEY1, button_index)) {

#if defined(SOC_TOUCH_VERSION_1) || defined(SOC_TOUCH_VERSION_2)
      if (bitRead(TouchButton.touch_mask, button_index) && bitRead(TouchButton.calibration, button_index +1)) {  // Touch
        uint32_t _value = touchRead(Pin(GPIO_KEY1, button_index));
#ifdef SOC_TOUCH_VERSION_2
        if (_value > Settings->touch_threshold) {  // ESPS3 No touch = 24200, Touch = 100000
#else
        if ((_value > 0) && (_value < Settings->touch_threshold)) {  // ESP32 No touch = 74, Touch = 20 (Probably read-error (0))
#endif
          TouchButton.hits[button_index]++;
        } else {
          TouchButton.hits[button_index] = 0;
        }
        AddLog(LOG_LEVEL_INFO, PSTR("PLOT: %u, %u, %u,"), button_index +1, _value, TouchButton.hits[button_index]);  // Button number (1..4), value, continuous hits under threshold
        continue;
      }
#endif  // ESP32 SOC_TOUCH_VERSION_1 or SOC_TOUCH_VERSION_2

    }
#ifdef USE_ADC
#ifndef FIRMWARE_MINIMAL
    else if (PinUsed(GPIO_ADC_BUTTON, button_index)) {
      button = AdcGetButton(Pin(GPIO_ADC_BUTTON, button_index));
    }
    else if (PinUsed(GPIO_ADC_BUTTON_INV, button_index)) {
      button = AdcGetButton(Pin(GPIO_ADC_BUTTON_INV, button_index));
    }
#endif  // FIRMWARE_MINIMAL
#endif  // USE_ADC

    XdrvMailbox.index = button_index;
    XdrvMailbox.payload = button;
    XdrvMailbox.command_code = (Button.last_state[button_index] & 0xFF) | ((Button.press_counter[button_index] & 0xFF) << 8);
    if (XdrvCall(FUNC_BUTTON_PRESSED)) {
      // Serviced
    }
#ifdef ESP8266
    else if (SONOFF_4CHPRO == TasmotaGlobal.module_type) {
      if (Button.hold_timer[button_index]) { Button.hold_timer[button_index]--; }

      bool button_pressed = false;
      if ((PRESSED == button) && (NOT_PRESSED == Button.last_state[button_index])) {
        AddLog(LOG_LEVEL_DEBUG, PSTR("BTN: Button%d level 1-0"), button_index +1);
        Button.hold_timer[button_index] = loops_per_second;
        button_pressed = true;
      }
      if ((NOT_PRESSED == button) && (PRESSED == Button.last_state[button_index])) {
        AddLog(LOG_LEVEL_DEBUG, PSTR("BTN: Button%d level 0-1"), button_index +1);
        if (!Button.hold_timer[button_index]) { button_pressed = true; }  // Do not allow within 1 second
      }
      if (button_pressed) {
        if (!Settings->flag3.mqtt_buttons) {                 // SetOption73 (0) - Decouple button from relay and send just mqtt topic
          if (!SendKey(KEY_BUTTON, button_index +1, POWER_TOGGLE)) {  // Execute Toggle command via MQTT if ButtonTopic is set
            ExecuteCommandPower(button_index +1, POWER_TOGGLE, SRC_BUTTON);  // Execute Toggle command internally
          }
        } else {
          MqttButtonTopic(button_index +1, 1, 0);            // SetOption73 (0) - Decouple button from relay and send just mqtt topic
        }
      }
    }
#endif  // ESP8266
    else {
      if ((PRESSED == button) && (NOT_PRESSED == Button.last_state[button_index])) {

        if (Settings->flag.button_single) {                  // SetOption13 (0) - Allow only single button press for immediate action,
          if (!Settings->flag3.mqtt_buttons) {               // SetOption73 (0) - Decouple button from relay and send just mqtt topic
            AddLog(LOG_LEVEL_DEBUG, PSTR("BTN: Button%d immediate"), button_index +1);
            if (!SendKey(KEY_BUTTON, button_index +1, POWER_TOGGLE)) {  // Execute Toggle command via MQTT if ButtonTopic is set
              ExecuteCommandPower(button_index +1, POWER_TOGGLE, SRC_BUTTON);  // Execute Toggle command internally
            }
          } else {
            MqttButtonTopic(button_index +1, 1, 0);          // SetOption73 1 - Decouple button from relay and send just mqtt topic
          }
        } else {
          Button.press_counter[button_index] = (Button.window_timer[button_index]) ? Button.press_counter[button_index] +1 : 1;
          AddLog(LOG_LEVEL_DEBUG, PSTR("BTN: Button%d multi-press %d"), button_index +1, Button.press_counter[button_index]);
          Button.window_timer[button_index] = uint32_t(DOUBLE_CLICK_WINDOW * loops_per_second) / 1000;
        }
        TasmotaGlobal.blinks = 201;
      }

      if (NOT_PRESSED == button) {
        Button.hold_timer[button_index] = 0;
        if (Settings->flag3.mqtt_buttons && (PRESSED == Button.last_state[button_index]) && !Button.press_counter[button_index]) { // SetOption73 (0) - Decouple button from relay and send just mqtt topic
          MqttButtonTopic(button_index +1, 6, 0);
        }
      } else {
        Button.hold_timer[button_index]++;
        if (Settings->flag.button_single) {                  // SetOption13 (0) - Allow only single button press for immediate action
          if (Button.hold_timer[button_index] == loops_per_second * hold_time_extent * Settings->param[P_HOLD_TIME] / 10) {  // SetOption32 (40) - Button held for factor times longer
            snprintf_P(scmnd, sizeof(scmnd), PSTR(D_CMND_SETOPTION "13 0"));  // Disable single press only
            ExecuteCommand(scmnd, SRC_BUTTON);
          }
        } else {
          if (Button.hold_timer[button_index] == loops_per_second * Settings->param[P_HOLD_TIME] / 10) {  // SetOption32 (40) - Button hold
            Button.press_counter[button_index] = 0;
            if (Settings->flag3.mqtt_buttons) {              // SetOption73 (0) - Decouple button from relay and send just mqtt topic
              MqttButtonTopic(button_index +1, 3, 1);
            } else {
              SendKey(KEY_BUTTON, button_index +1, POWER_HOLD);  // Execute Hold command via MQTT if ButtonTopic is set
            }
          } else {
            if (Settings->flag.button_restrict) {            // SetOption1 (0) - Control button multipress
              if (Settings->param[P_HOLD_IGNORE] > 0) {      // SetOption40 (0) - Do not ignore button hold
                if (Button.hold_timer[button_index] > loops_per_second * Settings->param[P_HOLD_IGNORE] / 10) {
                  Button.hold_timer[button_index] = 0;       // Reset button hold counter to stay below hold trigger
                  Button.press_counter[button_index] = 0;    // Discard button press to disable functionality
                }
              }
            } else {
              if ((Button.hold_timer[button_index] == loops_per_second * hold_time_extent * Settings->param[P_HOLD_TIME] / 10)) {  // SetOption32 (40) - Button held for factor times longer
                Button.press_counter[button_index] = 0;
                snprintf_P(scmnd, sizeof(scmnd), PSTR(D_CMND_RESET " 1"));
                ExecuteCommand(scmnd, SRC_BUTTON);
              }
            }
          }
        }
      }

      if (!Settings->flag.button_single) {                   // SetOption13 (0) - Allow multi-press
        if (Button.window_timer[button_index]) {
          Button.window_timer[button_index]--;
        } else {
          if (!TasmotaGlobal.restart_flag && !Button.hold_timer[button_index] && (Button.press_counter[button_index] > 0) && (Button.press_counter[button_index] < 7)) {

            bool single_press = false;
            if (Button.press_counter[button_index] < 3) {    // Single or Double press
#ifdef ESP8266
              if ((SONOFF_DUAL_R2 == TasmotaGlobal.module_type) || (SONOFF_DUAL == TasmotaGlobal.module_type) || (CH4 == TasmotaGlobal.module_type)) {
                single_press = true;
              } else
#endif  // ESP8266
              {
                single_press = (Settings->flag.button_swap +1 == Button.press_counter[button_index]);  // SetOption11 (0)
                if ((1 == Button.used) && (2 == TasmotaGlobal.devices_present)) {  // Single Button with two devices only
                  if (Settings->flag.button_swap) {          // SetOption11 (0)
                    Button.press_counter[button_index] = (single_press) ? 1 : 2;
                  }
                }
              }
            }

            XdrvMailbox.index = button_index;
            XdrvMailbox.payload = Button.press_counter[button_index];
            if (XdrvCall(FUNC_BUTTON_MULTI_PRESSED)) {
              // Serviced
//                AddLog(LOG_LEVEL_DEBUG, PSTR("BTN: FUNC_BUTTON_MULTI_PRESSED serviced"));
            } else

#ifdef ROTARY_V1
            if (!RotaryButtonPressed(button_index)) {
#endif
              if (!Settings->flag3.mqtt_buttons && single_press && SendKey(KEY_BUTTON, button_index + Button.press_counter[button_index], POWER_TOGGLE)) {  // Execute Toggle command via MQTT if ButtonTopic is set
                // Success
              } else {
                if (Button.press_counter[button_index] < 6) { // Single to Penta press
//                    if (WifiState() > WIFI_RESTART) {           // Wifimanager active
//                      TasmotaGlobal.restart_flag = 1;
//                    }
                  if (!Settings->flag3.mqtt_buttons) {       // SetOption73 - Detach buttons from relays and enable MQTT action state for multipress
                    if (Button.press_counter[button_index] == 1) {  // By default first press always send a TOGGLE (2)
                      ExecuteCommandPower(button_index + Button.press_counter[button_index], POWER_TOGGLE, SRC_BUTTON);
                    } else {
                      SendKey(KEY_BUTTON, button_index +1, Button.press_counter[button_index] +9);    // 2,3,4 and 5 press send just the key value (11,12,13 and 14) for rules
                      if (0 == button_index) {               // BUTTON1 can toggle up to 5 relays if present. If a relay is not present will send out the key value (2,11,12,13 and 14) for rules
                        uint32_t max_device = (TasmotaGlobal.devices_present < MAX_RELAY_BUTTON1) ? TasmotaGlobal.devices_present : MAX_RELAY_BUTTON1;
                        if ((Button.press_counter[button_index] > 1) && (Button.press_counter[button_index] <= max_device)) {
                          ExecuteCommandPower(button_index + Button.press_counter[button_index], POWER_TOGGLE, SRC_BUTTON);   // Execute Toggle command internally
                        }
                      }
                    }
                  }

                } else {    // 6 press start wificonfig 2
                  if (!Settings->flag.button_restrict) {     // SetOption1  - Control button multipress
                    snprintf_P(scmnd, sizeof(scmnd), PSTR(D_CMND_WIFICONFIG " 2"));
                    ExecuteCommand(scmnd, SRC_BUTTON);
                  }
                }
                if (Settings->flag3.mqtt_buttons) {          // SetOption73 (0) - Decouple button from relay and send just mqtt topic
                  if (Button.press_counter[button_index] >= 1 && Button.press_counter[button_index] <= 5) {
                    MqttButtonTopic(button_index +1, Button.press_counter[button_index], 0);
                  }
                }
              }
#ifdef ROTARY_V1
            }
#endif
            Button.press_counter[button_index] = 0;
          }
        }
      }

    }
    Button.last_state[button_index] = button;
  }
}

void MqttButtonTopic(uint32_t button_id, uint32_t action, uint32_t hold) {
  SendKey(KEY_BUTTON, button_id, (hold) ? 3 : action +9);

  if (!Settings->flag.hass_discovery) {                        // SetOption19 - Control Home Assistant automatic discovery (See SetOption59)
    char scommand[10];
    snprintf_P(scommand, sizeof(scommand), PSTR(D_JSON_BUTTON "%d"), button_id);
    char mqttstate[7];
    Response_P(S_JSON_SVALUE_ACTION_SVALUE, scommand, (hold) ? SettingsText(SET_STATE_TXT4) : GetTextIndexed(mqttstate, sizeof(mqttstate), action, kMultiPress));
    MqttPublishPrefixTopicRulesProcess_P(RESULT_OR_STAT, scommand);
  }
}

void ButtonLoop(void) {
  if (Button.used) {
    if (TimeReached(Button.debounce)) {
      SetNextTimeInterval(Button.debounce, Settings->button_debounce);  // ButtonDebounce (50)
      ButtonHandler();
    }
  }
}

/********************************************************************************************/
/*
 * Custom Reset Button Support
 * GPIO 9 with pull-down resistor, 7-second hold to trigger reset
 */

#define CUSTOM_RESET_BUTTON_PIN 9
#define CUSTOM_RESET_HOLD_TIME  7000  // 7 seconds in milliseconds
#define STATUS_LED_PIN 2

// LED Status patterns
#define LED_STATUS_BOOT       1  // Fast blink during boot
#define LED_STATUS_WIFI_CONN  2  // Medium blink while connecting to WiFi
#define LED_STATUS_MQTT_CONN  3  // Slow blink while connecting to MQTT
#define LED_STATUS_NORMAL     4  // Solid on when everything is connected
#define LED_STATUS_ERROR      5  // Very fast blink on error
#define LED_STATUS_RESET      6  // Rapid blink before reset

struct CUSTOM_RESET_BUTTON {
  uint32_t button_press_start_time = 0;
  uint32_t last_button_state = 1;      // Assume high (not pressed) initially due to pull-down
  uint32_t last_check_time = 0;
  bool button_pressed_logged = false;
  bool seven_second_logged = false;
  bool initialized = false;
} CustomResetButton;

struct STATUS_LED {
  uint32_t last_blink_time = 0;
  uint32_t blink_interval = 500;       // Default blink interval in ms
  uint8_t current_status = LED_STATUS_BOOT;
  uint8_t last_status = 0;
  bool led_state = false;              // Current LED state (on/off)
  bool initialized = false;
} StatusLed;

void CustomResetButtonInit(void) {
  if (!CustomResetButton.initialized) {
    // Initialize GPIO 9 as input with pull-down resistor
    pinMode(CUSTOM_RESET_BUTTON_PIN, INPUT_PULLDOWN);
    CustomResetButton.last_check_time = millis();
    CustomResetButton.initialized = true;

    AddLog(LOG_LEVEL_INFO, PSTR("CRB: Custom reset button initialized on GPIO %d"), CUSTOM_RESET_BUTTON_PIN);
  }
}

void CustomResetButtonLoop(void) {
  if (!CustomResetButton.initialized) {
    return;
  }

  uint32_t current_time = millis();

  // Check button every 50ms to avoid excessive polling
  if (current_time - CustomResetButton.last_check_time < 50) {
    return;
  }
  CustomResetButton.last_check_time = current_time;

  uint32_t current_button_state = digitalRead(CUSTOM_RESET_BUTTON_PIN);

  // Detect button press (transition from high to low, since we're using pull-down)
  if ((CustomResetButton.last_button_state == 1) && (current_button_state == 0)) {
    // Button just pressed
    CustomResetButton.button_press_start_time = current_time;
    CustomResetButton.button_pressed_logged = false;
    CustomResetButton.seven_second_logged = false;

    AddLog(LOG_LEVEL_INFO, PSTR("CRB: Reset button pressed"));
    CustomResetButton.button_pressed_logged = true;
  }

  // Check if button is currently pressed and held
  if (current_button_state == 0 && CustomResetButton.button_press_start_time > 0) {
    uint32_t press_duration = current_time - CustomResetButton.button_press_start_time;

    // Check if held for 7 seconds
    if (press_duration >= CUSTOM_RESET_HOLD_TIME && !CustomResetButton.seven_second_logged) {
      AddLog(LOG_LEVEL_INFO, PSTR("CRB: Reset button pressed for 7 seconds - triggering reset"));
      CustomResetButton.seven_second_logged = true;

      // Set LED to reset pattern
      StatusLedSetStatus(LED_STATUS_RESET);

      // Trigger reset by setting restart flag directly (same as CmndReset with payload 1)
      TasmotaGlobal.restart_flag = 211;
    }
    // Show reset pattern when button is held for more than 5 seconds (warning)
    else if (press_duration >= 5000 && press_duration < CUSTOM_RESET_HOLD_TIME) {
      StatusLedSetStatus(LED_STATUS_RESET);
    }
  }

  // Detect button release
  if ((CustomResetButton.last_button_state == 0) && (current_button_state == 1)) {
    // Button just released
    if (CustomResetButton.button_press_start_time > 0) {
      uint32_t press_duration = current_time - CustomResetButton.button_press_start_time;
      if (press_duration < CUSTOM_RESET_HOLD_TIME) {
        AddLog(LOG_LEVEL_INFO, PSTR("CRB: Reset button released after %d ms (not held long enough)"), press_duration);
      } else {
        AddLog(LOG_LEVEL_INFO, PSTR("CRB: Reset button released after %d ms"), press_duration);
      }
    }
    CustomResetButton.button_press_start_time = 0;
  }

  CustomResetButton.last_button_state = current_button_state;
}

void StatusLedInit(void) {
  if (!StatusLed.initialized) {
    // Initialize GPIO 2 as output for status LED
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);  // Start with LED off
    StatusLed.last_blink_time = millis();
    StatusLed.initialized = true;

    AddLog(LOG_LEVEL_INFO, PSTR("STS: Status LED initialized on GPIO %d"), STATUS_LED_PIN);
  }
}

void StatusLedSetStatus(uint8_t status) {
  if (StatusLed.current_status != status) {
    StatusLed.last_status = StatusLed.current_status;
    StatusLed.current_status = status;

    // Set blink intervals for different statuses
    switch (status) {
      case LED_STATUS_BOOT:
        StatusLed.blink_interval = 200;  // Fast blink (200ms)
        AddLog(LOG_LEVEL_DEBUG, PSTR("STS: LED status set to BOOT"));
        break;
      case LED_STATUS_WIFI_CONN:
        StatusLed.blink_interval = 500;  // Medium blink (500ms)
        AddLog(LOG_LEVEL_DEBUG, PSTR("STS: LED status set to WIFI_CONNECTING"));
        break;
      case LED_STATUS_MQTT_CONN:
        StatusLed.blink_interval = 1000; // Slow blink (1000ms)
        AddLog(LOG_LEVEL_DEBUG, PSTR("STS: LED status set to MQTT_CONNECTING"));
        break;
      case LED_STATUS_NORMAL:
        StatusLed.blink_interval = 0;    // Solid on
        StatusLed.led_state = true;
        digitalWrite(STATUS_LED_PIN, HIGH);
        AddLog(LOG_LEVEL_DEBUG, PSTR("STS: LED status set to NORMAL (solid on)"));
        break;
      case LED_STATUS_ERROR:
        StatusLed.blink_interval = 100;  // Very fast blink (100ms)
        AddLog(LOG_LEVEL_DEBUG, PSTR("STS: LED status set to ERROR"));
        break;
      case LED_STATUS_RESET:
        StatusLed.blink_interval = 50;   // Rapid blink (50ms)
        AddLog(LOG_LEVEL_DEBUG, PSTR("STS: LED status set to RESET"));
        break;
    }
  }
}

void StatusLedLoop(void) {
  if (!StatusLed.initialized) {
    return;
  }

  uint32_t current_time = millis();

  // Handle solid on state (normal operation)
  if (StatusLed.blink_interval == 0) {
    return;  // LED already set to solid on, no need to blink
  }

  // Handle blinking states
  if (current_time - StatusLed.last_blink_time >= StatusLed.blink_interval) {
    StatusLed.led_state = !StatusLed.led_state;
    digitalWrite(STATUS_LED_PIN, StatusLed.led_state ? HIGH : LOW);
    StatusLed.last_blink_time = current_time;
  }
}

void StatusLedCheckSystemState(void) {
  // Check system state and update LED accordingly
  static uint32_t last_state_check = 0;
  uint32_t current_time = millis();

  // Only check every 2 seconds to avoid excessive checking
  if (current_time - last_state_check < 2000) {
    return;
  }
  last_state_check = current_time;

  // Determine current system state
  uint8_t new_status = LED_STATUS_BOOT;

  // Check if we're in restart mode
  if (TasmotaGlobal.restart_flag) {
    new_status = LED_STATUS_RESET;
  }
  // Check WiFi connection status
  else if (WiFi.status() == WL_CONNECTED) {
    // WiFi is connected, check MQTT
    if (TasmotaGlobal.mqtt_data[0] != 0) {  // Basic MQTT activity check
      new_status = LED_STATUS_NORMAL;
    } else {
      new_status = LED_STATUS_MQTT_CONN;
    }
  }
  // WiFi not connected
  else {
    new_status = LED_STATUS_WIFI_CONN;
  }

  // Update LED status if changed
  StatusLedSetStatus(new_status);
}

#endif  // BUTTON_V4
