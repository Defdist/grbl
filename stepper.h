/*
  stepper.h - stepper motor interface
  Part of Grbl

  Copyright (c) 2009 Simen Svale Skogsrud

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

#ifndef stepper_h
#define stepper_h 

#include <avr/io.h>
#include <avr/sleep.h>

#define STEPPER_MODE_STOPPED 0
#define STEPPER_MODE_RUNNING 1
#define STEPPER_MODE_LIMIT_OVERRUN 2
#define STEPPER_MODE_HOMING 3

// Initialize and start the stepper motor subsystem
void st_init();

// Returns a bitmask with the stepper bit for the given axis set
uint8_t st_bit_for_stepper(int axis);

// Buffer a new line segment (might block until there is room in the buffer)
void st_buffer_line(int32_t steps_x, int32_t steps_y, int32_t steps_z, uint32_t rate);

// Block until all buffered steps are executed
void st_synchronize();

// Cancel all pending steps
void st_flush();

// Start the stepper subsystem
void st_start();

// Execute all buffered steps, then stop the stepper subsystem
inline void st_stop();

// Execute the homing cycle
void st_go_home();

// Echo steps to serial port? (true/false)
void st_set_echo(int value);

// Convert from millimeters to step-counts along the designated axis
int32_t st_millimeters_to_steps(double millimeters, int axis);

#endif