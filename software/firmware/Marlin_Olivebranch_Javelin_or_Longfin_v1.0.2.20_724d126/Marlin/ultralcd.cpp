/*
    Reprap firmware based on Sprinter and grbl.
 Copyright (C) 2011 Camiel Gubbels / Erik van der Zalm
 Copyright (C) 2015-2016 Aleph Objects Inc.

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

#include "temperature.h"
#include "ultralcd.h"
#ifdef ULTRA_LCD
#include "Marlin.h"
#include "language.h"
#include "cardreader.h"
#include "temperature.h"
#include "stepper.h"
#include "ConfigurationStore.h"

int8_t encoderDiff; /* encoderDiff is updated from interrupt context and added to encoderPosition every LCD update */

/* Configuration settings */
int plaPreheatHotendTemp;
int plaPreheatHPBTemp;
int plaPreheatFanSpeed;

int absPreheatHotendTemp;
int absPreheatHPBTemp;
int absPreheatFanSpeed;

#ifdef FILAMENT_LCD_DISPLAY
  unsigned long message_millis = 0;
#endif

#ifdef BABYSTEPPING
  bool z_offsetting = false;
#endif

#ifdef ULTIPANEL
  static float manual_feedrate[] = MANUAL_FEEDRATE;
#endif // ULTIPANEL

/* !Configuration settings */

//Function pointer to menu functions.
typedef void (*menuFunc_t)();

uint8_t lcd_status_message_level;
char lcd_status_message[LCD_WIDTH+1] = WELCOME_MSG;

#ifdef DOGLCD
#include "dogm_lcd_implementation.h"
#else
#include "ultralcd_implementation_hitachi_HD44780.h"
#endif

/** forward declarations **/

void copy_and_scalePID_i();
void copy_and_scalePID_d();

/* Different menus */
static void lcd_status_screen();
#ifdef ULTIPANEL
extern bool powersupply;
static void lcd_main_menu();
static void lcd_tune_menu();
//static void lcd_change_temperature_menu();
static void lcd_movement_menu();
static void lcd_move_menu();
static void lcd_configuration_menu();
static void lcd_control_temperature_menu();
static void lcd_control_temperature_preheat_pla_settings_menu();
static void lcd_control_temperature_preheat_abs_settings_menu();
static void lcd_control_advanced_menu();
static void lcd_control_volumetric_menu();
#ifdef DOGLCD
static void lcd_set_contrast();
#endif
static void lcd_control_retract_menu();
static void lcd_sdcard_menu();

#ifdef RESUME_FEATURE
  static void resume_menu();
  bool resume_selected = false;
  bool selected_resume_once = false;
  bool call_lcd_sdcard_menu = false;
  bool resume_print = false;
  bool examined_once = false;
  bool resume_z = false;
  extern float planner_disabled_below_z;
  extern float last_z;
  extern bool z_reached;
  extern bool layer_reached;
  extern bool hops;
  extern bool gone_up;
  char contfilename[26];
  char resumefilename[14] = "RESUME~1.GCO\0";
#endif //RESUME_FEATURE

#ifdef DELTA_CALIBRATION_MENU
static void lcd_delta_calibrate_menu();
#endif // DELTA_CALIBRATION_MENU

static void lcd_quick_feedback();//Cause an LCD refresh, and give the user visual or audible feedback that something has happened

/* Different types of actions that can be used in menu items. */
static void menu_action_back(menuFunc_t data);
static void menu_action_submenu(menuFunc_t data);
static void menu_action_gcode(const char* pgcode);
static void menu_action_function(menuFunc_t data);
static void menu_action_sdfile(const char* filename, char* longFilename);
static void menu_action_sddirectory(const char* filename, char* longFilename);
static void menu_action_setting_edit_bool(const char* pstr, bool* ptr);
static void menu_action_setting_edit_int3(const char* pstr, int* ptr, int minValue, int maxValue);
static void menu_action_setting_edit_float3(const char* pstr, float* ptr, float minValue, float maxValue);
static void menu_action_setting_edit_float32(const char* pstr, float* ptr, float minValue, float maxValue);
static void menu_action_setting_edit_float43(const char* pstr, float* ptr, float minValue, float maxValue);
static void menu_action_setting_edit_float5(const char* pstr, float* ptr, float minValue, float maxValue);
static void menu_action_setting_edit_float51(const char* pstr, float* ptr, float minValue, float maxValue);
static void menu_action_setting_edit_float52(const char* pstr, float* ptr, float minValue, float maxValue);
static void menu_action_setting_edit_long5(const char* pstr, unsigned long* ptr, unsigned long minValue, unsigned long maxValue);
static void menu_action_setting_edit_long6(const char* pstr, unsigned long* ptr, unsigned long minValue, unsigned long maxValue);
static void menu_action_setting_edit_callback_bool(const char* pstr, bool* ptr, menuFunc_t callbackFunc);
static void menu_action_setting_edit_callback_int3(const char* pstr, int* ptr, int minValue, int maxValue, menuFunc_t callbackFunc);
static void menu_action_setting_edit_callback_float3(const char* pstr, float* ptr, float minValue, float maxValue, menuFunc_t callbackFunc);
static void menu_action_setting_edit_callback_float32(const char* pstr, float* ptr, float minValue, float maxValue, menuFunc_t callbackFunc);
static void menu_action_setting_edit_callback_float43(const char* pstr, float* ptr, float minValue, float maxValue, menuFunc_t callbackFunc);
static void menu_action_setting_edit_callback_float5(const char* pstr, float* ptr, float minValue, float maxValue, menuFunc_t callbackFunc);
static void menu_action_setting_edit_callback_float51(const char* pstr, float* ptr, float minValue, float maxValue, menuFunc_t callbackFunc);
static void menu_action_setting_edit_callback_float52(const char* pstr, float* ptr, float minValue, float maxValue, menuFunc_t callbackFunc);
static void menu_action_setting_edit_callback_long5(const char* pstr, unsigned long* ptr, unsigned long minValue, unsigned long maxValue, menuFunc_t callbackFunc);

#define ENCODER_FEEDRATE_DEADZONE 10

#if !defined(LCD_I2C_VIKI)
  #ifndef ENCODER_STEPS_PER_MENU_ITEM
    #define ENCODER_STEPS_PER_MENU_ITEM 5
  #endif
  #ifndef ENCODER_PULSES_PER_STEP
    #define ENCODER_PULSES_PER_STEP 1
  #endif
#else
  #ifndef ENCODER_STEPS_PER_MENU_ITEM
    #define ENCODER_STEPS_PER_MENU_ITEM 2 // VIKI LCD rotary encoder uses a different number of steps per rotation
  #endif
  #ifndef ENCODER_PULSES_PER_STEP
    #define ENCODER_PULSES_PER_STEP 1
  #endif
#endif


/* Helper macros for menus */
#define START_MENU() do { \
    if (encoderPosition > 0x8000) encoderPosition = 0; \
    if (encoderPosition / ENCODER_STEPS_PER_MENU_ITEM < currentMenuViewOffset) currentMenuViewOffset = encoderPosition / ENCODER_STEPS_PER_MENU_ITEM;\
    uint8_t _lineNr = currentMenuViewOffset, _menuItemNr; \
    bool wasClicked = LCD_CLICKED;\
    for(uint8_t _drawLineNr = 0; _drawLineNr < LCD_HEIGHT; _drawLineNr++, _lineNr++) { \
        _menuItemNr = 0;
#define MENU_ITEM(type, label, args...) do { \
    if (_menuItemNr == _lineNr) { \
        if (lcdDrawUpdate) { \
            const char* _label_pstr = PSTR(label); \
            if ((encoderPosition / ENCODER_STEPS_PER_MENU_ITEM) == _menuItemNr) { \
                lcd_implementation_drawmenu_ ## type ## _selected (_drawLineNr, _label_pstr , ## args ); \
            }else{\
                lcd_implementation_drawmenu_ ## type (_drawLineNr, _label_pstr , ## args ); \
            }\
        }\
        if (wasClicked && (encoderPosition / ENCODER_STEPS_PER_MENU_ITEM) == _menuItemNr) {\
            lcd_quick_feedback(); \
            menu_action_ ## type ( args ); \
            return;\
        }\
    }\
    _menuItemNr++;\
} while(0)
#define MENU_ITEM_DUMMY() do { _menuItemNr++; } while(0)
#define MENU_ITEM_EDIT(type, label, args...) MENU_ITEM(setting_edit_ ## type, label, PSTR(label) , ## args )
#define MENU_ITEM_EDIT_CALLBACK(type, label, args...) MENU_ITEM(setting_edit_callback_ ## type, label, PSTR(label) , ## args )
#define END_MENU() \
    if (encoderPosition / ENCODER_STEPS_PER_MENU_ITEM >= _menuItemNr) encoderPosition = _menuItemNr * ENCODER_STEPS_PER_MENU_ITEM - 1; \
    if ((uint8_t)(encoderPosition / ENCODER_STEPS_PER_MENU_ITEM) >= currentMenuViewOffset + LCD_HEIGHT) { currentMenuViewOffset = (encoderPosition / ENCODER_STEPS_PER_MENU_ITEM) - LCD_HEIGHT + 1; lcdDrawUpdate = 1; _lineNr = currentMenuViewOffset - 1; _drawLineNr = -1; } \
    } } while(0)

/** Used variables to keep track of the menu */
#ifndef REPRAPWORLD_KEYPAD
volatile uint8_t buttons;//Contains the bits of the currently pressed buttons.
#else
volatile uint8_t buttons_reprapworld_keypad; // to store the reprapworld_keypad shift register values
#endif
#ifdef LCD_HAS_SLOW_BUTTONS
volatile uint8_t slow_buttons;//Contains the bits of the currently pressed buttons.
#endif
uint8_t currentMenuViewOffset;              /* scroll offset in the current menu */
uint32_t blocking_enc;
uint8_t lastEncoderBits;
uint32_t encoderPosition;
#if (SDCARDDETECT > 0)
bool lcd_oldcardstatus;
#endif
#endif //ULTIPANEL

menuFunc_t currentMenu = lcd_status_screen; /* function pointer to the currently active menu */
uint32_t lcd_next_update_millis;
uint8_t lcd_status_update_delay;
bool ignore_click = false;
bool wait_for_unclick;
uint8_t lcdDrawUpdate = 2;                  /* Set to none-zero when the LCD needs to draw, decreased after every draw. Set to 2 in LCD routines so the LCD gets at least 1 full redraw (first redraw is partial) */

//prevMenu and prevEncoderPosition are used to store the previous menu location when editing settings.
menuFunc_t prevMenu = NULL;
uint16_t prevEncoderPosition;
//Variables used when editing values.
const char* editLabel;
void* editValue;
int32_t minEditValue, maxEditValue;
menuFunc_t callbackFunc;

// place-holders for Ki and Kd edits
float raw_Ki, raw_Kd;

static void lcd_goto_menu(menuFunc_t menu, const uint32_t encoder=0, const bool feedback=true) {
  if (currentMenu != menu) {
    currentMenu = menu;
    encoderPosition = encoder;
    if (feedback) lcd_quick_feedback();

    // For LCD_PROGRESS_BAR re-initialize the custom characters
    #if defined(LCD_PROGRESS_BAR) && defined(SDSUPPORT)
      lcd_set_custom_characters(menu == lcd_status_screen);
    #endif
  }
}

/* Main status screen. It's up to the implementation specific part to show what is needed. As this is very display dependent */
static void lcd_status_screen()
{
  #if defined(LCD_PROGRESS_BAR) && defined(SDSUPPORT)
    uint16_t mil = millis();
    #ifndef PROGRESS_MSG_ONCE
      if (mil > progressBarTick + PROGRESS_BAR_MSG_TIME + PROGRESS_BAR_BAR_TIME) {
        progressBarTick = mil;
      }
    #endif
    #if PROGRESS_MSG_EXPIRE > 0
      // keep the message alive if paused, count down otherwise
      if (messageTick > 0) {
        if (card.isFileOpen()) {
          if (IS_SD_PRINTING) {
            if ((mil-messageTick) >= PROGRESS_MSG_EXPIRE) {
              lcd_status_message[0] = '\0';
              messageTick = 0;
            }
          }
          else {
            messageTick += LCD_UPDATE_INTERVAL;
          }
        }
        else {
          messageTick = 0;
        }
      }
    #endif
  #endif //LCD_PROGRESS_BAR

    if (lcd_status_update_delay)
        lcd_status_update_delay--;
    else
        lcdDrawUpdate = 1;
    if (lcdDrawUpdate)
    {
        lcd_implementation_status_screen();
        lcd_status_update_delay = 10;   /* redraw the main screen every second. This is easier then trying keep track of all things that change on the screen */
    }
#ifdef ULTIPANEL

#ifdef RESUME_FEATURE
    call_lcd_sdcard_menu = false;
#endif

    bool current_click = LCD_CLICKED;

    if (ignore_click) {
        if (wait_for_unclick) {
          if (!current_click) {
              ignore_click = wait_for_unclick = false;
          }
          else {
              current_click = false;
          }
        }
        else if (current_click) {
            lcd_quick_feedback();
            wait_for_unclick = true;
            current_click = false;
        }
    }

    if (current_click)
    {
        lcd_goto_menu(lcd_main_menu);
        lcd_implementation_init( // to maybe revive the LCD if static electricity killed it.
          #if defined(LCD_PROGRESS_BAR) && defined(SDSUPPORT)
            currentMenu == lcd_status_screen
          #endif
        );
        #ifdef FILAMENT_LCD_DISPLAY
          message_millis = millis();  // get status message to show up for a while
        #endif
    }

#ifdef ULTIPANEL_FEEDMULTIPLY
    // Dead zone at 100% feedrate
    if ((feedmultiply < 100 && (feedmultiply + int(encoderPosition)) > 100) ||
            (feedmultiply > 100 && (feedmultiply + int(encoderPosition)) < 100))
    {
        encoderPosition = 0;
        feedmultiply = 100;
    }

    if (feedmultiply == 100 && int(encoderPosition) > ENCODER_FEEDRATE_DEADZONE)
    {
        feedmultiply += int(encoderPosition) - ENCODER_FEEDRATE_DEADZONE;
        encoderPosition = 0;
    }
    else if (feedmultiply == 100 && int(encoderPosition) < -ENCODER_FEEDRATE_DEADZONE)
    {
        feedmultiply += int(encoderPosition) + ENCODER_FEEDRATE_DEADZONE;
        encoderPosition = 0;
    }
    else if (feedmultiply != 100)
    {
        feedmultiply += int(encoderPosition);
        encoderPosition = 0;
    }
#endif //ULTIPANEL_FEEDMULTIPLY

    if (feedmultiply < 10)
        feedmultiply = 10;
    else if (feedmultiply > 999)
        feedmultiply = 999;
#endif //ULTIPANEL
}

#ifdef ULTIPANEL

static void lcd_return_to_status() { lcd_goto_menu(lcd_status_screen, 0, false); }

static void lcd_sdcard_pause() { card.pauseSDPrint(); }

static void lcd_sdcard_resume() { card.startFileprint(); }

static void lcd_sdcard_stop()
{
    quickStop();
    card.sdprinting = false;
    card.closefile();
    #ifdef RESUME_FEATURE
      resume_print = false;
    #endif
    if(SD_FINISHED_STEPPERRELEASE)
    {
        enquecommand_P(PSTR(SD_FINISHED_RELEASECOMMAND));
    }
    autotempShutdown();

        disable_heater();

	cancel_heatup = true;

	LCD_MESSAGEPGM(MSG_PRINT_ABORTED);

        for(int i=0; i<EXTRUDERS; i++) target_temp_reached[i] = false;

    MYSERIAL.flush();
    clear_buffer(); //clear buffer

    #ifdef RESUME_FEATURE
      planner_disabled_below_z = 0.0;
      selected_resume_once = false;
      resume_z = false;
    #endif //RESUME_FEATURE
}

/* Menu implementation */
static void lcd_main_menu()
{
    #ifdef RESUME_FEATURE
      examined_once = false;
      if(call_lcd_sdcard_menu)
      {
        lcd_goto_menu(lcd_sdcard_menu);
        call_lcd_sdcard_menu = false;
        return;
      }
    #endif
    START_MENU();
    MENU_ITEM(back, MSG_WATCH, lcd_status_screen);
    if (movesplanned() || IS_SD_PRINTING)
    {
        MENU_ITEM(submenu, MSG_TUNE, lcd_tune_menu);
    }else{
        MENU_ITEM(submenu, MSG_MOVEMENT, lcd_movement_menu);
        MENU_ITEM(submenu, MSG_TEMPERATURE, lcd_control_temperature_menu);
#ifdef DELTA_CALIBRATION_MENU
        MENU_ITEM(submenu, MSG_DELTA_CALIBRATE, lcd_delta_calibrate_menu);
#endif // DELTA_CALIBRATION_MENU
    }
    MENU_ITEM(submenu, MSG_CONFIGURATION, lcd_configuration_menu);
#ifdef SDSUPPORT
    if (card.cardOK)
    {
        if (card.isFileOpen())
        {
            if (card.sdprinting)
                MENU_ITEM(function, MSG_PAUSE_PRINT, lcd_sdcard_pause);
            else
                MENU_ITEM(function, MSG_RESUME_PRINT, lcd_sdcard_resume);
            MENU_ITEM(function, MSG_STOP_PRINT, lcd_sdcard_stop);
        }else if (!movesplanned() && !IS_SD_PRINTING){
            MENU_ITEM(submenu, MSG_CARD_MENU, lcd_sdcard_menu);
#if SDCARDDETECT < 1
            MENU_ITEM(gcode, MSG_CNG_SDCARD, PSTR("M21"));  // SD-card changed by user
#endif
        }
    }else{
        MENU_ITEM(submenu, MSG_NO_CARD, lcd_sdcard_menu);
#if SDCARDDETECT < 1
        MENU_ITEM(gcode, MSG_INIT_SDCARD, PSTR("M21")); // Manually initialize the SD-card via user interface
#endif
    }
#endif
    #ifdef RESUME_FEATURE
      selected_resume_once = false;
    #endif
    END_MENU();
}

#ifdef SDSUPPORT
static void lcd_autostart_sd()
{
    card.lastnr=0;
    card.setroot();
    card.checkautostart(true);
}
#endif

void lcd_set_home_offsets()
{
    for(int8_t i=0; i < NUM_AXIS; i++) {
      if (i != E_AXIS) {
        add_homing[i] -= current_position[i];
        current_position[i] = 0.0;
      }
    }
    plan_set_position(0.0, 0.0, 0.0, current_position[E_AXIS]);

    // Audio feedback
    enquecommand_P(PSTR("M300 S659 P200"));
    enquecommand_P(PSTR("M300 S698 P200"));
    lcd_return_to_status();
}


#ifdef BABYSTEPPING
  static void _lcd_babystep(int axis, const char *msg) {
    if (/*encoderPosition != 0 && */((int)encoderPosition < 0 || -1.0*Z_PROBE_OFFSET_RANGE_MAX < zprobe_zoffset) && (zprobe_zoffset < -1.0*Z_PROBE_OFFSET_RANGE_MIN || (int)encoderPosition > 0)) {
      babystepsTodo[axis] += (int)encoderPosition;
      zprobe_zoffset -= (int)encoderPosition/axis_steps_per_unit[Z_AXIS];
      encoderPosition = 0;
      z_offsetting = true;
      lcdDrawUpdate = 1;
    }
    if (lcdDrawUpdate && axis != Z_AXIS) lcd_implementation_drawedit(msg, "");
    else if (lcdDrawUpdate && axis == Z_AXIS)
    {
      lcd_implementation_drawedit(msg, ftostr43(-1.0*zprobe_zoffset));
      uint8_t noz_pos = 6*(zprobe_zoffset);
      u8g.drawBitmapP(66,noz_pos,2,12,nozzle_bmp);
      u8g.drawBitmapP(60,24,3,1,offset_bedline_bmp);
      u8g.drawBitmapP(0,47,3,16,ccw_bmp);
      u8g.drawStr(27,60,"Z");
      u8g.drawBitmapP(34,49,2,10,down_arrow_bmp);
      u8g.drawBitmapP(85,47,3,16,cw_bmp);
      u8g.drawStr(107,60,"Z");
      u8g.drawBitmapP(113,51,2,10,up_arrow_bmp);
      if (offset_up)
      {
        u8g.setColorIndex(0);
        u8g.drawBox(113,46,16,13);
        u8g.setColorIndex(1);
        u8g.drawBitmapP(113,48,2,13,longup_arrow_bmp);
      }
      else
      {
        u8g.setColorIndex(0);
        u8g.drawBox(34,49,16,13);
        u8g.setColorIndex(1);
        u8g.drawBitmapP(34,49,2,13,longdown_arrow_bmp);
      }
    }
    if (LCD_CLICKED)
    {
      z_offsetting = false;
      Config_StoreZOffset();
      lcd_goto_menu(lcd_control_advanced_menu);
    }
  }
  static void lcd_babystep_x() { _lcd_babystep(X_AXIS, PSTR(MSG_BABYSTEPPING_X)); }
  static void lcd_babystep_y() { _lcd_babystep(Y_AXIS, PSTR(MSG_BABYSTEPPING_Y)); }
  static void lcd_babystep_z() { _lcd_babystep(Z_AXIS, PSTR(MSG_BABYSTEPPING_Z)); }
  static void lcd_babystep_zoffset() { _lcd_babystep(Z_AXIS, PSTR(MSG_ZPROBE_ZOFFSET)); }

#endif //BABYSTEPPING

static void lcd_tune_menu()
{
    START_MENU();
    MENU_ITEM(back, MSG_MAIN, lcd_main_menu);
    MENU_ITEM_EDIT(int3, MSG_SPEED, &feedmultiply, 10, 999);
#if TEMP_SENSOR_0 != 0
    MENU_ITEM_EDIT(int3, MSG_NOZZLE
    #if EXTRUDERS > 1
      "1"
    #endif
    , &target_temperature[0], 0, HEATER_0_MAXTEMP - 15);
#endif
#if EXTRUDERS > 1
    MENU_ITEM_EDIT(int3, MSG_NOZZLE1, &target_temperature[1], 0, HEATER_1_MAXTEMP - 15);
#endif
#if EXTRUDERS > 2
    MENU_ITEM_EDIT(int3, MSG_NOZZLE2, &target_temperature[2], 0, HEATER_2_MAXTEMP - 15);
#endif
#if EXTRUDERS > 3
    MENU_ITEM_EDIT(int3, MSG_NOZZLE3, &target_temperature[2], 0, HEATER_2_MAXTEMP - 15);
#endif
#if TEMP_SENSOR_BED != 0
    MENU_ITEM_EDIT(int3, MSG_BED, &target_temperature_bed, 0, BED_MAXTEMP - 15);
#endif
    MENU_ITEM_EDIT(int3, MSG_FLOW
    #if EXTRUDERS > 1
      " 1"
    #endif
    , &extrudemultiply, 10, 999);
//    MENU_ITEM_EDIT(int3, MSG_FLOW0, &extruder_multiply[0], 10, 999);
#if EXTRUDERS > 1
    MENU_ITEM_EDIT(int3, MSG_FLOW1, &extruder_multiply[1], 10, 999);
#endif
#if EXTRUDERS > 2
    MENU_ITEM_EDIT(int3, MSG_FLOW2, &extruder_multiply[2], 10, 999);
#endif
#if EXTRUDERS > 3
    MENU_ITEM_EDIT(int3, MSG_FLOW3, &extruder_multiply[2], 10, 999);
#endif
    MENU_ITEM_EDIT(int3, MSG_FAN_SPEED, &fanSpeed, 0, 255);

#ifdef TRACK_LAYER
    unsigned long layer = current_layer;
    MENU_ITEM_EDIT(long6, MSG_LAYER, &layer, layer, layer);
#endif //TRACK_LAYER

#ifdef BABYSTEPPING
    #ifdef BABYSTEP_XY
      //MENU_ITEM(submenu, MSG_BABYSTEP_X, lcd_babystep_x);
      //MENU_ITEM(submenu, MSG_BABYSTEP_Y, lcd_babystep_y);
    #endif //BABYSTEP_XY
    //MENU_ITEM(submenu, MSG_BABYSTEP_Z, lcd_babystep_z);
#endif
#ifdef FILAMENTCHANGEENABLE
     MENU_ITEM(gcode, MSG_FILAMENTCHANGE, PSTR("M600"));
#endif
    END_MENU();
}

void lcd_preheat_pla0()
{
    setTargetHotend0(plaPreheatHotendTemp);
    setTargetBed(plaPreheatHPBTemp);
    fanSpeed = plaPreheatFanSpeed;
    lcd_return_to_status();
    setWatch(); // heater sanity check timer
}

void lcd_preheat_abs0()
{
    setTargetHotend0(absPreheatHotendTemp);
    setTargetBed(absPreheatHPBTemp);
    fanSpeed = absPreheatFanSpeed;
    lcd_return_to_status();
    setWatch(); // heater sanity check timer
}

#if EXTRUDERS > 1 //2nd extruder preheat
void lcd_preheat_pla1()
{
    setTargetHotend1(plaPreheatHotendTemp);
    setTargetBed(plaPreheatHPBTemp);
    fanSpeed = plaPreheatFanSpeed;
    lcd_return_to_status();
    setWatch(); // heater sanity check timer
}

void lcd_preheat_abs1()
{
    setTargetHotend1(absPreheatHotendTemp);
    setTargetBed(absPreheatHPBTemp);
    fanSpeed = absPreheatFanSpeed;
    lcd_return_to_status();
    setWatch(); // heater sanity check timer
}
#endif //2nd extruder preheat

#if EXTRUDERS > 2 //3 extruder preheat
void lcd_preheat_pla2()
{
    setTargetHotend2(plaPreheatHotendTemp);
    setTargetBed(plaPreheatHPBTemp);
    fanSpeed = plaPreheatFanSpeed;
    lcd_return_to_status();
    setWatch(); // heater sanity check timer
}

void lcd_preheat_abs2()
{
    setTargetHotend2(absPreheatHotendTemp);
    setTargetBed(absPreheatHPBTemp);
    fanSpeed = absPreheatFanSpeed;
    lcd_return_to_status();
    setWatch(); // heater sanity check timer
}
#endif //3 extruder preheat

#if EXTRUDERS > 1 || EXTRUDERS > 2 //more than one extruder present
void lcd_preheat_pla012()
{
    setTargetHotend0(plaPreheatHotendTemp);
    setTargetHotend1(plaPreheatHotendTemp);
    setTargetHotend2(plaPreheatHotendTemp);
    setTargetBed(plaPreheatHPBTemp);
    fanSpeed = plaPreheatFanSpeed;
    lcd_return_to_status();
    setWatch(); // heater sanity check timer
}

void lcd_preheat_abs012()
{
    setTargetHotend0(absPreheatHotendTemp);
    setTargetHotend1(absPreheatHotendTemp);
    setTargetHotend2(absPreheatHotendTemp);
    setTargetBed(absPreheatHPBTemp);
    fanSpeed = absPreheatFanSpeed;
    lcd_return_to_status();
    setWatch(); // heater sanity check timer
}
#endif //more than one extruder present

void lcd_preheat_pla_bedonly()
{
    setTargetBed(plaPreheatHPBTemp);
    fanSpeed = plaPreheatFanSpeed;
    lcd_return_to_status();
    setWatch(); // heater sanity check timer
}

void lcd_preheat_abs_bedonly()
{
    setTargetBed(absPreheatHPBTemp);
    fanSpeed = absPreheatFanSpeed;
    lcd_return_to_status();
    setWatch(); // heater sanity check timer
}

static void lcd_preheat_pla_menu()
{
    START_MENU();
    MENU_ITEM(function, MSG_PREHEAT_PLA0, lcd_preheat_pla0);
#if EXTRUDERS > 1 //2 extruder preheat
    MENU_ITEM(function, MSG_PREHEAT_PLA1, lcd_preheat_pla1);
#endif //2 extruder preheat
#if EXTRUDERS > 2 //3 extruder preheat
    MENU_ITEM(function, MSG_PREHEAT_PLA2, lcd_preheat_pla2);
#endif //3 extruder preheat
#if EXTRUDERS > 1 || EXTRUDERS > 2 //all extruder preheat
    MENU_ITEM(function, MSG_PREHEAT_PLA012, lcd_preheat_pla012);
#endif //2 extruder preheat
#if TEMP_SENSOR_BED != 0
    MENU_ITEM(function, MSG_PREHEAT_PLA_BEDONLY, lcd_preheat_pla_bedonly);
#endif
    END_MENU();
}

static void lcd_preheat_abs_menu()
{
    START_MENU();
    MENU_ITEM(function, MSG_PREHEAT_ABS0, lcd_preheat_abs0);
#if EXTRUDERS > 1 //2 extruder preheat
    MENU_ITEM(function, MSG_PREHEAT_ABS1, lcd_preheat_abs1);
#endif //2 extruder preheat
#if EXTRUDERS > 2 //3 extruder preheat
    MENU_ITEM(function, MSG_PREHEAT_ABS2, lcd_preheat_abs2);
#endif //3 extruder preheat
#if EXTRUDERS > 1 || EXTRUDERS > 2 //all extruder preheat
    MENU_ITEM(function, MSG_PREHEAT_ABS012, lcd_preheat_abs012);
#endif //2 extruder preheat
#if TEMP_SENSOR_BED != 0
    MENU_ITEM(function, MSG_PREHEAT_ABS_BEDONLY, lcd_preheat_abs_bedonly);
#endif
    END_MENU();
}

void lcd_cooldown()
{
    setTargetHotend0(0);
    setTargetHotend1(0);
    setTargetHotend2(0);
    setTargetBed(0);
    fanSpeed = 0;
    lcd_return_to_status();
}

static void lcd_movement_menu()
{
    START_MENU();
    MENU_ITEM(back, MSG_MAIN, lcd_main_menu);
    MENU_ITEM(gcode, MSG_AUTO_HOME, PSTR("G28"));
    MENU_ITEM(gcode, MSG_DISABLE_STEPPERS, PSTR("M84"));
    MENU_ITEM(submenu, MSG_MOVE_AXIS, lcd_move_menu);
    END_MENU();
}
//~ static void lcd_change_temperature_menu()
//~ {
    //~ START_MENU();
    //~ MENU_ITEM(back, MSG_MAIN, lcd_main_menu);
    //~ MENU_ITEM(submenu, MSG_TEMPERATURE, lcd_control_temperature_menu);
//~ #ifdef SDSUPPORT
    //~ #ifdef MENU_ADDAUTOSTART
      //~ MENU_ITEM(function, MSG_AUTOSTART, lcd_autostart_sd);
    //~ #endif
//~ #endif
//~ //    MENU_ITEM(function, MSG_SET_HOME_OFFSETS, lcd_set_home_offsets);
//~ //    MENU_ITEM(gcode, MSG_SET_ORIGIN, PSTR("G92 X0 Y0 Z0"));
//~ #if TEMP_SENSOR_0 != 0
  //~ #if EXTRUDERS > 1 || EXTRUDERS > 2 || TEMP_SENSOR_BED != 0
//~ //    MENU_ITEM(submenu, MSG_PREHEAT_PLA, lcd_preheat_pla_menu);
//~ //    MENU_ITEM(submenu, MSG_PREHEAT_ABS, lcd_preheat_abs_menu);
  //~ #else
    //~ MENU_ITEM(function, MSG_PREHEAT_PLA, lcd_preheat_pla0);
    //~ MENU_ITEM(function, MSG_PREHEAT_ABS, lcd_preheat_abs0);
  //~ #endif
//~ #endif
    //~ MENU_ITEM(function, MSG_COOLDOWN, lcd_cooldown);
//#if PS_ON_PIN > -1
//    if (powersupply)
//    {
//        MENU_ITEM(gcode, MSG_SWITCH_PS_OFF, PSTR("M81"));
//    }else{
//        MENU_ITEM(gcode, MSG_SWITCH_PS_ON, PSTR("M80"));
//    }
//#endif
//    END_MENU();
//}

#ifdef DELTA_CALIBRATION_MENU
static void lcd_delta_calibrate_menu()
{
    START_MENU();
    MENU_ITEM(back, MSG_MAIN, lcd_main_menu);
    MENU_ITEM(gcode, MSG_AUTO_HOME, PSTR("G28"));
    MENU_ITEM(gcode, MSG_DELTA_CALIBRATE_X, PSTR("G0 F8000 X-77.94 Y-45 Z0"));
    MENU_ITEM(gcode, MSG_DELTA_CALIBRATE_Y, PSTR("G0 F8000 X77.94 Y-45 Z0"));
    MENU_ITEM(gcode, MSG_DELTA_CALIBRATE_Z, PSTR("G0 F8000 X0 Y90 Z0"));
    MENU_ITEM(gcode, MSG_DELTA_CALIBRATE_CENTER, PSTR("G0 F8000 X0 Y0 Z0"));
    END_MENU();
}
#endif // DELTA_CALIBRATION_MENU

float move_menu_scale;
static void lcd_move_menu_axis();

signed char home_dir[] = {X_HOME_DIR, Y_HOME_DIR, Z_HOME_DIR};

static void _lcd_move(const char *name, int axis, int min, int max) {
  if (encoderPosition != 0) {
    refresh_cmd_timeout();
    if (    (home_dir[axis] == -1 && (!digitalRead(axis_max_pin[axis])^axis_max_endstop_inverting[axis] || (int)encoderPosition < 0))
         || (home_dir[axis] == 1 && (!digitalRead(axis_min_pin[axis])^axis_min_endstop_inverting[axis] || (int)encoderPosition > 0)) )
      current_position[axis] += float((int)encoderPosition) * move_menu_scale;
    if (min_software_endstops && current_position[axis] < min) current_position[axis] = min;
    if (max_software_endstops && current_position[axis] > max) current_position[axis] = max;
    encoderPosition = 0;
    #ifdef DELTA
      calculate_delta(current_position);
      plan_buffer_line(delta[X_AXIS], delta[Y_AXIS], delta[Z_AXIS], current_position[E_AXIS], manual_feedrate[axis]/60, active_extruder);
    #else
      plan_buffer_line(current_position[X_AXIS], current_position[Y_AXIS], current_position[Z_AXIS], current_position[E_AXIS], manual_feedrate[axis]/60, active_extruder);
    #endif
    lcdDrawUpdate = 1;
  }
  if (lcdDrawUpdate) lcd_implementation_drawedit(name, ftostr31(current_position[axis]));
  if (LCD_CLICKED) lcd_goto_menu(lcd_move_menu_axis);
}
static void lcd_move_x() { _lcd_move(PSTR("X"), X_AXIS, X_MIN_POS, X_MAX_POS); }
static void lcd_move_y() { _lcd_move(PSTR("Y"), Y_AXIS, Y_MIN_POS, Y_MAX_POS); }
static void lcd_move_z() { _lcd_move(PSTR("Z"), Z_AXIS, Z_MIN_POS, Z_MAX_POS); }
static void lcd_move_e(
  #if EXTRUDERS > 1
    uint8_t e
  #endif
) {
  #if EXTRUDERS > 1
    unsigned short original_active_extruder = active_extruder;
    active_extruder = e;
  #endif
  if (encoderPosition != 0) {
    current_position[E_AXIS] += float((int)encoderPosition) * move_menu_scale;
    encoderPosition = 0;
  #ifdef DELTA
    calculate_delta(current_position);
    plan_buffer_line(delta[X_AXIS], delta[Y_AXIS], delta[Z_AXIS], current_position[E_AXIS], manual_feedrate[E_AXIS]/60, active_extruder);
  #else
    plan_buffer_line(current_position[X_AXIS], current_position[Y_AXIS], current_position[Z_AXIS], current_position[E_AXIS], manual_feedrate[E_AXIS]/60, active_extruder);
  #endif
    lcdDrawUpdate = 1;
  }
  if (lcdDrawUpdate) {
    PGM_P pos_label;
    #if EXTRUDERS == 1
      pos_label = PSTR(MSG_MOVE_E);
    #else
      switch (e) {
        case 0: pos_label = PSTR(MSG_MOVE_E MSG_MOVE_E1); break;
        case 1: pos_label = PSTR(MSG_MOVE_E MSG_MOVE_E2); break;
        #if EXTRUDERS > 2
          case 2: pos_label = PSTR(MSG_MOVE_E MSG_MOVE_E3); break;
          #if EXTRUDERS > 3
            case 3: pos_label = PSTR(MSG_MOVE_E MSG_MOVE_E4); break;
          #endif //EXTRUDERS > 3
        #endif //EXTRUDERS > 2
      }
    #endif //EXTRUDERS > 1
    lcd_implementation_drawedit(pos_label, ftostr31(current_position[E_AXIS]));
  }
  if (LCD_CLICKED) lcd_goto_menu(lcd_move_menu_axis);
  #if EXTRUDERS > 1
    active_extruder = original_active_extruder;
  #endif
}

#if EXTRUDERS > 1
  static void lcd_move_e0() { lcd_move_e(0); }
  static void lcd_move_e1() { lcd_move_e(1); }
  #if EXTRUDERS > 2
    static void lcd_move_e2() { lcd_move_e(2); }
    #if EXTRUDERS > 3
      static void lcd_move_e3() { lcd_move_e(3); }
    #endif
  #endif
#endif // EXTRUDERS > 1

static void lcd_move_menu_axis()
{
    START_MENU();
    MENU_ITEM(back, MSG_MOVE_AXIS, lcd_move_menu);
    MENU_ITEM(submenu, MSG_MOVE_X, lcd_move_x);
    MENU_ITEM(submenu, MSG_MOVE_Y, lcd_move_y);
  //if (move_menu_scale < 10.0) {
    MENU_ITEM(submenu, MSG_MOVE_Z, lcd_move_z);
    #if EXTRUDERS == 1
      MENU_ITEM(submenu, MSG_MOVE_E, lcd_move_e);
    #else
      MENU_ITEM(submenu, MSG_MOVE_E MSG_MOVE_E1, lcd_move_e0);
      MENU_ITEM(submenu, MSG_MOVE_E MSG_MOVE_E2, lcd_move_e1);
      #if EXTRUDERS > 2
        MENU_ITEM(submenu, MSG_MOVE_E MSG_MOVE_E3, lcd_move_e2);
        #if EXTRUDERS > 3
          MENU_ITEM(submenu, MSG_MOVE_E MSG_MOVE_E4, lcd_move_e3);
        #endif
      #endif
    #endif // EXTRUDERS > 1
  //}
    END_MENU();
}

static void lcd_move_menu_10mm()
{
    move_menu_scale = 10.0;
    lcd_move_menu_axis();
}
static void lcd_move_menu_1mm()
{
    move_menu_scale = 1.0;
    lcd_move_menu_axis();
}
static void lcd_move_menu_01mm()
{
    move_menu_scale = 0.1;
    lcd_move_menu_axis();
}

static void lcd_move_menu()
{
    START_MENU();
    MENU_ITEM(back, MSG_MOVEMENT, lcd_movement_menu);
    MENU_ITEM(submenu, MSG_MOVE_10MM, lcd_move_menu_10mm);
    MENU_ITEM(submenu, MSG_MOVE_1MM, lcd_move_menu_1mm);
    MENU_ITEM(submenu, MSG_MOVE_01MM, lcd_move_menu_01mm);
    //TODO:X,Y,Z,E
    END_MENU();
}

static void lcd_configuration_menu()
{
    START_MENU();
    MENU_ITEM(back, MSG_MAIN, lcd_main_menu);
    MENU_ITEM(submenu, MSG_ADVANCED, lcd_control_advanced_menu);
//	MENU_ITEM(submenu, MSG_VOLUMETRIC, lcd_control_volumetric_menu);

//#ifdef DOGLCD
//    MENU_ITEM_EDIT(int3, MSG_CONTRAST, &lcd_contrast, 0, 63);
//    MENU_ITEM(submenu, MSG_CONTRAST, lcd_set_contrast);
//#endif
#ifdef FWRETRACT
    MENU_ITEM(submenu, MSG_RETRACT, lcd_control_retract_menu);
#endif
#ifdef EEPROM_SETTINGS
    MENU_ITEM(function, MSG_STORE_EPROM, Config_StoreSettings);
    MENU_ITEM(function, MSG_LOAD_EPROM, Config_RetrieveSettings);
#endif
    MENU_ITEM(function, MSG_RESTORE_FAILSAFE, Config_ResetDefault);
    END_MENU();
}

static void lcd_control_temperature_menu()
{
#ifdef PIDTEMP
    // set up temp variables - undo the default scaling
    raw_Ki = unscalePID_i(Ki);
    raw_Kd = unscalePID_d(Kd);
#endif

    START_MENU();
    MENU_ITEM(back, MSG_MAIN, lcd_main_menu);
#if TEMP_SENSOR_0 != 0
    MENU_ITEM_EDIT(int3, MSG_NOZZLE
    #if EXTRUDERS > 1
      "1"
    #endif
    , &target_temperature[0], 0, HEATER_0_MAXTEMP - 15);
#endif
#if EXTRUDERS > 1
    MENU_ITEM_EDIT(int3, MSG_NOZZLE1, &target_temperature[1], 0, HEATER_1_MAXTEMP - 15);
#endif
#if EXTRUDERS > 2
    MENU_ITEM_EDIT(int3, MSG_NOZZLE2, &target_temperature[2], 0, HEATER_2_MAXTEMP - 15);
#endif
#if EXTRUDERS > 3
    MENU_ITEM_EDIT(int3, MSG_NOZZLE3, &target_temperature[2], 0, HEATER_2_MAXTEMP - 15);
#endif
#if TEMP_SENSOR_BED != 0
    MENU_ITEM_EDIT(int3, MSG_BED, &target_temperature_bed, 0, BED_MAXTEMP - 15);
#endif
    MENU_ITEM_EDIT(int3, MSG_FAN_SPEED, &fanSpeed, 0, 255);
#if defined AUTOTEMP && (TEMP_SENSOR_0 != 0)
    MENU_ITEM_EDIT(bool, MSG_AUTOTEMP, &autotemp_enabled);
    MENU_ITEM_EDIT(float3, MSG_MIN, &autotemp_min, 0, HEATER_0_MAXTEMP - 15);
    MENU_ITEM_EDIT(float3, MSG_MAX, &autotemp_max, 0, HEATER_0_MAXTEMP - 15);
    MENU_ITEM_EDIT(float32, MSG_FACTOR, &autotemp_factor, 0.0, 1.0);
#endif
    MENU_ITEM(function, MSG_COOLDOWN, lcd_cooldown);
//#ifdef PIDTEMP
//    MENU_ITEM_EDIT(float52, MSG_PID_P, &Kp, 1, 9990);
    // i is typically a small value so allows values below 1
//    MENU_ITEM_EDIT_CALLBACK(float52, MSG_PID_I, &raw_Ki, 0.01, 9990, copy_and_scalePID_i);
//    MENU_ITEM_EDIT_CALLBACK(float52, MSG_PID_D, &raw_Kd, 1, 9990, copy_and_scalePID_d);
//# ifdef PID_ADD_EXTRUSION_RATE
//    MENU_ITEM_EDIT(float3, MSG_PID_C, &Kc, 1, 9990);
//# endif//PID_ADD_EXTRUSION_RATE
//#endif//PIDTEMP
//    MENU_ITEM(submenu, MSG_PREHEAT_PLA_SETTINGS, lcd_control_temperature_preheat_pla_settings_menu);
//    MENU_ITEM(submenu, MSG_PREHEAT_ABS_SETTINGS, lcd_control_temperature_preheat_abs_settings_menu);
    END_MENU();
}

static void lcd_control_temperature_preheat_pla_settings_menu()
{
    START_MENU();
    MENU_ITEM(back, MSG_TEMPERATURE, lcd_control_temperature_menu);
    MENU_ITEM_EDIT(int3, MSG_FAN_SPEED, &plaPreheatFanSpeed, 0, 255);
#if TEMP_SENSOR_0 != 0
    MENU_ITEM_EDIT(int3, MSG_NOZZLE, &plaPreheatHotendTemp, 0, HEATER_0_MAXTEMP - 15);
#endif
#if TEMP_SENSOR_BED != 0
    MENU_ITEM_EDIT(int3, MSG_BED, &plaPreheatHPBTemp, 0, BED_MAXTEMP - 15);
#endif
#ifdef EEPROM_SETTINGS
    MENU_ITEM(function, MSG_STORE_EPROM, Config_StoreSettings);
#endif
    END_MENU();
}

static void lcd_control_temperature_preheat_abs_settings_menu()
{
    START_MENU();
    MENU_ITEM(back, MSG_TEMPERATURE, lcd_control_temperature_menu);
    MENU_ITEM_EDIT(int3, MSG_FAN_SPEED, &absPreheatFanSpeed, 0, 255);
#if TEMP_SENSOR_0 != 0
    MENU_ITEM_EDIT(int3, MSG_NOZZLE, &absPreheatHotendTemp, 0, HEATER_0_MAXTEMP - 15);
#endif
#if TEMP_SENSOR_BED != 0
    MENU_ITEM_EDIT(int3, MSG_BED, &absPreheatHPBTemp, 0, BED_MAXTEMP - 15);
#endif
#ifdef EEPROM_SETTINGS
    MENU_ITEM(function, MSG_STORE_EPROM, Config_StoreSettings);
#endif
    END_MENU();
}

static void lcd_control_advanced_menu()
{
    START_MENU();
    MENU_ITEM(back, MSG_CONFIGURATION, lcd_configuration_menu);
#ifdef ENABLE_AUTO_BED_LEVELING
    //MENU_ITEM_EDIT(float43, MSG_ZPROBE_ZOFFSET, &zprobe_zoffset, 0.5, 2.5);
    MENU_ITEM(submenu, MSG_ZPROBE_ZOFFSET, lcd_babystep_zoffset);
#endif
#ifdef RESUME_FEATURE
    if(axis_known_position[Z_AXIS] && current_position[Z_AXIS] != Z_RAISE_AFTER_HOMING && current_position[Z_AXIS] != 0)
      MENU_ITEM_EDIT(bool, "Resume from Z", &resume_z);
#endif
    MENU_ITEM_EDIT(float5, MSG_ACC, &acceleration, 500, 99000);
    MENU_ITEM_EDIT(float3, MSG_VXY_JERK, &max_xy_jerk, 1, 990);
    MENU_ITEM_EDIT(float52, MSG_VZ_JERK, &max_z_jerk, 0.1, 990);
    MENU_ITEM_EDIT(float3, MSG_VE_JERK, &max_e_jerk, 1, 990);
    MENU_ITEM_EDIT(float3, MSG_VMAX MSG_X, &max_feedrate[X_AXIS], 1, 999);
    MENU_ITEM_EDIT(float3, MSG_VMAX MSG_Y, &max_feedrate[Y_AXIS], 1, 999);
    MENU_ITEM_EDIT(float3, MSG_VMAX MSG_Z, &max_feedrate[Z_AXIS], 1, 999);
    MENU_ITEM_EDIT(float3, MSG_VMAX MSG_E, &max_feedrate[E_AXIS], 1, 999);
    MENU_ITEM_EDIT(float3, MSG_VMIN, &minimumfeedrate, 0, 999);
    MENU_ITEM_EDIT(float3, MSG_VTRAV_MIN, &mintravelfeedrate, 0, 999);
    MENU_ITEM_EDIT_CALLBACK(long5, MSG_AMAX MSG_X, &max_acceleration_units_per_sq_second[X_AXIS], 100, 99000, reset_acceleration_rates);
    MENU_ITEM_EDIT_CALLBACK(long5, MSG_AMAX MSG_Y, &max_acceleration_units_per_sq_second[Y_AXIS], 100, 99000, reset_acceleration_rates);
    MENU_ITEM_EDIT_CALLBACK(long5, MSG_AMAX MSG_Z, &max_acceleration_units_per_sq_second[Z_AXIS], 100, 99000, reset_acceleration_rates);
    MENU_ITEM_EDIT_CALLBACK(long5, MSG_AMAX MSG_E, &max_acceleration_units_per_sq_second[E_AXIS], 100, 99000, reset_acceleration_rates);
    MENU_ITEM_EDIT(float5, MSG_A_RETRACT, &retract_acceleration, 100, 99000);
    MENU_ITEM_EDIT(float52, MSG_XSTEPS, &axis_steps_per_unit[X_AXIS], 5, 9999);
    MENU_ITEM_EDIT(float52, MSG_YSTEPS, &axis_steps_per_unit[Y_AXIS], 5, 9999);
    MENU_ITEM_EDIT(float51, MSG_ZSTEPS, &axis_steps_per_unit[Z_AXIS], 5, 9999);
    #if EXTRUDERS > 1
      MENU_ITEM_EDIT(float51, MSG_E0STEPS, &axis_steps_per_unit[E_AXIS], 5, 9999);
      MENU_ITEM_EDIT(float51, MSG_E1STEPS, &axis_steps_per_unit[E_AXIS+1], 5, 9999);
      #if EXTRUDERS > 2
        MENU_ITEM_EDIT(float51, MSG_E2STEPS, &axis_steps_per_unit[E_AXIS+2], 5, 9999);
        #if EXTRUDERS > 3
          MENU_ITEM_EDIT(float51, MSG_E3STEPS, &axis_steps_per_unit[E_AXIS+3], 5, 9999);
        #endif
      #endif
    #else
      MENU_ITEM_EDIT(float51, MSG_ESTEPS, &axis_steps_per_unit[E_AXIS], 5, 9999);
    #endif
#ifdef ABORT_ON_ENDSTOP_HIT_FEATURE_ENABLED
    MENU_ITEM_EDIT(bool, MSG_ENDSTOP_ABORT, &abort_on_endstop_hit);
#endif
#ifdef SCARA
    MENU_ITEM_EDIT(float74, MSG_XSCALE, &axis_scaling[X_AXIS],0.5,2);
    MENU_ITEM_EDIT(float74, MSG_YSCALE, &axis_scaling[Y_AXIS],0.5,2);
#endif
    END_MENU();
}

static void lcd_control_volumetric_menu()
{
	START_MENU();
	MENU_ITEM(back, MSG_CONFIGURATION, lcd_configuration_menu);

	MENU_ITEM_EDIT_CALLBACK(bool, MSG_VOLUMETRIC_ENABLED, &volumetric_enabled, calculate_volumetric_multipliers);

	if (volumetric_enabled) {
		MENU_ITEM_EDIT_CALLBACK(float43, MSG_FILAMENT_SIZE_EXTRUDER_0, &filament_size[0], DEFAULT_NOMINAL_FILAMENT_DIA - .5, DEFAULT_NOMINAL_FILAMENT_DIA + .5, calculate_volumetric_multipliers);
#if EXTRUDERS > 1
		MENU_ITEM_EDIT_CALLBACK(float43, MSG_FILAMENT_SIZE_EXTRUDER_1, &filament_size[1], DEFAULT_NOMINAL_FILAMENT_DIA - .5, DEFAULT_NOMINAL_FILAMENT_DIA + .5, calculate_volumetric_multipliers);
#if EXTRUDERS > 2
		MENU_ITEM_EDIT_CALLBACK(float43, MSG_FILAMENT_SIZE_EXTRUDER_2, &filament_size[2], DEFAULT_NOMINAL_FILAMENT_DIA - .5, DEFAULT_NOMINAL_FILAMENT_DIA + .5, calculate_volumetric_multipliers);
#endif
#endif
	}

	END_MENU();
}

#ifdef DOGLCD
static void lcd_set_contrast()
{
    if (encoderPosition != 0)
    {
        lcd_contrast -= encoderPosition;
        if (lcd_contrast < 0) lcd_contrast = 0;
        else if (lcd_contrast > 63) lcd_contrast = 63;
        encoderPosition = 0;
        lcdDrawUpdate = 1;
        u8g.setContrast(lcd_contrast);
    }
    if (lcdDrawUpdate)
    {
        lcd_implementation_drawedit(PSTR(MSG_CONTRAST), itostr2(lcd_contrast));
    }
    if (LCD_CLICKED) lcd_goto_menu(lcd_configuration_menu);
}
#endif

#ifdef FWRETRACT
static void lcd_control_retract_menu()
{
    START_MENU();
    MENU_ITEM(back, MSG_CONFIGURATION, lcd_configuration_menu);
    MENU_ITEM_EDIT(bool, MSG_AUTORETRACT, &autoretract_enabled);
    MENU_ITEM_EDIT(float52, MSG_CONTROL_RETRACT, &retract_length, 0, 100);
	#if EXTRUDERS > 1
      MENU_ITEM_EDIT(float52, MSG_CONTROL_RETRACT_SWAP, &retract_length_swap, 0, 100);
    #endif
    MENU_ITEM_EDIT(float3, MSG_CONTROL_RETRACTF, &retract_feedrate, 1, 999);
    MENU_ITEM_EDIT(float52, MSG_CONTROL_RETRACT_ZLIFT, &retract_zlift, 0, 999);
    MENU_ITEM_EDIT(float52, MSG_CONTROL_RETRACT_RECOVER, &retract_recover_length, 0, 100);
	#if EXTRUDERS > 1
      MENU_ITEM_EDIT(float52, MSG_CONTROL_RETRACT_RECOVER_SWAP, &retract_recover_length_swap, 0, 100);
    #endif
    MENU_ITEM_EDIT(float3, MSG_CONTROL_RETRACT_RECOVERF, &retract_recover_feedrate, 1, 999);
    END_MENU();
}

#endif //FWRETRACT

#if SDCARDDETECT == -1
static void lcd_sd_refresh()
{
    card.initsd();
    currentMenuViewOffset = 0;
}
#endif
static void lcd_sd_updir()
{
    #ifdef RESUME_FEATURE
      examined_once = false;
    #endif
    card.updir();
    currentMenuViewOffset = 0;
}

#ifdef RESUME_FEATURE
  void resume_menu()
  {
    if((int)encoderPosition != 0) {
      if((int)encoderPosition > 0)
        resume_selected = true;
      else
        resume_selected = false;
      encoderPosition = 0;
      lcdDrawUpdate = 1;
    }
    if (lcdDrawUpdate)
    {
      u8g.drawStr(5,30,"Continue Last Print");
      if(resume_z)
        u8g.drawStr(35,39,"(from Z):");
      else
        u8g.drawStr(119,30,":");
      if(resume_selected)
      {
        u8g.setColorIndex(0);
        u8g.drawBox(30,41,15,11);
        u8g.setColorIndex(1);
        u8g.drawStr(32,50,"No");
        u8g.setColorIndex(1);
        u8g.drawBox(70,41,21,11);
        u8g.setColorIndex(0);
        u8g.drawStr(72,50,"Yes");
      }
      else
      {
        u8g.setColorIndex(1);
        u8g.drawBox(30,41,15,11);
        u8g.setColorIndex(0);
        u8g.drawStr(32,50,"No");
        u8g.setColorIndex(0);
        u8g.drawBox(70,41,21,11);
        u8g.setColorIndex(1);
        u8g.drawStr(72,50,"Yes");
      }
    }
    if (LCD_CLICKED)
    {
      selected_resume_once = true;
      if(resume_selected && !resume_z)
        menu_action_sdfile(resumefilename, '\0');
      else if(resume_selected && resume_z)
      {
        planner_disabled_below_z = current_position[Z_AXIS];
        menu_action_sdfile(resumefilename, '\0');
      }
      else
      {
        resume_z = false;
        call_lcd_sdcard_menu = true;
      }
    }
  }
#endif //RESUME_FEATURE

void lcd_sdcard_menu()
{
    if (lcdDrawUpdate == 0 && LCD_CLICKED == 0)
        return;	// nothing to do (so don't thrash the SD card)
    uint16_t fileCnt = card.getnrfilenames();
    #ifdef RESUME_FEATURE
      bool found_resume_gcode = false;
      card.getWorkDirName();
      for(uint16_t i=0;(i<fileCnt && !examined_once);i++)
      {
        SERIAL_ECHOLN("checking");
        card.getfilename(fileCnt-1-i);
        if(strstr(card.filename, resumefilename) != NULL) //found a resume gcode file!
        {
          found_resume_gcode = true;
          if(!selected_resume_once)
          {
            resume_menu();
            return;
          }
        }
      }
      examined_once = true;
    #endif
    START_MENU();
    MENU_ITEM(back, MSG_MAIN, lcd_main_menu);
    card.getWorkDirName();
    if(card.filename[0]=='/')
    {
#if SDCARDDETECT == -1
        MENU_ITEM(function, LCD_STR_REFRESH MSG_REFRESH, lcd_sd_refresh);
#endif
    }else{
        MENU_ITEM(function, LCD_STR_FOLDER "..", lcd_sd_updir);
    }

    #ifdef RESUME_FEATURE
      static uint16_t forbidden_encpos = 0;
    #endif
    for(uint16_t i=0;i<fileCnt;i++)
    {
        #ifdef RESUME_FEATURE
          if(forbidden_encpos && found_resume_gcode && (encoderPosition / ENCODER_STEPS_PER_MENU_ITEM) == forbidden_encpos)
            encoderPosition++;
        #endif
        if (_menuItemNr == _lineNr)
        {
            #ifndef SDCARD_RATHERRECENTFIRST
              card.getfilename(i);
            #else
              card.getfilename(fileCnt-1-i);
            #endif
            if (card.filenameIsDir)
            {
                MENU_ITEM(sddirectory, MSG_CARD_MENU, card.filename, card.longFilename);
            }else{
                #ifndef RESUME_FEATURE
                  MENU_ITEM(sdfile, MSG_CARD_MENU, card.filename, card.longFilename);
                #else
                  if(strstr(card.filename, resumefilename) == NULL)
                    MENU_ITEM(sdfile, MSG_CARD_MENU, card.filename, card.longFilename);
                  else
                  {
                    forbidden_encpos = _menuItemNr;
                  /*SERIAL_ECHOLN("encpos: ");
                    SERIAL_ECHOLN(encoderPosition / ENCODER_STEPS_PER_MENU_ITEM);
                    SERIAL_ECHOLN("menuItemNr: ");
                    SERIAL_ECHOLN((int)(_menuItemNr));*/
                    MENU_ITEM_DUMMY();
                    _lineNr++;
                    lcdDrawUpdate=1;
                  }
                #endif
            }
        }else{
            MENU_ITEM_DUMMY();
        }
    }
    END_MENU();
}

#define menu_edit_type(_type, _name, _strFunc, scale) \
    void menu_edit_ ## _name () \
    { \
        if ((int32_t)encoderPosition < 0) encoderPosition = 0; \
        if ((int32_t)encoderPosition > maxEditValue) encoderPosition = maxEditValue; \
        if (lcdDrawUpdate) \
            lcd_implementation_drawedit(editLabel, _strFunc(((_type)((int32_t)encoderPosition + minEditValue)) / scale)); \
        if (LCD_CLICKED) \
        { \
            *((_type*)editValue) = ((_type)((int32_t)encoderPosition + minEditValue)) / scale; \
            lcd_goto_menu(prevMenu, prevEncoderPosition); \
        } \
    } \
    void menu_edit_callback_ ## _name () { \
        menu_edit_ ## _name (); \
        if (LCD_CLICKED) (*callbackFunc)(); \
    } \
    static void menu_action_setting_edit_ ## _name (const char* pstr, _type* ptr, _type minValue, _type maxValue) \
    { \
        prevMenu = currentMenu; \
        prevEncoderPosition = encoderPosition; \
         \
        lcdDrawUpdate = 2; \
        currentMenu = menu_edit_ ## _name; \
         \
        editLabel = pstr; \
        editValue = ptr; \
        minEditValue = minValue * scale; \
        maxEditValue = maxValue * scale - minEditValue; \
        encoderPosition = (*ptr) * scale - minEditValue; \
    }\
    static void menu_action_setting_edit_callback_ ## _name (const char* pstr, _type* ptr, _type minValue, _type maxValue, menuFunc_t callback) \
    { \
        prevMenu = currentMenu; \
        prevEncoderPosition = encoderPosition; \
         \
        lcdDrawUpdate = 2; \
        currentMenu = menu_edit_callback_ ## _name; \
         \
        editLabel = pstr; \
        editValue = ptr; \
        minEditValue = minValue * scale; \
        maxEditValue = maxValue * scale - minEditValue; \
        encoderPosition = (*ptr) * scale - minEditValue; \
        callbackFunc = callback;\
    }
menu_edit_type(int, int3, itostr3, 1)
menu_edit_type(float, float3, ftostr3, 1)
menu_edit_type(float, float32, ftostr32, 100)
menu_edit_type(float, float43, ftostr43, 1000)
menu_edit_type(float, float5, ftostr5, 0.01)
menu_edit_type(float, float51, ftostr51, 10)
menu_edit_type(float, float52, ftostr52, 100)
menu_edit_type(unsigned long, long5, ftostr5, 0.01)
menu_edit_type(unsigned long, long6, ftostr5, 1)

#ifdef REPRAPWORLD_KEYPAD
	static void reprapworld_keypad_move_z_up() {
    encoderPosition = 1;
    move_menu_scale = REPRAPWORLD_KEYPAD_MOVE_STEP;
		lcd_move_z();
  }
	static void reprapworld_keypad_move_z_down() {
    encoderPosition = -1;
    move_menu_scale = REPRAPWORLD_KEYPAD_MOVE_STEP;
		lcd_move_z();
  }
	static void reprapworld_keypad_move_x_left() {
    encoderPosition = -1;
    move_menu_scale = REPRAPWORLD_KEYPAD_MOVE_STEP;
		lcd_move_x();
  }
	static void reprapworld_keypad_move_x_right() {
    encoderPosition = 1;
    move_menu_scale = REPRAPWORLD_KEYPAD_MOVE_STEP;
		lcd_move_x();
	}
	static void reprapworld_keypad_move_y_down() {
    encoderPosition = 1;
    move_menu_scale = REPRAPWORLD_KEYPAD_MOVE_STEP;
		lcd_move_y();
	}
	static void reprapworld_keypad_move_y_up() {
		encoderPosition = -1;
		move_menu_scale = REPRAPWORLD_KEYPAD_MOVE_STEP;
    lcd_move_y();
	}
	static void reprapworld_keypad_move_home() {
		enquecommand_P((PSTR("G28"))); // move all axis home
	}
#endif

/** End of menus **/

static void lcd_quick_feedback()
{
    lcdDrawUpdate = 2;
    blocking_enc = millis() + 500;
    lcd_implementation_quick_feedback();
}

/** Menu action functions **/
static void menu_action_back(menuFunc_t data) { lcd_goto_menu(data); }
static void menu_action_submenu(menuFunc_t data) { lcd_goto_menu(data); }
static void menu_action_gcode(const char* pgcode) { enquecommand_P(pgcode); }
static void menu_action_function(menuFunc_t data) { (*data)(); }
static void menu_action_sdfile(const char* filename, char* longFilename)
{
    char cmd[30];
    char* c;
    sprintf_P(cmd, PSTR("M23 %s"), filename);
    for(c = &cmd[4]; *c; c++)
        *c = tolower(*c);
    #ifdef RESUME_FEATURE
      for(int i=0; i<26; i++) contfilename[i] = filename[i];

      if(strstr(card.filename, resumefilename) != NULL)
      {
        resume_selected = false;
        cmd[1] = '3';
        cmd[2] = '2';
        enquecommand(cmd);
      }
      else
      {
        if(resume_z)
        {
          home_x_and_y = true;
          enquecommand("G27");
          enquecommand("G28");
        }
        if(!resume_selected)
          card.removeFile(resumefilename, true); // delete the old resume file
    #endif
        enquecommand(cmd);
        enquecommand_P(PSTR("M24"));
    #ifdef RESUME_FEATURE
      }
    #endif
    lcd_return_to_status();
}
static void menu_action_sddirectory(const char* filename, char* longFilename)
{
    card.chdir(filename);
    encoderPosition = 0;
}
static void menu_action_setting_edit_bool(const char* pstr, bool* ptr)
{
    *ptr = !(*ptr);
}
static void menu_action_setting_edit_callback_bool(const char* pstr, bool* ptr, menuFunc_t callback)
{
	menu_action_setting_edit_bool(pstr, ptr);
	(*callback)();
}
#endif//ULTIPANEL

/** LCD API **/
void lcd_init()
{
    lcd_implementation_init();

#ifdef NEWPANEL
    SET_INPUT(BTN_EN1);
    SET_INPUT(BTN_EN2);
    WRITE(BTN_EN1,HIGH);
    WRITE(BTN_EN2,HIGH);
  #if BTN_ENC > 0
    SET_INPUT(BTN_ENC);
    WRITE(BTN_ENC,HIGH);
  #endif
  #ifdef REPRAPWORLD_KEYPAD
    pinMode(SHIFT_CLK,OUTPUT);
    pinMode(SHIFT_LD,OUTPUT);
    pinMode(SHIFT_OUT,INPUT);
    WRITE(SHIFT_OUT,HIGH);
    WRITE(SHIFT_LD,HIGH);
  #endif
#else  // Not NEWPANEL
  #ifdef SR_LCD_2W_NL // Non latching 2 wire shift register
     pinMode (SR_DATA_PIN, OUTPUT);
     pinMode (SR_CLK_PIN, OUTPUT);
  #elif defined(SHIFT_CLK)
     pinMode(SHIFT_CLK,OUTPUT);
     pinMode(SHIFT_LD,OUTPUT);
     pinMode(SHIFT_EN,OUTPUT);
     pinMode(SHIFT_OUT,INPUT);
     WRITE(SHIFT_OUT,HIGH);
     WRITE(SHIFT_LD,HIGH);
     WRITE(SHIFT_EN,LOW);
  #else
     #ifdef ULTIPANEL
     #error ULTIPANEL requires an encoder
     #endif
  #endif // SR_LCD_2W_NL
#endif//!NEWPANEL

#if defined (SDSUPPORT) && defined(SDCARDDETECT) && (SDCARDDETECT > 0)
    pinMode(SDCARDDETECT,INPUT);
    WRITE(SDCARDDETECT, HIGH);
    lcd_oldcardstatus = IS_SD_INSERTED;
#endif//(SDCARDDETECT > 0)
#ifdef LCD_HAS_SLOW_BUTTONS
    slow_buttons = 0;
#endif
    lcd_buttons_update();
#ifdef ULTIPANEL
    encoderDiff = 0;
#endif
}

void lcd_update()
{
    static unsigned long timeoutToStatus = 0;

    #ifdef LCD_HAS_SLOW_BUTTONS
    slow_buttons = lcd_implementation_read_slow_buttons(); // buttons which take too long to read in interrupt context
    #endif

    lcd_buttons_update();

    #if (SDCARDDETECT > 0)
    if((IS_SD_INSERTED != lcd_oldcardstatus && lcd_detected()))
    {
        lcdDrawUpdate = 2;
        lcd_oldcardstatus = IS_SD_INSERTED;
        lcd_implementation_init( // to maybe revive the LCD if static electricity killed it.
          #if defined(LCD_PROGRESS_BAR) && defined(SDSUPPORT)
            currentMenu == lcd_status_screen
          #endif
        );

        if(lcd_oldcardstatus)
        {
            card.initsd();
            LCD_MESSAGEPGM(MSG_SD_INSERTED);
        }
        else
        {
            card.release();
            LCD_MESSAGEPGM(WELCOME_MSG);
        }
    }
    #endif//CARDINSERTED

    if (lcd_next_update_millis < millis())
    {
#ifdef ULTIPANEL
		#ifdef REPRAPWORLD_KEYPAD
        	if (REPRAPWORLD_KEYPAD_MOVE_Z_UP) {
        		reprapworld_keypad_move_z_up();
        	}
        	if (REPRAPWORLD_KEYPAD_MOVE_Z_DOWN) {
        		reprapworld_keypad_move_z_down();
        	}
        	if (REPRAPWORLD_KEYPAD_MOVE_X_LEFT) {
        		reprapworld_keypad_move_x_left();
        	}
        	if (REPRAPWORLD_KEYPAD_MOVE_X_RIGHT) {
        		reprapworld_keypad_move_x_right();
        	}
        	if (REPRAPWORLD_KEYPAD_MOVE_Y_DOWN) {
        		reprapworld_keypad_move_y_down();
        	}
        	if (REPRAPWORLD_KEYPAD_MOVE_Y_UP) {
        		reprapworld_keypad_move_y_up();
        	}
        	if (REPRAPWORLD_KEYPAD_MOVE_HOME) {
        		reprapworld_keypad_move_home();
        	}
		#endif
        if (abs(encoderDiff) >= ENCODER_PULSES_PER_STEP)
        {
            lcdDrawUpdate = 1;
            encoderPosition += encoderDiff / ENCODER_PULSES_PER_STEP;
            encoderDiff = 0;
            timeoutToStatus = millis() + LCD_TIMEOUT_TO_STATUS;
        }
        if (LCD_CLICKED)
            timeoutToStatus = millis() + LCD_TIMEOUT_TO_STATUS;
#endif//ULTIPANEL

#ifdef DOGLCD        // Changes due to different driver architecture of the DOGM display
        blink++;     // Variable for fan animation and alive dot
        u8g.firstPage();
        do
        {
            u8g.setFont(u8g_font_6x10_marlin);
            u8g.setPrintPos(125,0);
            if (blink % 2) u8g.setColorIndex(1); else u8g.setColorIndex(0); // Set color for the alive dot
            u8g.drawPixel(127,63); // draw alive dot
            u8g.setColorIndex(1); // black on white
            (*currentMenu)();
            #ifdef BABYSTEPPING
            if(z_offsetting && timeoutToStatus-14000 < millis())
            {
              u8g.setColorIndex(0);
              u8g.drawBox(114,48,16,13);
              u8g.drawBox(34,49,16,13);
              u8g.setColorIndex(1);
              u8g.drawBitmapP(113,51,2,10,up_arrow_bmp);
              u8g.drawBitmapP(34,49,2,10,down_arrow_bmp);
            }
            #endif
            if (!lcdDrawUpdate)  break; // Terminate display update, when nothing new to draw. This must be done before the last dogm.next()
        } while( u8g.nextPage() );
#else
        (*currentMenu)();
#endif

#ifdef LCD_HAS_STATUS_INDICATORS
        lcd_implementation_update_indicators();
#endif

#ifdef ULTIPANEL
        if(timeoutToStatus < millis() && currentMenu != lcd_status_screen)
        {
            lcd_return_to_status();
            lcdDrawUpdate = 2;
            z_offsetting = false;
        }
#endif//ULTIPANEL
        if (lcdDrawUpdate == 2) lcd_implementation_clear();
        if (lcdDrawUpdate) lcdDrawUpdate--;
        lcd_next_update_millis = millis() + LCD_UPDATE_INTERVAL;
    }
}

void lcd_ignore_click(bool b)
{
    ignore_click = b;
    wait_for_unclick = false;
}

void lcd_finishstatus() {
  int len = strlen(lcd_status_message);
  if (len > 0) {
    while (len < LCD_WIDTH) {
      lcd_status_message[len++] = ' ';
    }
  }
  lcd_status_message[LCD_WIDTH] = '\0';
  #if defined(LCD_PROGRESS_BAR) && defined(SDSUPPORT)
    #if PROGRESS_MSG_EXPIRE > 0
      messageTick =
    #endif
    progressBarTick = millis();
  #endif
  lcdDrawUpdate = 2;

  #ifdef FILAMENT_LCD_DISPLAY
    message_millis = millis();  //get status message to show up for a while
  #endif
}
void lcd_setstatus(const char* message)
{
    if (lcd_status_message_level > 0)
        return;
    strncpy(lcd_status_message, message, LCD_WIDTH);
    lcd_finishstatus();
}
void lcd_setstatuspgm(const char* message)
{
    if (lcd_status_message_level > 0)
        return;
    strncpy_P(lcd_status_message, message, LCD_WIDTH);
    lcd_finishstatus();
}
void lcd_setalertstatuspgm(const char* message)
{
    lcd_setstatuspgm(message);
    lcd_status_message_level = 1;
#ifdef ULTIPANEL
    lcd_return_to_status();
#endif//ULTIPANEL
}
void lcd_reset_alert_level()
{
    lcd_status_message_level = 0;
}

#ifdef DOGLCD
void lcd_setcontrast(uint8_t value)
{
    lcd_contrast = value & 63;
    u8g.setContrast(lcd_contrast);
}
#endif

#ifdef ULTIPANEL
/* Warning: This function is called from interrupt context */
void lcd_buttons_update()
{
#ifdef NEWPANEL
    uint8_t newbutton=0;
    if(READ(BTN_EN1)==0)  newbutton|=EN_A;
    if(READ(BTN_EN2)==0)  newbutton|=EN_B;
  #if BTN_ENC > 0
    if((blocking_enc<millis()) && (READ(BTN_ENC)==0))
        newbutton |= EN_C;
  #endif
    buttons = newbutton;
    #ifdef LCD_HAS_SLOW_BUTTONS
    buttons |= slow_buttons;
    #endif
    #ifdef REPRAPWORLD_KEYPAD
      // for the reprapworld_keypad
      uint8_t newbutton_reprapworld_keypad=0;
      WRITE(SHIFT_LD,LOW);
      WRITE(SHIFT_LD,HIGH);
      for(int8_t i=0;i<8;i++) {
          newbutton_reprapworld_keypad = newbutton_reprapworld_keypad>>1;
          if(READ(SHIFT_OUT))
              newbutton_reprapworld_keypad|=(1<<7);
          WRITE(SHIFT_CLK,HIGH);
          WRITE(SHIFT_CLK,LOW);
      }
      buttons_reprapworld_keypad=~newbutton_reprapworld_keypad; //invert it, because a pressed switch produces a logical 0
	#endif
#else   //read it from the shift register
    uint8_t newbutton=0;
    WRITE(SHIFT_LD,LOW);
    WRITE(SHIFT_LD,HIGH);
    unsigned char tmp_buttons=0;
    for(int8_t i=0;i<8;i++)
    {
        newbutton = newbutton>>1;
        if(READ(SHIFT_OUT))
            newbutton|=(1<<7);
        WRITE(SHIFT_CLK,HIGH);
        WRITE(SHIFT_CLK,LOW);
    }
    buttons=~newbutton; //invert it, because a pressed switch produces a logical 0
#endif//!NEWPANEL

    //manage encoder rotation
    uint8_t enc=0;
    if (buttons & EN_A) enc |= B01;
    if (buttons & EN_B) enc |= B10;
    if(enc != lastEncoderBits)
    {
        switch(enc)
        {
        case encrot0:
            if(lastEncoderBits==encrot3)
                encoderDiff++;
            else if(lastEncoderBits==encrot1)
                encoderDiff--;
            break;
        case encrot1:
            if(lastEncoderBits==encrot0)
                encoderDiff++;
            else if(lastEncoderBits==encrot2)
                encoderDiff--;
            break;
        case encrot2:
            if(lastEncoderBits==encrot1)
                encoderDiff++;
            else if(lastEncoderBits==encrot3)
                encoderDiff--;
            break;
        case encrot3:
            if(lastEncoderBits==encrot2)
                encoderDiff++;
            else if(lastEncoderBits==encrot0)
                encoderDiff--;
            break;
        }
    }
    lastEncoderBits = enc;
}

bool lcd_detected(void)
{
#if (defined(LCD_I2C_TYPE_MCP23017) || defined(LCD_I2C_TYPE_MCP23008)) && defined(DETECT_DEVICE)
  return lcd.LcdDetected() == 1;
#else
  return true;
#endif
}

void lcd_buzz(long duration, uint16_t freq)
{
#ifdef LCD_USE_I2C_BUZZER
  lcd.buzz(duration,freq);
#endif
}

bool lcd_clicked()
{
  return LCD_CLICKED;
}
#endif//ULTIPANEL

/********************************/
/** Float conversion utilities **/
/********************************/
//  convert float to string with +123.4 format
char conv[8];
char *ftostr3(const float &x)
{
  return itostr3((int)x);
}

char *itostr2(const uint8_t &x)
{
  //sprintf(conv,"%5.1f",x);
  int xx=x;
  conv[0]=(xx/10)%10+'0';
  conv[1]=(xx)%10+'0';
  conv[2]=0;
  return conv;
}

// Convert float to string with 123.4 format, dropping sign
char *ftostr31(const float &x)
{
  int xx=x*10;
  conv[0]=(xx>=0)?'+':'-';
  xx=abs(xx);
  conv[1]=(xx/1000)%10+'0';
  conv[2]=(xx/100)%10+'0';
  conv[3]=(xx/10)%10+'0';
  conv[4]='.';
  conv[5]=(xx)%10+'0';
  conv[6]=0;
  return conv;
}

// Convert float to string with 123.4 format
char *ftostr31ns(const float &x)
{
  int xx=x*10;
  //conv[0]=(xx>=0)?'+':'-';
  xx=abs(xx);
  conv[0]=(xx/1000)%10+'0';
  conv[1]=(xx/100)%10+'0';
  conv[2]=(xx/10)%10+'0';
  conv[3]='.';
  conv[4]=(xx)%10+'0';
  conv[5]=0;
  return conv;
}

char *ftostr32(const float &x)
{
  long xx=x*100;
  if (xx >= 0)
    conv[0]=(xx/10000)%10+'0';
  else
    conv[0]='-';
  xx=abs(xx);
  conv[1]=(xx/1000)%10+'0';
  conv[2]=(xx/100)%10+'0';
  conv[3]='.';
  conv[4]=(xx/10)%10+'0';
  conv[5]=(xx)%10+'0';
  conv[6]=0;
  return conv;
}

// Convert float to string with 1.234 format
char *ftostr43(const float &x)
{
    long xx = x * 1000;
    if (xx >= 0)
    {
      conv[0] = (xx / 1000) % 10 + '0';
      xx = abs(xx);
      conv[1] = '.';
      conv[2] = (xx / 100) % 10 + '0';
      conv[3] = (xx / 10) % 10 + '0';
      conv[4] = (xx) % 10 + '0';
      conv[5] = 0;
    }
    else
    {
      conv[0] = '-';
      xx = abs(xx);
      conv[1] = (xx / 1000) % 10 + '0';
      conv[2] = '.';
      conv[3] = (xx / 100) % 10 + '0';
      conv[4] = (xx / 10) % 10 + '0';
      conv[5] = (xx) % 10 + '0';
      conv[6] = 0;
    }
    return conv;
}

//Float to string with 12.3 format
char *ftostr13ns(const float &x)
{
  long xx=x*10;
  
  xx=abs(xx);
  conv[0]=(xx/100)%10+'0';
  conv[1]=(xx/10)%10+'0';
  conv[2]='.';
  conv[3]=(xx)%10+'0';
  conv[4]=0;
  return conv;
}

//Float to string with 1.23 format
char *ftostr12ns(const float &x)
{
  long xx=x*100;
  
  xx=abs(xx);
  conv[0]=(xx/100)%10+'0';
  conv[1]='.';
  conv[2]=(xx/10)%10+'0';
  conv[3]=(xx)%10+'0';
  conv[4]=0;
  return conv;
}

//  convert float to space-padded string with -_23.4_ format
char *ftostr32sp(const float &x) {
  long xx = abs(x * 100);
  uint8_t dig;

  if (x < 0) { // negative val = -_0
    conv[0] = '-';
    dig = (xx / 1000) % 10;
    conv[1] = dig ? '0' + dig : ' ';
  }
  else { // positive val = __0
    dig = (xx / 10000) % 10;
    if (dig) {
      conv[0] = '0' + dig;
      conv[1] = '0' + (xx / 1000) % 10;
    }
    else {
      conv[0] = ' ';
      dig = (xx / 1000) % 10;
      conv[1] = dig ? '0' + dig : ' ';
    }
  }

  conv[2] = '0' + (xx / 100) % 10; // lsd always

  dig = xx % 10;
  if (dig) { // 2 decimal places
    conv[5] = '0' + dig;
    conv[4] = '0' + (xx / 10) % 10;
    conv[3] = '.';
  }
  else { // 1 or 0 decimal place
    dig = (xx / 10) % 10;
    if (dig) {
      conv[4] = '0' + dig;
      conv[3] = '.';
    }
    else {
      conv[3] = conv[4] = ' ';
    }
    conv[5] = ' ';
  }
  conv[6] = '\0';
  return conv;
}

char *itostr31(const int &xx)
{
  conv[0]=(xx>=0)?'+':'-';
  conv[1]=(xx/1000)%10+'0';
  conv[2]=(xx/100)%10+'0';
  conv[3]=(xx/10)%10+'0';
  conv[4]='.';
  conv[5]=(xx)%10+'0';
  conv[6]=0;
  return conv;
}

// Convert int to rj string with 123 or -12 format
char *itostr3(const int &x)
{
  int xx = x;
  if (xx < 0) {
     conv[0]='-';
     xx = -xx;
  } else if (xx >= 100)
    conv[0]=(xx/100)%10+'0';
  else
    conv[0]=' ';
  if (xx >= 10)
    conv[1]=(xx/10)%10+'0';
  else
    conv[1]=' ';
  conv[2]=(xx)%10+'0';
  conv[3]=0;
  return conv;
}

// Convert int to lj string with 123 format
char *itostr3left(const int &xx)
{
  if (xx >= 100)
  {
    conv[0]=(xx/100)%10+'0';
    conv[1]=(xx/10)%10+'0';
    conv[2]=(xx)%10+'0';
    conv[3]=0;
  }
  else if (xx >= 10)
  {
    conv[0]=(xx/10)%10+'0';
    conv[1]=(xx)%10+'0';
    conv[2]=0;
  }
  else
  {
    conv[0]=(xx)%10+'0';
    conv[1]=0;
  }
  return conv;
}

// Convert int to rj string with 1234 format
char *itostr4(const int &xx) {
  conv[0] = xx >= 1000 ? (xx / 1000) % 10 + '0' : ' ';
  conv[1] = xx >= 100 ? (xx / 100) % 10 + '0' : ' ';
  conv[2] = xx >= 10 ? (xx / 10) % 10 + '0' : ' ';
  conv[3] = xx % 10 + '0';
  conv[4] = 0;
  return conv;
}

// Convert float to rj string with 12345 format
char *ftostr5(const float &x) {
  long xx = abs(x);
  conv[0] = xx >= 10000 ? (xx / 10000) % 10 + '0' : ' ';
  conv[1] = xx >= 1000 ? (xx / 1000) % 10 + '0' : ' ';
  conv[2] = xx >= 100 ? (xx / 100) % 10 + '0' : ' ';
  conv[3] = xx >= 10 ? (xx / 10) % 10 + '0' : ' ';
  conv[4] = xx % 10 + '0';
  conv[5] = 0;
  return conv;
}

// Convert float to string with +1234.5 format
char *ftostr51(const float &x)
{
  long xx=x*10;
  conv[0]=(xx>=0)?'+':'-';
  xx=abs(xx);
  conv[1]=(xx/10000)%10+'0';
  conv[2]=(xx/1000)%10+'0';
  conv[3]=(xx/100)%10+'0';
  conv[4]=(xx/10)%10+'0';
  conv[5]='.';
  conv[6]=(xx)%10+'0';
  conv[7]=0;
  return conv;
}

// Convert float to string with +123.45 format
char *ftostr52(const float &x)
{
  long xx=x*100;
  conv[0]=(xx>=0)?'+':'-';
  xx=abs(xx);
  conv[1]=(xx/10000)%10+'0';
  conv[2]=(xx/1000)%10+'0';
  conv[3]=(xx/100)%10+'0';
  conv[4]='.';
  conv[5]=(xx/10)%10+'0';
  conv[6]=(xx)%10+'0';
  conv[7]=0;
  return conv;
}

// Callback for after editing PID i value
// grab the PID i value out of the temp variable; scale it; then update the PID driver
void copy_and_scalePID_i()
{
#ifdef PIDTEMP
  Ki = scalePID_i(raw_Ki);
  updatePID();
#endif
}

// Callback for after editing PID d value
// grab the PID d value out of the temp variable; scale it; then update the PID driver
void copy_and_scalePID_d()
{
#ifdef PIDTEMP
  Kd = scalePID_d(raw_Kd);
  updatePID();
#endif
}

#endif //ULTRA_LCD