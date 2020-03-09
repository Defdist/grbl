


GrblDD, for Ghost Gunner

A standards-compliant g-code controller that will look up to 16 motions into the future to deliver smooth acceleration and jerk-free cornering.

* [Licensing](https://github.com/gnea/grbl/wiki/Licensing): GrblDD is free software, released under the GPLv3 license.

* Lead bitbanger: JTS aka @doppelhub

* Built on the wonderful Grbl v1.1c (2019) firmware written by Sungeun "Sonny" Jeon, Ph.D. (USA) aka @chamnit

* Built on the wonderful Grbl v0.6 (2011) firmware written by Simen Svale Skogsrud (Norway).

***
## How to upload source code to Arduino  
  -1: Download and install Arduino IDE  
  -2: Clone/download this repository  
  -3: Place folder "grblDD" into Arduino libraries folder (on a Mac, that's ~/Documents/Arduino/libraries)**  
  -4: Launch Arduino, then go to File>Examples... all the way at the bottom you'll see grblDD>grblUpload  
  -5: Select COM port (Tools>Port)  
  -6: Click "Upload" button (looks like an arrow pointing to the right)

  **Optional: On a Mac, to prevent having to manually perform this step each time grblDD is updated, create a symbolic link (using Terminal):  
  ln -s /path/to/original /path/to/symbolic/link  
  where:  
  -"/path/to/original" is wherever your git directory cloned to  
  -"/path/to/symbolic/link" is the Arduino libraries folder (~/Documents/Arduino/libraries)  
  Example:  
  ln -s /Users/jts/Documents/GitHub/grbl/grblDD /Users/jts/Documents/Arduino/libraries  

## Update Summary for grblDD 3v0
Items listed below are changes to standard grbl 1v1h:

- **IMPORTANT:** After updating firmware, You must update EEPROM using "$RST=*".  
--  For our prototyping needs, we can just manually send that command.  
--  For release, DDcut should probably just always send that command after updating grbl firmware (if we don't do that already).  


-$HX, $HY, or $HZ homes single axis ($H still homes all axes).  This should make testing limit switch tab placement easier.  

-'$E' now reports entire EEPROM (mainly for debug)  

-'$L' = autolevel X table using previously stored calibration data.  Uses calibration data stored by '$LS'  
-'$LS' = store difference between X limit switches in EEPROM.  Run $LS after manually squaring machine.  

-'$B=' stores RMA/manufacturing notes in memory.  Write whatever you want here.  Each successive call overwrites entire previous note.  
--76 characters max (grbl reports error if longer).  Only numbers, letters, and period are stored (all other symbols removed, including spaces).  
-'$B' reports any manufacturing/rma notes previously stored.  All letters CAPITALIZED.
Example: "$B=Hello.World.this text has spaces".
Example: "$B" returns: "HELLO.WORLD.THISTEXTHASSPACES"

-Added (background) spindle safety interlock; BLDC HallC state intentionally pulled low when M5 disables spindle.  

-Probing should work better than you've ever seen it work.  Of course, y'all can't test it until I send 64M1 firmware (spindle).  

-Steppers now automatically configure power levels.  You'll notice steppers run much cooler.  

-'M17' command: places steppers into high power mode (ONLY during the next motion command).  Primarily designed to free stuck axes (particularly X).    
USE SPARINGLY... PCB overheats (and safely shuts down) in ~5 seconds.  Intended use: freeing bound X axis.  

-'M18' command: turn steppers off (until next motion command).  Allows you to hand turn motors without unplugging GG.  Useful for assembly/troubleshooting.  
Note: This replaces '$O' functionality previously described.

-When GG hits a hard limit switch, the response is now:  
--"[MSG:LIM TRIP X]" or "[MSG:LIM TRIP Y]" or "[MSG:LIM TRIP Z]"  
--"ALARM:1"  
This should make it easier for our customers to figure out which limit switch has tripped.  

If a particular g-code line contains a formatting error, grbl now echoes the entire line, then reports the error number:    
--Example (sending "g0 X-J"):  
--New grbl response:  
---[echo: G0X-J]  
---error:2  
--(Note: the echoed line is formatted (all capital letters, no spaces, no comments)  

If the g-code contains line numbers (e.g. "N123 G0 X-1"), grbl will now return the line currently executing when '?' is received.  
If the g-code does not contain a line number (e.g. G0 X-1"), grbl returns '0'.  
If no line is currently executing (e.g. grbl is IDLE), grbl returns '0'.  
--Example (Send "N122 G1 Y-20 F100".  While that line is running, send '?'):.  
--New grbl response:  
---<Run|M:-91.500,-24.440,-0.500|B:13,128|L:122|0000>  

-Simplified realtime reporting (i.e. data returned by '?' command).  grbl1v1 made it difficult to automatically parse data.  
The following improvements were made:  
--every single '?' is now always formatted exactly the same way.  Previously, certain commands were only returned every 10 or 20 requests.  
--feedrate and spindle rpm are no longer returned here (they're already available via '$G' parser state).  
--When a limit switch or probe isn't tripped, the corresponding digit is now filled with '0'.  Previously, non-tripped parameters were not sent at all.  
Returned data from '?' is formatted as follows:  
<state | M:X,Y,Z(absolute WCS) | B:(free blocks in planner buffer),(free bytes in serial RX buffer) | L:nnnnnnn | PXYZ (letter if tripped, else '0') | W:X,Y,Z(work offsets) >  
//Note: WCO only appears every once in a while, and whenever it updates.  
Examples:  
<Idle | M:-91.500,-20.000,-0.500 | B:15,128 | L:0 | 0000> (idle | WCS | Buffers completely empty | nothing is tripped (probe/X/Y/Z all equal 0)  
<Idle | M:-91.500,-20.000,-0.500 | B:15,128 | L:0 | P000> (same as above, except probe is tripped)  
<Hold:0 | M:-91.500,-20.000,-0.500 | Bf:15,1 | L:0 | P000> (same as above, except machine is in feedhold & RX buffer is full)  
<Alarm | M:0.000,0.000,0.000 | B:15,128 | L:0 | 0X00> (machine hasn't been homed, X limit switch is tripped**)  
<Alarm | M:0.000,0.000,0.000 | B:15,128 | L:0 | 00Y0> (machine hasn't been homed, Y limit switch is tripped**)  
<Alarm | M:0.000,0.000,0.000 | B:15,128 | L:0 | 000Z> (machine hasn't been homed, Z limit switch is tripped**)  
<Alarm | M:0.000,0.000,0.000 | B:15,128 | L:0 | 0XY0> (machine hasn't been homed, X & Y limit switches are tripped**)  
**Note: you cannot query '?' after a hard limit occurs.  Thus, you can only see which limit switch is tripped prior to initial homing sequence.  

$I returns GG hardware/firmware versions (e.g. "[grbl:1.1h GG:3A PCB:3B VFD:3A YMD:20200101]"), where:  
  -"GG:3A" is a GG3 mechanical assembly, with revision A hardware.  
  This number should always be the actual machine hardware (i.e. a GG1 unit will always return itself as GG:1_)  
  -"PCB:3B" is a GG1/2/3 machine with a GG3 PCB inside (revision B).  
  -"VFD:3A" is a GG1/2/3 machine with VFD firmware 3A... note machines that don't have VFD hardware will return "VFD:00".  
  -"YMD20200101" is the build year/month/date (YYYYMMDD).  
  -Keep in mind that all existing GG1/GG2 machines will return the old $I data until their firmware gets upgraded.  

```
List of Supported G-Codes:
  - Non-Modal Commands: G4, G10L2, G10L20, G28, G30, G28.1, G30.1, G53, G92, G92.1
  - Motion Modes: G0, G1, G2, G3, G38.2, G38.3, G38.4, G38.5, G80
  - Feed Rate Modes: G93, G94
  - Unit Modes: G20, G21
  - Distance Modes: G90, G91
  - Arc IJK Distance Modes: G91.1
  - Plane Select Modes: G17, G18, G19
  - Tool Length Offset Modes: G43.1, G49
  - Cutter Compensation Modes: G40
  - Coordinate System Modes: G54, G55, G56, G57, G58, G59
  - Control Modes: G61
  - Program Flow: M0, M1, M2, M30*
  - Spindle Control: M3, M4, M5
  - (DDcut only) Find midpoint between two WCS (see manual): M100
  - (DDcut only) Verify absolute distance is less than x (see manual): M101
  - Stepper High power mode (next motion command only): M17
  - Stepper Zero power mode (turn steppers off until next motion command): M18
  - Valid Non-Command Words: F, I, J, K, L, N, P, R, S, T, X, Y, Z
  - '|' (shift+\) now resets grbl (in addition to control+x, which is not a keyboard character).
```

