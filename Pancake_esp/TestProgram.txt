# Smiley Face Program
# 1) Mouth (partial arc)
# Move to mouth start (rightmost point)
cnc_jog TargetX_m=0.15 TargetY_m=0.10 LinearSpeed_mps=0.03 PumpOn=0
# Draw lower half arc from rightmost (pi/2) to leftmost (3*pi/2)
cnc_arc StartTheta_rad=1.5708 EndTheta_rad=4.7124 Radius_m=0.05 LinearSpeed_mps=0.03 CenterX_m=0.10 CenterY_m=0.10
wait timeout_ms=500

# 2) Left Eye (full circle)
# Move to top of left eye
cnc_jog TargetX_m=0.075 TargetY_m=0.16 LinearSpeed_mps=0.03 PumpOn=0
# Draw full circle (0 to 2*pi)
cnc_arc StartTheta_rad=0.0 EndTheta_rad=6.2832 Radius_m=0.01 LinearSpeed_mps=0.03 CenterX_m=0.075 CenterY_m=0.15
wait timeout_ms=500

# 3) Right Eye (full circle)
# Move to top of right eye
cnc_jog TargetX_m=0.125 TargetY_m=0.16 LinearSpeed_mps=0.03 PumpOn=0
cnc_arc StartTheta_rad=0.0 EndTheta_rad=6.2832 Radius_m=0.01 LinearSpeed_mps=0.03 CenterX_m=0.125 CenterY_m=0.15
wait timeout_ms=500

# 4) Spiral over everything to finish
cnc_spiral SpiralConstant_mprad=0.001,SpiralRate_radps=4.0,CenterX_m=0.10,CenterY_m=0.12,MaxRadius_m=0.12
wait timeout_ms=2000
