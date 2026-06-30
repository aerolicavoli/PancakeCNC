# Smiley pattern centered at local origin 

# Draw pancake perimeter
cnc_jog LinearSpeed_mps=0.06 TargetX_m=0 TargetY_m=0.06 PumpOn=0
pump_purge pumpSpeed_degps=500 duration_ms=200
cnc_arc StartTheta_rad=0 EndTheta_rad=6.28 Radius_m=0.06 CenterX_m=0 CenterY_m=0 LinearSpeed_mps=0.07
wait timeout_ms=1000

# Jog to the mouth Start
cnc_jog LinearSpeed_mps=0.06 TargetX_m=0.026 TargetY_m=-0.023 PumpOn=0

# mouth
pump_purge pumpSpeed_degps=500 duration_ms=300
cnc_arc StartTheta_rad=2.3 EndTheta_rad=4.3 Radius_m=0.035 CenterX_m=0 CenterY_m=0.0 LinearSpeed_mps=0.07
pump_purge pumpSpeed_degps=-200 duration_ms=200

# Jog to eye
cnc_jog LinearSpeed_mps=0.06 TargetX_m=-0.025 TargetY_m=0.035 PumpOn=0
pump_purge pumpSpeed_degps=500 duration_ms=200
wait timeout_ms=1000

cnc_jog LinearSpeed_mps=0.06 TargetX_m=0.025 TargetY_m=0.035 PumpOn=0
pump_purge pumpSpeed_degps=500 duration_ms=200
wait timeout_ms=1000

pump_purge pumpSpeed_degps=-200 duration_ms=200
