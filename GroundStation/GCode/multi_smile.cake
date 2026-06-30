# Print multiple smiley pancakes from one run_file.
# Each included pattern uses cartesian coordinates relative to local_origin.

run_file cnc_settings.cake

# Clean the nozzle
pump_purge pumpSpeed_degps=500 duration_ms=1500

local_origin OriginX_m=0.13  OriginY_m=0.18
run_file smily_pattern.cake

local_origin OriginX_m=0.0 OriginY_m=0.27
run_file smily_pattern.cake

local_origin OriginX_m=-0.13 OriginY_m=0.18
run_file smily_pattern.cake

local_origin OriginX_m=0.0 OriginY_m=0.0

cnc_go_to_angle TargetS0_deg=0.0 TargetS1_deg=0.0

cnc_go_home
