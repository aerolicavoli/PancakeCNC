# Pump speed test program 

# Start by asserting CNC settings
run_file cnc_settings.cake

# Draw pancake perimeter
cnc_jog LinearSpeed_mps=0.03 TargetX_m=0.1 TargetY_m=0.225 PumpOn=0
cnc_arc StartTheta_rad=0 EndTheta_rad=6.28 Radius_m=0.05 CenterX_m=0.1 CenterY_m=0.175 LinearSpeed_mps=0.1
wait timeout_ms=2000

# Jog to the mouth Start
cnc_jog LinearSpeed_mps=0.03 TargetX_m=0.125 TargetY_m=0.175 PumpOn=0


# mouth
cnc_arc StartTheta_rad=1.9 EndTheta_rad=4.6 Radius_m=0.025 CenterX_m=0.1 CenterY_m=0.175 LinearSpeed_mps=0.1
wait timeout_ms=2000

# Jog to eye
cnc_jog LinearSpeed_mps=0.03 TargetX_m=0.085 TargetY_m=0.21 PumpOn=0
cnc_arc StartTheta_rad=0 EndTheta_rad=6.28 Radius_m=0.01 CenterX_m=0.085 CenterY_m=0.22 LinearSpeed_mps=0.05
wait timeout_ms=2000

cnc_jog LinearSpeed_mps=0.03 TargetX_m=0.115 TargetY_m=0.21 PumpOn=0
cnc_arc StartTheta_rad=0 EndTheta_rad=6.28 Radius_m=0.01 CenterX_m=0.115 CenterY_m=0.22 LinearSpeed_mps=0.05

cnc_go_home
wait timeout_ms=2000

ask_to_continue

# Fill in the pancake
cnc_spiral CenterY_m=0.175 CenterX_m=0.1 MaxRadius_m=0.05 SpiralConstant_mprad=0.001 SpiralRate_radps=10.0 LinearSpeed_mps=0.5
cnc_go_home
