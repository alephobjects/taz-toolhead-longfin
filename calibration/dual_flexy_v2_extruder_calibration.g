M999            ; clear errors
G21             ; set units to millimeters
G90             ; use absolute coordinates
M82             ; use absolute distances for extrusion
T1              ; use 2st extruder
M104 S225       ; get 2nd extruder tempening up
G92 X0 Y0 Z0 E0 ; Set cords to zero
T0              ; activate first extruder
G92 E0          ; set E to 0
M104 S240       ; set second extruder nozzle to 230C
M106 S255       ; turn on ze fanz
G4 S5           ; wait 5 sec
M107            ; turn off ze fans
T0              ; activate first extruder
T0              ; ""
M109 S240       ; set first extruder nozzle to 230C and wait for it to reach temp
M106 S0         ; turn off ze fanz
G1 E100 F90     ; move extruder 0 100mm 
M109 S240       ; wait before detemping
T1              ; change extruder
T1              ; ""
M109 S225       ; set second extruder nozzle to 230C and wait
G92 E0          ; Set the extruder position to 0
G1 E200 F20     ; move flexy 200mm 
G4 S1           ; wail a lil bit
M109 S225       ; wait for extrusion before detempening
M104 S0         ; Turn off 2nd extruder
T0              ; switch to 1st extruder
M104 S0 ;
M84
