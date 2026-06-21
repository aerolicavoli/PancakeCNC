# Lux Aeterna space logo centered at local origin
# Multi-pass approximation using arcs for the ring strokes and orbit stroke.

# Go to start point
cnc_jog TargetX_m=0.0 TargetY_m=-0.04 LinearSpeed_mps=0.06 PumpOn=0

# One big batter swoop.  Lower veritcal downward, outer circle counter clockwise, orbit swoop, then outer top circle clockwise, then upper line downward
pump_purge pumpSpeed_degps=500 duration_ms=200
cnc_jog TargetX_m=0 TargetY_m=-0.058 LinearSpeed_mps=0.07 PumpOn=1
cnc_arc EndTheta_rad=1.320000 StartTheta_rad=3.141593 Radius_m=0.058000 CenterX_m=0 CenterY_m=0 LinearSpeed_mps=0.07
cnc_arc EndTheta_rad=-0.681804 StartTheta_rad=0.021804 Radius_m=0.167788 CenterX_m=0.052527 CenterY_m=-0.153354 LinearSpeed_mps=0.07
cnc_arc StartTheta_rad=-1.980000 EndTheta_rad=0.000000 Radius_m=0.058000 CenterX_m=0 CenterY_m=0 LinearSpeed_mps=0.07
cnc_jog TargetX_m=0 TargetY_m=0.04 LinearSpeed_mps=0.07 PumpOn=1

# Jog to lower inner circle
cnc_jog TargetX_m=-0.031541 TargetY_m=-0.0246 LinearSpeed_mps=0.06 PumpOn=0

# Lower inner circle
#pump_purge pumpSpeed_degps=500 duration_ms=500
cnc_arc StartTheta_rad=4.050000 EndTheta_rad=1.800000 Radius_m=0.040000 CenterX_m=0 CenterY_m=0 LinearSpeed_mps=0.07

# Jog to upper inner cicle
cnc_jog TargetX_m=0.0287 TargetY_m=0.0278 LinearSpeed_mps=0.06 PumpOn=0

# Upper inner circle
#pump_purge pumpSpeed_degps=500 duration_ms=500
cnc_arc StartTheta_rad=0.8000 EndTheta_rad=-1.500000 Radius_m=0.040000 CenterX_m=0.0 CenterY_m=0.0 LinearSpeed_mps=0.07
pump_purge pumpSpeed_degps=-200 duration_ms=200
