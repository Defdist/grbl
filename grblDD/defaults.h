/*
  defaults.h - defaults settings configuration file
  Part of Grbl

  Copyright (c) 2012-2016 Sungeun K. Jeon for Gnea Research LLC

  Grbl is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Grbl is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Grbl.  If not, see <http://www.gnu.org/licenses/>.
*/

/* The defaults.h file serves as a central default settings selector for different machine
   types, from DIY CNC mills to CNC conversions of off-the-shelf machines. The settings
   files listed here are supplied by users, so your results may vary. However, this should
   give you a good starting point as you get to know your machine and tweak the settings for
   your nefarious needs.
   NOTE: Ensure one and only one of these DEFAULTS_XXX values is defined in config.h */

#ifndef defaults_h

#ifdef DEFAULTS_GG3
  // Grbl generic default settings. Should work across different machines.
  #define DEFAULT_X_STEPS_PER_MM 400.0
  #define DEFAULT_Y_STEPS_PER_MM 400.0
  #define DEFAULT_Z_STEPS_PER_MM 400.0
  #define DEFAULT_X_MAX_RATE 2540.0 // mm/min
  #define DEFAULT_Y_MAX_RATE 3100.0 // mm/min
  #define DEFAULT_Z_MAX_RATE 3100.0 // mm/min
  #define DEFAULT_X_ACCELERATION (500.0*60*60) // __*60*60 mm/min^2 = __ mm/sec^2
  #define DEFAULT_Y_ACCELERATION (500.0*60*60) // __*60*60 mm/min^2 = __ mm/sec^2
  #define DEFAULT_Z_ACCELERATION (500.0*60*60) // __*60*60 mm/min^2 = __ mm/sec^2
  #define DEFAULT_X_MAX_TRAVEL 86.5  // mm //Must be a positive value. GG3 nom mech distance = 88 mm
  #define DEFAULT_Y_MAX_TRAVEL 241.5 // mm //Must be a positive value. GG3 nom mech distance = 242.9 mm
  #define DEFAULT_Z_MAX_TRAVEL 78.5  // mm //Must be a positive value. GG3 nom mech distance = 80 mm
  #define DEFAULT_SPINDLE_RPM_MAX 8000.0 // rpm
  #define DEFAULT_SPINDLE_RPM_MIN 0.0 // rpm
  #define DEFAULT_STEP_PULSE_MICROSECONDS 10
  #define DEFAULT_STEPPING_INVERT_MASK 0  // b00000ZYX
  #define DEFAULT_DIRECTION_INVERT_MASK 0 // b00000ZYX
  #define DEFAULT_STEPPER_IDLE_LOCK_TIME 100 // msec (0-254, 255 keeps steppers enabled)
  #define DEFAULT_STATUS_REPORT_MASK 127 // b01111111 
  #define DEFAULT_JUNCTION_DEVIATION 0.02 // mm
  #define DEFAULT_ARC_TOLERANCE 0.002 // mm
  #define DEFAULT_REPORT_INCHES 0 // false
  #define DEFAULT_INVERT_ST_ENABLE 0 // false
  #define DEFAULT_INVERT_LIMIT_PINS 1 // false
  #define DEFAULT_SOFT_LIMIT_ENABLE 1
  #define DEFAULT_HARD_LIMIT_ENABLE 1
  #define DEFAULT_INVERT_PROBE_PIN 0
  #define DEFAULT_LASER_MODE 0
  #define DEFAULT_HOMING_ENABLE 1
  #define DEFAULT_HOMING_DIR_MASK 1 // b0000001
  #define DEFAULT_HOMING_FINE_RATE 30.0 // mm/min
  #define DEFAULT_HOMING_SEEK_RATE 2000.0 // mm/min
  #define DEFAULT_HOMING_DEBOUNCE_DELAY 1 // msec (0-65k)
  #define DEFAULT_HOMING_PULLOFF 0.5 // mm
#endif

#endif
