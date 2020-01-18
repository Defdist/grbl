/*
  limits.c - code pertaining to limit-switches and performing the homing cycle
  Part of Grbl

  Copyright (c) 2012-2016 Sungeun K. Jeon for Gnea Research LLC
  Copyright (c) 2009-2011 Simen Svale Skogsrud

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

#include "grbl.h"


// Homing axis search distance multiplier. Computed by this value times the cycle travel.
#ifndef HOMING_AXIS_SEARCH_SCALAR
  #define HOMING_AXIS_SEARCH_SCALAR  1.5 // Must be > 1 to ensure limit switch will be engaged.
#endif
#ifndef HOMING_AXIS_LOCATE_SCALAR
  #define HOMING_AXIS_LOCATE_SCALAR  5.0 // Must be > 1 to ensure limit switch is cleared.
#endif

void limits_init()
{
  LIMIT_DDR &= ~(LIMIT_MASK); // Set as input pins
  LIMIT_X1_DDR &= ~(LIMIT_MASK); //Set as input pin

  #ifdef DISABLE_LIMIT_PIN_PULL_UP
    LIMIT_PORT &= ~(LIMIT_MASK); // Normal low operation. Requires external pull-down.
    LIMIT_X1_PORT &= ~(LIMIT_X1_MASK);  //X1 limit switch only used for autolevel
  #else
    LIMIT_PORT |= (LIMIT_X1_MASK);  // Enable internal pull-up resistors. Normal high operation.
    LIMIT_X1_PORT |= (LIMIT_X1_MASK);  //X1 limit switch only used for autolevel
  #endif

  if (bit_istrue(settings.flags,BITFLAG_HARD_LIMIT_ENABLE)) {
    LIMIT_PCMSK |= LIMIT_MASK; // Enable specific pins of the Pin Change Interrupt
    PCICR |= (1 << LIMIT_INT); // Enable Pin Change Interrupt
  } else {
    limits_disable();
  }
}


// Disables hard limits.
void limits_disable()
{
  LIMIT_PCMSK &= ~LIMIT_MASK;  // Disable specific pins of the Pin Change Interrupt
  PCICR &= ~(1 << LIMIT_INT);  // Disable Pin Change Interrupt
}


// Returns limit state as a bit-wise uint8 variable. Each bit indicates an axis limit, where
// triggered is 1 and not triggered is 0. Invert mask is applied. Axes are defined by their
// number in bit position, i.e. Z_AXIS is (1<<2) or bit 2, and Y_AXIS is (1<<1) or bit 1.
uint8_t limits_get_state()
{
  uint8_t limit_state = 0;
  uint8_t pin = (LIMIT_PIN & LIMIT_MASK);
  if (bit_isfalse(settings.flags,BITFLAG_INVERT_LIMIT_PINS)) { pin ^= LIMIT_MASK; }
  if (pin) {
    uint8_t idx;
    for (idx=0; idx<N_AXIS; idx++) {
      if (pin & get_limit_pin_mask(idx)) { limit_state |= (1 << idx); }
    }
  }
  return(limit_state);
}


//X1 limit switch is not in the same PORT as the interrupt limits, and cannot be a limit interrupt.
//This function should only be used during autolevel.  Spindle should be off to reduce (unfiltered) noise.
uint8_t limits_X1_get_state() //returns true if X1 limit is tripped
{
  uint8_t limit_state = 0;
  uint8_t X1_pin_state = (LIMIT_X1_PIN & LIMIT_X1_MASK); //return only X1 pin state (high or low) from PORT
  if (bit_isfalse(settings.flags,BITFLAG_INVERT_LIMIT_PINS)) { X1_pin_state ^= LIMIT_X1_MASK; }
  if (X1_pin_state & LIMIT_X1_MASK) { limit_state = 1; } // limit X1 is tripped (regardless of logic level)
  else { limit_state = 0; } //limit X1 not tripped (regardless of logic level; see BITFLAG_INVERT_LIMIT_PINS)
  return(limit_state);
}


// This is the Limit Pin Change Interrupt, which handles the hard limit feature. A bouncing
// limit switch can cause a lot of problems, like false readings and multiple interrupt calls.
// If a switch is triggered at all, something bad has happened and treat it as such, regardless
// if a limit switch is being disengaged. It's impossible to reliably tell the state of a
// bouncing pin because the Arduino microcontroller does not retain any state information when
// detecting a pin change. If we poll the pins in the ISR, you can miss the correct reading if the 
// switch is bouncing.
// NOTE: Do not attach an e-stop to the limit pins, because this interrupt is disabled during
// homing cycles and will not respond correctly. 
ISR(LIMIT_INT_vect) //Limit pin change interrupt process.
{
  // Ignore limit switches if already in an alarm state or in-process of executing an alarm.
  // When in the alarm state, Grbl should have been reset or will force a reset, so any pending
  // moves in the planner and serial buffers are all cleared and newly sent blocks will be
  // locked out until a homing cycle or a kill lock command. Allows the user to disable the hard
  // limit setting if their limits are constantly triggering after a reset and move their axes.
  if (sys.state != STATE_ALARM) {
    if (!(sys_rt_exec_alarm)) {
      mc_reset(); // Initiate system kill.
      system_set_exec_alarm(EXEC_ALARM_HARD_LIMIT); // Indicate hard limit critical event
    }
  }
}


// Home the specified cycle axes, set machine position, and perform a pull-off motion after
// completing. Homing is a special motion case, which involves rapid uncontrolled stops to locate
// the trigger point of the limit switches. The rapid stops are handled by a system level axis lock
// mask, which prevents the stepper algorithm from executing step pulses. Homing motions typically
// circumvent the processes for executing motions in normal operation.
// NOTE: Only the abort realtime command can interrupt this process.
// TODO: Move limit pin-specific calls to a general function for portability.
void limits_go_home(uint8_t cycle_mask) //runs once for each part of the homing cycle...
{  //for $H: runs once for Z (HOMING_CYCLE0), then once for X&Y (HOMING_CYCLE_1).
   //For $HX: HOMING_CYCLE_X=0, $HY: HOMING_CYCLE_Y=1, $HZ: HOMING_CYCLE_Z=2
  if (sys.abort) { return; } // Block if system reset has been issued.

  // Initialize plan data struct for homing motion. Spindle is disabled.
  plan_line_data_t plan_data;
  plan_line_data_t *pl_data = &plan_data; //The address of plan_data is written to "*pl_data" 
                                          //"pl_data" is a pointer to structure "plan_data"
  memset(pl_data,0,sizeof(plan_line_data_t));
  pl_data->condition = (PL_COND_FLAG_SYSTEM_MOTION|PL_COND_FLAG_NO_FEED_OVERRIDE);
  #ifdef USE_LINE_NUMBERS
    pl_data->line_number = HOMING_CYCLE_LINE_NUMBER;
  #endif

  // Initialize variables used for homing computations.
  uint8_t n_cycle = (2*N_HOMING_LOCATE_CYCLE+1);  //number of times to locate limit switch
  uint8_t step_pin[N_AXIS];//the physical port pin for each axis' stepper
  float target[N_AXIS]; //target[3];
  float max_travel = 0.0; //stored as a negative value
  uint8_t idx;
  for (idx=0; idx<N_AXIS; idx++) {
    step_pin[idx] = get_step_pin_mask(idx); // Create step pin masks (the physical pin location on the port)
    if (bit_istrue(cycle_mask,bit(idx))) { //If X/Y/Z axis is part of this homing cycle, set axis max travel
      // Set target based on max_travel setting. Ensure homing switches engaged with search scalar.
      // NOTE: settings.max_travel[] is stored as a negative value.
      max_travel = max(max_travel,(-HOMING_AXIS_SEARCH_SCALAR)*settings.max_travel[idx]);
    }
  }

  // Set search mode with approach at seek rate to quickly engage the specified cycle_mask limit switches.
  bool approach = true;
  float homing_rate = settings.homing_seek_rate;
  uint8_t limit_state; //tripped state of all limit switches
  uint8_t axislock;
  uint8_t n_active_axis;

  do { //runs once for each stepper direction change during homing cycle
    system_convert_array_steps_to_mpos(target,sys_position); //convert steps to mm on all three axes

    // Initialize and declare variables needed for homing routine.
    axislock = 0; //bitmask: '1' = axis enabled, '0' = locked
    n_active_axis = 0; //number of axes simultaneously home
    for (idx=0; idx<N_AXIS; idx++) {
      // Set target location for active axes and setup computation for homing rate.
      if (bit_istrue(cycle_mask,bit(idx))) {
        n_active_axis++;
        sys_position[idx] = 0;

        // For axis to home, set target position to either + or -, depending on limit switch location.
        // NOTE: This happens to compile smaller than any other implementation tried.
        if (bit_istrue(settings.homing_dir_mask,bit(idx))) { 
          if (approach) { target[idx] = -max_travel; }  // After initial pass, max_travel changes to pulloff distance
          else { target[idx] = max_travel; } 
        } else {
          if (approach) { target[idx] = max_travel; }
          else { target[idx] = -max_travel; }
        }

        // bitmask that enables motion on a particular axis.  '1' = axis enabled, '0' = locked
        axislock |= step_pin[idx];
      }
    }
    homing_rate *= sqrt(n_active_axis); // [sqrt(N_AXIS)] Scale so individual axes all move at homing rate.
    sys.homing_axis_lock = axislock;  //bitmask: '1' = axis enabled, '0' = locked

    // Perform homing cycle. Planner buffer should be empty, as required to initiate the homing cycle.
    pl_data->feed_rate = homing_rate; // Set current homing rate.
    plan_buffer_line(target, pl_data); // Bypass mc_line(). Directly plan homing motion.

    sys.step_control = STEP_CONTROL_EXECUTE_SYS_MOTION; // Set to execute homing motion and clear existing flags.
    st_prep_buffer(); // Prep and fill segment buffer from newly planned block.
    st_wake_up(); // Enable steppers
    do { 
      if (approach) {  // true when moving towards limit switch on enabled axis/axes
        // Check limit state. Lock out cycle axes when they change.
        limit_state = limits_get_state(); //bitmask: true if limit switch tripped
        for (idx=0; idx<N_AXIS; idx++) { //check each axis
          if (axislock & step_pin[idx]) { //if axis is enabled 
            if (limit_state & (1 << idx)) { //if limit switch for this axis is tripped
              axislock &= ~(step_pin[idx]); //disable this axis
            }
          }
        }
        sys.homing_axis_lock = axislock; //update which axes are still enabled
      } //when this finishes, enabled axis/axes are sitting tripped on limit switch

      st_prep_buffer(); // Check and prep segment buffer. NOTE: Should take no longer than 200us.

      // Exit routines: No time to run protocol_execute_realtime() in this loop.
      if (sys_rt_exec_state & (EXEC_RESET | EXEC_CYCLE_STOP)) {
        uint8_t rt_exec = sys_rt_exec_state;
        // Homing failure condition: Reset issued during cycle.
        if (rt_exec & EXEC_RESET) { system_set_exec_alarm(EXEC_ALARM_HOMING_FAIL_RESET); }
        
        // Homing failure condition: Limit switch still engaged after pull-off motion
        if (!approach && (limits_get_state() & cycle_mask)) { system_set_exec_alarm(EXEC_ALARM_HOMING_FAIL_PULLOFF); }
        
        // Homing failure condition: Limit switch not found during approach.
        if (approach && (rt_exec & EXEC_CYCLE_STOP)) { system_set_exec_alarm(EXEC_ALARM_HOMING_FAIL_APPROACH); }
        
        if (sys_rt_exec_alarm) {
          mc_reset(); // Stop motors, if they are running.
          protocol_execute_realtime();
          return;
        } else {
          // Pull-off motion complete. Disable CYCLE_STOP from executing.
          system_clear_exec_state_flag(EXEC_CYCLE_STOP);
          break;
        }
      }
    } while (STEP_MASK & axislock); //keep moving towards limit switches as long as at least one axis hasn't hit limit switch

    st_reset(); // Immediately force kill steppers and reset step segment buffer.
    delay_ms(settings.homing_debounce_delay); // Delay to allow transient dynamics to dissipate.

    // Reverse direction
    approach = !approach;

    // After first approach, need to pull away far enough to ensure limit switches reset.
    // After second approach, homing enters locating phase.
    if (approach) {
      if (n_cycle == 2*N_HOMING_LOCATE_CYCLE) { //2nd time we move towards.  Makeup initial pulloff
        max_travel = settings.homing_pulloff*HOMING_AXIS_LOCATE_SCALAR + DISTANCE_FIRST_PULLAWAY;
        homing_rate = settings.homing_seek_rate; //fine
      } else { //3rd, 4th, 5th, etc times we move towards.
        max_travel = settings.homing_pulloff*HOMING_AXIS_LOCATE_SCALAR;
        homing_rate = settings.homing_feed_rate; //fine
      }
    } else if (n_cycle == 2*N_HOMING_LOCATE_CYCLE+1) {//1st time we move away.  Ensures limits untrip
      max_travel = DISTANCE_FIRST_PULLAWAY;
      homing_rate = settings.homing_seek_rate; //coarse
    } else { //2nd, 3rd, 4th, etc times we move away
      max_travel = settings.homing_pulloff;
      homing_rate = settings.homing_seek_rate; //coarse
    }
  } while (n_cycle-- > 0);
  //when this loop finishes, the homed axis/axes are positioned right where the limit switch tripped

  // The active cycle axes should now be homed and machine limit(s) have been located. By
  // default, Grbl defines machine space as all negative, as do most CNCs. Since limit switches
  // can be on either side of an axes, check and set axes machine zero appropriately. Also,
  // set up pull-off maneuver from axes limit switches that have been homed. This provides
  // some initial clearance off the switches and should also help prevent them from falsely
  // triggering when hard limits are enabled or when more than one axes shares a limit pin.
  int32_t set_axis_position;
  // Set machine positions for homed limit switches. Don't update non-homed axes.
  for (idx=0; idx<N_AXIS; idx++) {
    // NOTE: settings.max_travel[] is stored as a negative value.
    if (cycle_mask & bit(idx)) {
      if ( bit_istrue(settings.homing_dir_mask,bit(idx)) ) {
        set_axis_position = lround((settings.max_travel[idx]+settings.homing_pulloff)*settings.steps_per_mm[idx]);
      } else {
        set_axis_position = lround(-settings.homing_pulloff*settings.steps_per_mm[idx]);
      }
    sys_position[idx] = set_axis_position;
    }
  }
  sys.step_control = STEP_CONTROL_NORMAL_OP; // Return step control to normal operation.
}


// Performs a soft limit check. Called from mc_line() only. Assumes the machine has been homed,
// the workspace volume is in all negative space, and the system is in normal operation.
// NOTE: Used by jogging to limit travel within soft-limit volume.
void limits_soft_check(float *target)
{
  if (system_check_travel_limits(target)) {
    sys.soft_limit = true;
    // Force feed hold if cycle is active. All buffered blocks are guaranteed to be within
    // workspace volume so just come to a controlled stop so position is not lost. When complete
    // enter alarm mode.
    if (sys.state == STATE_CYCLE) {
      system_set_exec_state_flag(EXEC_FEED_HOLD);
      do {
        protocol_execute_realtime();
        if (sys.abort) { return; }
      } while ( sys.state != STATE_IDLE );
    }
    mc_reset(); // Issue system reset and ensure spindle is shutdown.
    system_set_exec_alarm(EXEC_ALARM_SOFT_LIMIT); // Indicate soft limit critical event
    protocol_execute_realtime(); // Execute to enter critical event loop and system abort
    return;
  }
}


//move both steppers simultaneosly, noting when each limit switch trips
int16_t limits_find_trip_delta_X1X2() 
{ 
  if (sys.abort) { return(0); } // Block if system reset has been issued.  
  float target[N_AXIS]; //target[3]
  uint8_t limit_X1_tripped = 0; //stored to prevent state changes during loop
  uint8_t limit_X2_tripped = 0;
  int16_t trip_position_X1 = 0; //stored machine position when limit switch tripped
  int16_t trip_position_X2 = 0;

  // Initialize plan data struct for homing motion. Spindle is disabled.
  plan_line_data_t plan_data;
  plan_line_data_t *pl_data = &plan_data; //The address of plan_data is written to "*pl_data" 
                                          //"pl_data" is a pointer to structure "plan_data"
  memset(pl_data,0,sizeof(plan_line_data_t));
  pl_data->condition = (PL_COND_FLAG_SYSTEM_MOTION|PL_COND_FLAG_NO_FEED_OVERRIDE);
  #ifdef USE_LINE_NUMBERS
    pl_data->line_number = HOMING_CYCLE_LINE_NUMBER;
  #endif
  //Ready to plan motion

  //*******************************************************************************

  // move X away from limit switches to ensure X1 isn't already tripped
  system_convert_array_steps_to_mpos(target,sys_position); //convert steps to mm on all three axes
  sys_position[X_AXIS] = 0;
  //determine which direction is towards limit switch
  if (bit_istrue(settings.homing_dir_mask,bit(X_AXIS)) ) { target[X_AXIS] = DISTANCE_FIRST_PULLAWAY; } 
  else { target[X_AXIS] = (-DISTANCE_FIRST_PULLAWAY); }
  sys.homing_axis_lock = get_step_pin_mask(X_AXIS); //enable X axis motion
  pl_data->feed_rate = settings.homing_seek_rate; //move away quickly  //JTS2do: change to seek once motion understood
  plan_buffer_line(target, pl_data); // Bypass mc_line(). Directly plan motion.
  sys.step_control = STEP_CONTROL_EXECUTE_SYS_MOTION; // Set to execute motion and clear existing flags.
  st_prep_buffer(); // Prep and fill segment buffer from newly planned block.
  st_wake_up(); // Enable steppers  
  do { //move away from X limits until both limits aren't tripped
    st_prep_buffer(); // Check and prep segment buffer
    // update limit switch positions
    limit_X1_tripped = limits_X1_get_state();
    limit_X2_tripped = (limits_get_state() & (1<<X_AXIS));
  } while (limit_X1_tripped || limit_X2_tripped);
  //we've now moved away from limit switches until both X1 & X2 are NOT tripped
  st_reset(); // Immediately force kill stepper interrupts and reset step segment buffer.
  delay_ms(settings.homing_debounce_delay); // Delay to allow transient dynamics to dissipate.

  //*******************************************************************************

  //move X towards limits until both are tripped
  system_convert_array_steps_to_mpos(target,sys_position); //convert steps to mm on all three axes
  sys_position[X_AXIS] = 0;
  //determine which direction is towards limit switch
  float max_travel = (-HOMING_AXIS_SEARCH_SCALAR)*settings.max_travel[X_AXIS]; //stored as a negative value
  if( bit_istrue(settings.homing_dir_mask,bit(X_AXIS)) ) { target[X_AXIS] = -max_travel; }
  else { target[X_AXIS] = max_travel; } 
  sys.homing_axis_lock = get_step_pin_mask(X_AXIS); //enable X axis
  bool helper_X1 = 1; // false after X1 position logged
  bool helper_X2 = 1; // false after X2 position logged
  // Planner buffer should be empty before initiating table level cycle.
  pl_data->feed_rate = settings.homing_feed_rate; // same feedrate as fine homing motion
  plan_buffer_line(target, pl_data); // Bypass mc_line(). Directly plan homing motion.
  sys.step_control = STEP_CONTROL_EXECUTE_SYS_MOTION; // Set to execute homing motion and clear existing flags.
  st_prep_buffer(); // Prep and fill segment buffer from newly planned block.
  st_wake_up(); // Enable steppers
  do { //move towards limits until both X1 & X2 limit switches are tripped   
    // update limit switch positions
    limit_X1_tripped = limits_X1_get_state();
    limit_X2_tripped = (limits_get_state() & (1<<X_AXIS));
    if(limit_X1_tripped && helper_X1) { //X1 just tripped
      trip_position_X1 = sys_position[X_AXIS]; //Store current machine position
      helper_X1 = 0; //don't run this if again
    }
    if(limit_X2_tripped && helper_X2) { //X2 just tripped
      trip_position_X2 = sys_position[X_AXIS]; //Store current machine position
      helper_X2 = 0; //don't run this if again
    }
    st_prep_buffer(); // Check and prep segment buffer. NOTE: Should take no longer than 200us.
  } while (!(limit_X1_tripped && limit_X2_tripped)); 

  st_reset(); // Immediately force kill steppers and reset step segment buffer.
  delay_ms(settings.homing_debounce_delay); // Delay to allow transient dynamics to dissipate.

  //*******************************************************************************

  // move X away from limit switches to prevent tripping during squaring routine
  system_convert_array_steps_to_mpos(target,sys_position); //convert steps to mm on all three axes
  sys_position[X_AXIS] = 0;
  if (bit_istrue(settings.homing_dir_mask,bit(X_AXIS)) ) { target[X_AXIS] = DISTANCE_FIRST_PULLAWAY; } 
  else { target[X_AXIS] = (-DISTANCE_FIRST_PULLAWAY); }
  sys.homing_axis_lock = get_step_pin_mask(X_AXIS); //enable X axis motion
  pl_data->feed_rate = settings.homing_seek_rate; //move away quickly  //JTS2do: change to seek once motion understood
  plan_buffer_line(target, pl_data); // Bypass mc_line(). Directly plan motion.
  sys.step_control = STEP_CONTROL_EXECUTE_SYS_MOTION; // Set to execute motion and clear existing flags.
  st_prep_buffer(); // Prep and fill segment buffer from newly planned block.
  st_wake_up(); // Enable steppers  
  do { //move away from X limits until both limits aren't tripped
    st_prep_buffer(); // Check and prep segment buffer
    if (sys_rt_exec_state & (EXEC_RESET | EXEC_CYCLE_STOP)) { // true when pull-off motion completes.
      system_clear_exec_state_flag(EXEC_CYCLE_STOP);  
      break;
    }
  } while (1); //loop keeps running until motion stops (breaks out when done)
  //at this point we've moved away from the limit switches
  st_reset(); // Immediately force kill stepper interrupts and reset step segment buffer.
  delay_ms(settings.homing_debounce_delay); // Delay to allow transient dynamics to dissipate.

  //*******************************************************************************

  //Put things back the way they were before all this uncontrolled motion started
  sys.step_control = STEP_CONTROL_NORMAL_OP; // Return step control to normal operation.

  return trip_position_X1 - trip_position_X2;  // Delta between limit switch X1 X2 trip points
}