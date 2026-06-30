# Lux Aeterna space logo
# Multi-pass approximation using arcs for the ring strokes and orbit stroke.

run_file cnc_settings.cake

# Central orbit arc. Its endpoints are shared with the two outer arcs.
cnc_jog TargetX_m=0.046789 TargetY_m=0.151923 LinearSpeed_mps=0.030000 PumpOn=0
cnc_arc StartTheta_rad=-0.681804 EndTheta_rad=0.021804 Radius_m=0.167788 CenterX_m=0.152527 CenterY_m=0.021646 LinearSpeed_mps=0.045000

# Upper-left outer arc, top connector, and inner arc.
cnc_jog TargetX_m=0.046789 TargetY_m=0.151923 LinearSpeed_mps=0.030000 PumpOn=0
cnc_arc StartTheta_rad=-1.980000 EndTheta_rad=0.000000 Radius_m=0.058000 CenterX_m=0.100000 CenterY_m=0.175000 LinearSpeed_mps=0.045000

cnc_jog TargetX_m=0.100000 TargetY_m=0.215000 LinearSpeed_mps=0.045000 PumpOn=1

cnc_jog TargetX_m=0.1287 TargetY_m=0.203 LinearSpeed_mps=0.03000 PumpOn=0


cnc_arc StartTheta_rad=0.8000 EndTheta_rad=-1.500000 Radius_m=0.040000 CenterX_m=0.100000 CenterY_m=0.175000 LinearSpeed_mps=0.045000

# Lower-right outer arc, bottom connector, and inner arc.
cnc_jog TargetX_m=0.156185 TargetY_m=0.189394 LinearSpeed_mps=0.030000 PumpOn=0
cnc_arc StartTheta_rad=1.320000 EndTheta_rad=3.141593 Radius_m=0.058000 CenterX_m=0.100000 CenterY_m=0.175000 LinearSpeed_mps=0.045000
cnc_jog TargetX_m=0.100000 TargetY_m=0.135000 LinearSpeed_mps=0.045000 PumpOn=1
cnc_jog TargetX_m=0.068459 TargetY_m=0.150400 LinearSpeed_mps=0.030000 PumpOn=0
cnc_arc StartTheta_rad=4.050000 EndTheta_rad=1.800000 Radius_m=0.040000 CenterX_m=0.100000 CenterY_m=0.175000 LinearSpeed_mps=0.045000

wait timeout_ms=500
cnc_go_home
