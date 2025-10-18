# Pump speed test program 

# Start by asserting CNC settings
SetMotorLimits motor=S0 accel=10000.0 speed=50.0
SetMotorLimits motor=S1 accel=10000.0 speed=50.0
SetMotorLimits motor=Pump accel=800.0 speed=700.0
set_pump_constant pumpConstant_degpm=1500.0

set_accel_scale accelScale=0.03

ask_to_continue

# Jog Home
cnc_jog LinearSpeed_mps=0.04 TargetX_m=0.04 TargetY_m=0.02

ask_to_continue

# Depart home position
cnc_jog LinearSpeed_mps=0.04 TargetX_m=0.04 TargetY_m=0.2

# Run the pump
#pump_purge duration_ms=1500 pumpSpeed_degps=300

ask_to_continue

# Go to top left corner
cnc_jog LinearSpeed_mps=0.04 TargetX_m=-0.18 TargetY_m=0.22 PumpOn=0

ask_to_continue

# Jog right
cnc_jog LinearSpeed_mps=0.04 TargetX_m=0.18 TargetY_m=0.22 PumpOn=1

# Step down
cnc_jog LinearSpeed_mps=0.04 TargetX_m=0.18 TargetY_m=0.2 PumpOn=0

# Job left
cnc_jog LinearSpeed_mps=0.04 TargetX_m=-0.18 TargetY_m=0.2 PumpOn=1


# Step down
cnc_jog LinearSpeed_mps=0.04 TargetX_m=-0.18 TargetY_m=0.18 PumpOn=0


# Jog right
cnc_jog LinearSpeed_mps=0.04 TargetX_m=0.18 TargetY_m=0.18 PumpOn=1


# Step down
cnc_jog LinearSpeed_mps=0.04 TargetX_m=0.18 TargetY_m=0.16 PumpOn=0

# Job left
cnc_jog LinearSpeed_mps=0.04 TargetX_m=-0.18 TargetY_m=0.16 PumpOn=1
