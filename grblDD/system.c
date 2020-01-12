/*
  system.c - Handles system level commands and real-time processes
  Part of Grbl

  Copyright (c) 2014-2016 Sungeun K. Jeon for Gnea Research LLC

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


void system_init()
{
  DDRC &= ~(CONTROL_MASK); // Configure as input pins
  PCMSK1 |= CONTROL_MASK;  // Enable specific pins of the Pin Change Interrupt
  PCICR |= (1 << PCIE1);   // Enable Pin Change Interrupt
}

//JTS Pin change interrupt for probe and spindle overlaod
// ISR needs to be as fast as possible.
// Sets only the realtime command execute variable; the main program executes these when
// its ready. This works exactly like the character-based realtime commands when picked off
// directly from the incoming serial data stream.
ISR(PCINT1_vect)
{
  if(CONTROL_PIN & (1<<CONTROL_SPINDLE_OVERLOAD_BIT)) //true if spindle overload pin (A4) toggled to high
  {
    //bit_true(sys_rt_exec_state, EXEC_CYCLE_START);} //JTS2do: add "SLOW DOWN SPINDLE OVERLOADED"
  }
  else //true if spindle overload pin (A4) toggled to low
  {     //also true if probe tripped.  Probe signal is noisy, so reading it (again) here is unreliable.
    //JTS2do add "SPINDLE NO LONGER OVERLOADED"   
    PCMSK1 = CONTROL_MASK; //JTS disable probe interrupt vector (to prevent probe interrupts when not probing).
    sys.probe_interrupt_occurred = 1; //JTS log that probe occurred
  }
}

// Executes user startup script, if stored.
void system_execute_startup(char *line)
{
  uint8_t n;
  for (n=0; n < N_STARTUP_LINE; n++) {
    if (!(settings_read_startup_line(n, line))) {
      line[0] = 0;
      report_execute_startup_message(line,STATUS_SETTING_READ_FAIL);
    } else {
      if (line[0] != 0) {
        uint8_t status_code = gc_execute_line(line);
        report_execute_startup_message(line,status_code);
      }
    }
  }
}


// Directs and executes one line of formatted input from protocol_process. While mostly
// incoming streaming g-code blocks, this also executes Grbl internal commands, such as
// settings, initiating the homing cycle, and toggling switch states. This differs from
// the realtime command module by being susceptible to when Grbl is ready to execute the
// next line during a cycle, so for switches like block delete, the switch only effects
// the lines that are processed afterward, not necessarily real-time during a cycle,
// since there are motions already stored in the buffer. However, this 'lag' should not
// be an issue, since these commands are not typically used during a cycle.
uint8_t system_execute_line(char *line)
{
  uint8_t char_counter = 1;
  uint8_t helper_var = 0; // Helper variable
  float parameter, value;
  switch( line[char_counter] ) { //read second character (since we know first is '$')
    case 0 : report_grbl_help(); break; //entire line is '$' (protocol_main_loop() tacks '0' at end of each line)

    case 'J' : // $J = Jogging
      // Execute only if in IDLE or JOG states.
      if (sys.state != STATE_IDLE && sys.state != STATE_JOG) { return(STATUS_IDLE_ERROR); }
      if(line[2] != '=') { return(STATUS_INVALID_STATEMENT); }
      return(gc_execute_line(line)); // NOTE: $J= is ignored inside g-code parser and used to detect jog motions.
      break;

    case '$': case 'G': case 'C': case 'X':
      if ( line[2] != 0 ) { return(STATUS_INVALID_STATEMENT); }
      switch( line[1] ) {
        case '$' : // $$, Prints Grbl settings
          if ( sys.state & (STATE_CYCLE | STATE_HOLD) ) { return(STATUS_IDLE_ERROR); } // Block during cycle. Takes too long to print.
          else { report_grbl_settings(); }
          break;
        case 'G' : // $G = Prints gcode parser state
          // TODO: Move this to realtime commands for GUIs to request this data during suspend-state.
          report_gcode_modes();
          break;
        case 'C' : // $C = Set check g-code mode [IDLE/CHECK]
          // Perform reset when toggling off. Check g-code mode should only work if Grbl
          // is idle and ready, regardless of alarm locks. This is mainly to keep things
          // simple and consistent.
          if ( sys.state == STATE_CHECK_MODE ) {
            mc_reset();
            report_feedback_message(MESSAGE_DISABLED);
          } else {
            if (sys.state) { return(STATUS_IDLE_ERROR); } // Requires no alarm mode.
            sys.state = STATE_CHECK_MODE;
            report_feedback_message(MESSAGE_ENABLED);
          }
          break;
        case 'X' : // $X = Disable alarm lock [ALARM]
          if (sys.state == STATE_ALARM) {
            report_feedback_message(MESSAGE_ALARM_UNLOCK);
            sys.state = STATE_IDLE;
            // Don't run startup script. Prevents stored moves in startup from causing accidents.
          } // Otherwise, no effect.
          break;
      }
      break;

    default : //all other '$___' commands
      // Block any system command that requires the state as IDLE/ALARM. (i.e. EEPROM, homing)
      if ( !(sys.state == STATE_IDLE || sys.state == STATE_ALARM) ) { return(STATUS_IDLE_ERROR); }
      switch( line[1] ) {
        case '#' : // $# = Print Grbl NGC parameters
          if ( line[2] != 0 ) { return(STATUS_INVALID_STATEMENT); }
          else { report_ngc_parameters(); }
          break;

        case 'H' : // $H = Perform homing cycle [IDLE/ALARM]
          if (bit_isfalse(settings.flags,BITFLAG_HOMING_ENABLE)) {return(STATUS_SETTING_DISABLED); }          
          if (line[2] == 0) { // entire line is '$H' (i.e. not $HX)
            sys.state = STATE_HOMING; // Set system state variable
            mc_homing_cycle(HOMING_CYCLE_ALL);
          #ifdef HOMING_SINGLE_AXIS_COMMANDS
            } else if (line[3] == 0) {
              sys.state = STATE_HOMING; // Set system state variable
              switch (line[2]) {
                case 'X': mc_homing_cycle(HOMING_CYCLE_X); break; // $HX
                case 'Y': mc_homing_cycle(HOMING_CYCLE_Y); break; // $HY
                case 'Z': mc_homing_cycle(HOMING_CYCLE_Z); break; // $HZ
                default: return(STATUS_INVALID_STATEMENT);
              }
          #endif
          } else { return(STATUS_INVALID_STATEMENT); }
          if (!sys.abort) {  // Execute startup scripts after successful homing.
            sys.state = STATE_IDLE; // Set to IDLE when complete.
            st_go_idle(); // Set steppers to the idle state before returning.
            if (line[2] == 0) { system_execute_startup(line); }
          }
          break;

        case 'E' : // $E = report entire EEPROM
          if ( line[2] == 0 ) { report_read_EEPROM(); }
          else { return(STATUS_INVALID_STATEMENT); }
          break;

        case 'L' : // $L & $LS
          sys.state = STATE_HOMING; // Set system state variable
          if ( line[2] == 0 ) { //$L = autolevel X table using offset from EEPROM
            for (int ii=0 ; ii<2 ; ii++) { mc_autolevel_X(); } //run twice (1st gets table close to square)
          } //$L = autolevel X table using offset from EEPROM
          else if( ((line[2] == 'S') && (line[3] == 0)) ) { //$LS
            mc_X_is_level(); //$LS = determine existing 'squared' X offset and store in EEPROM
          } else { //neither $L nor $LS entered
            sys.state = STATE_IDLE; //exit level mode
            return(STATUS_INVALID_STATEMENT);
          }   
          if (!sys.abort) {  // Execute startup scripts after successful homing.
            sys.state = STATE_IDLE; // Set to IDLE when complete.
            st_go_idle(); // Set steppers to the idle state before returning.
          }
          break;

        case 'S' : // $SLP = Puts Grbl to sleep [IDLE/ALARM]
          if ((line[2] != 'L') || (line[3] != 'P') || (line[4] != 0)) { return(STATUS_INVALID_STATEMENT); }
          system_set_exec_state_flag(EXEC_SLEEP); // Set to execute sleep mode immediately
          break;

        case 'B' : // $B & $B= - stored line is all CAPS, no spaces (use '.' for space)  76 characters MAX!
          if( line[++char_counter] == 0 )  { //$B = Read Build Notes
            settings_read_manf_notes(line); //when finished, 'line' contains $B manufacturing notes
            report_manf_notes(line);
          } else  { //"$B=" Overwrite previous build notes with new notes.
            if(line[char_counter++] != '=') { return(STATUS_INVALID_STATEMENT); }
            helper_var = char_counter; // Set helper variable as counter to start of user info line.
            //write buildnotes to EEPROM
            do {
              line[char_counter-helper_var] = line[char_counter];
            } while (line[char_counter++] != 0);
            settings_store_manf_notes(line);
          }
          break;

        case 'I' : // $I = Print or store build info. [IDLE/ALARM]
          if ( line[++char_counter] == 0 ) { //$I
            settings_read_build_info(line);  //when finished, 'line' contains $I build info
            report_build_info(line);
          #ifdef ENABLE_BUILD_INFO_WRITE_COMMAND
            //this command will overwrite other EEPROM data if longer than 78 characters (length isn't checked).
            //82 characters available, but last four indicate EOL (x0AB0, where'xAB' is last two char in string).
            } else { // Store build info line [IDLE/ALARM]
              if(line[char_counter++] != '=') { return(STATUS_INVALID_STATEMENT); }
              helper_var = char_counter; // Set helper variable as counter to start of user info line.
              do {
                line[char_counter-helper_var] = line[char_counter];
              } while (line[char_counter++] != 0);
              settings_store_build_info(line);
          #endif
          }
          break;

        case 'R' : // $RST = Restore defaults [IDLE/ALARM]
          if ((line[2] != 'S') || (line[3] != 'T') || (line[4] != '=') || (line[6] != 0)) { return(STATUS_INVALID_STATEMENT); }
          switch (line[5]) {
            #ifdef ENABLE_RESTORE_EEPROM_DEFAULT_SETTINGS
              case '$': settings_restore(SETTINGS_RESTORE_DEFAULTS); break;
            #endif
            #ifdef ENABLE_RESTORE_EEPROM_CLEAR_PARAMETERS
              case '#': settings_restore(SETTINGS_RESTORE_PARAMETERS); break;
            #endif
            #ifdef ENABLE_RESTORE_EEPROM_WIPE_ALL
              case '*': settings_restore(SETTINGS_RESTORE_ALL); break;
            #endif
            default: return(STATUS_INVALID_STATEMENT);
          }
          report_feedback_message(MESSAGE_RESTORE_DEFAULTS);
          mc_reset(); // Force reset to ensure settings are initialized correctly.
          break;

        case 'N' : // $N = Startup lines. [IDLE/ALARM]
          if ( line[++char_counter] == 0 ) { // Print startup lines
            for (helper_var=0; helper_var < N_STARTUP_LINE; helper_var++) {
              if (!(settings_read_startup_line(helper_var, line))) {
                report_status_message(STATUS_SETTING_READ_FAIL);
              } else {
                report_startup_line(helper_var,line);
              }
            }
            break;
          } else { // Store startup line [IDLE Only] Prevents motion during ALARM.
            if (sys.state != STATE_IDLE) { return(STATUS_IDLE_ERROR); } // Store only when idle.
            helper_var = true;  // Set helper_var to flag storing method.
            // No break. Continues into default: to read remaining command characters.
          }

        default :  // $ or $N, followed by number.  Store settings [IDLE/ALARM]
          if(!read_float(line, &char_counter, &parameter)) { return(STATUS_BAD_NUMBER_FORMAT); } //remaining line must be a number
          if(line[char_counter++] != '=') { return(STATUS_INVALID_STATEMENT); }
          if (helper_var) { //$N, falling through form above = Store startup line
            // Prepare sending gcode block to gcode parser by shifting all characters
            helper_var = char_counter; // Set helper variable as counter to start of gcode block
            do {
              line[char_counter-helper_var] = line[char_counter];
            } while (line[char_counter++] != 0);
            // Execute gcode block to ensure block is valid.
            helper_var = gc_execute_line(line); // Set helper_var to returned status code.
            if (helper_var) { return(helper_var); }
            else {
              helper_var = trunc(parameter); // Set helper_var to int value of parameter
              settings_store_startup_line(helper_var,line);
            }
          } else { // Store global setting ($ followed by a number)  
            if(!read_float(line, &char_counter, &value)) { return(STATUS_BAD_NUMBER_FORMAT); }
            if((line[char_counter] != 0) || (parameter > 255)) { return(STATUS_INVALID_STATEMENT); }
            return(settings_store_global_setting((uint8_t)parameter, value));
          }
      }
  }
  return(STATUS_OK); // If '$' command makes it to here, then everything's ok.
}



void system_flag_wco_change()
{
  #ifdef FORCE_BUFFER_SYNC_DURING_WCO_CHANGE
    protocol_buffer_synchronize();
  #endif
  sys.report_wco_counter = 0;
}


// Returns machine position of axis 'idx'. Must be sent a 'step' array.
// NOTE: If motor steps and machine position are not in the same coordinate frame, this function
//   serves as a central place to compute the transformation.
float system_convert_axis_steps_to_mpos(int32_t *steps, uint8_t idx)
{
  float pos;
  pos = steps[idx]/settings.steps_per_mm[idx];
  return(pos);
}


void system_convert_array_steps_to_mpos(float *position, int32_t *steps)
{
  uint8_t idx;
  for (idx=0; idx<N_AXIS; idx++) {
    position[idx] = system_convert_axis_steps_to_mpos(steps, idx);
  }
  return;
}

// Checks and reports if target array exceeds machine travel limits.
uint8_t system_check_travel_limits(float *target)
{
  uint8_t idx;
  for (idx=0; idx<N_AXIS; idx++) {
    // NOTE: max_travel is stored as negative
    if (target[idx] > 0 || target[idx] < settings.max_travel[idx]) { return(true); }
  }
  return(false);
}


// Special handlers for setting and clearing Grbl's real-time execution flags.
void system_set_exec_state_flag(uint8_t mask) {
  uint8_t sreg = SREG;
  cli();
  sys_rt_exec_state |= (mask);
  SREG = sreg;
}

void system_clear_exec_state_flag(uint8_t mask) {
  uint8_t sreg = SREG;
  cli();
  sys_rt_exec_state &= ~(mask);
  SREG = sreg;
}

void system_set_exec_alarm(uint8_t code) {
  uint8_t sreg = SREG;
  cli();
  sys_rt_exec_alarm = code;
  SREG = sreg;
}

void system_clear_exec_alarm() {
  uint8_t sreg = SREG;
  cli();
  sys_rt_exec_alarm = 0;
  SREG = sreg;
}

void system_set_exec_motion_override_flag(uint8_t mask) {
  uint8_t sreg = SREG;
  cli();
  sys_rt_exec_motion_override |= (mask);
  SREG = sreg;
}

void system_set_exec_accessory_override_flag(uint8_t mask) {
  uint8_t sreg = SREG;
  cli();
  sys_rt_exec_accessory_override |= (mask);
  SREG = sreg;
}

void system_clear_exec_motion_overrides() {
  uint8_t sreg = SREG;
  cli();
  sys_rt_exec_motion_override = 0;
  SREG = sreg;
}

void system_clear_exec_accessory_overrides() {
  uint8_t sreg = SREG;
  cli();
  sys_rt_exec_accessory_override = 0;
  SREG = sreg;
}
