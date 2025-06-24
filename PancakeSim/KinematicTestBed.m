%% Conversions from sily units
clear all
INToM = 0.0254;
OZFINToNm = 1.390 * INToM;
LBMTokg = 0.453592;
aluminumDensity_kgpm3 = 2.7 * 100^3 / 1000;

%% Print Area
% 11x20in griddle
% 
global stage0Length_m stage1Length_m BaseOffset_m
PrintHeight_m = 18 * INToM; %0.558;
PrintWidth_m = 9 * INToM; %0.457;
BaseOffset_m = 0.04;

ArmReach_m = sqrt((PrintHeight_m/2)^2+(PrintWidth_m+BaseOffset_m)^2);
stage0Length_m = (ArmReach_m+BaseOffset_m)/2;
stage1Length_m = (ArmReach_m-BaseOffset_m)/2;

%% Drive Specs
% S1 Steper 
% https://www.mcmaster.com/4798n12/
motorTeeth = 10; %https://www.servocity.com/6mm-10-tooth-pinion-pulley/
hubTeeth = 24; % https://www.mcmaster.com/1277N51/
s1GearRatio = motorTeeth/hubTeeth;
s1Torque_Nm = 30 * OZFINToNm / s1GearRatio; % At low speed
s1StepSize_rd = s1Torque_Nm * 0.9 * pi/180;
s1StepMass_kg = LBMTokg * 0.7;

% S0 Stepper
%https://www.mcmaster.com/4798N13/

hubTeeth = 108; % https://www.servocity.com/2302-series-aluminum-mod-0-8-hub-mount-gear-14mm-bore-108-tooth/
motorTeeth = 16; % https://www.mcmaster.com/2664N334/
s0GearRatio = motorTeeth/hubTeeth;
s0StepSize_rd = s0GearRatio * 0.9 * pi/180;
s0Torque_Nm = 30 * OZFINToNm / s0GearRatio; % At low speed
s0StepMass_kg = LBMTokg * 0.7;



%% Dynamic estimates
s0MaxAccel_rdps2 = 0.1;
s1MaxAccel_rdps2 = 0.1;

close all

figure; grid on; hold on;
rectangle('Position',[-PrintHeight_m/2,BaseOffset_m,PrintHeight_m,PrintWidth_m])

areaH = patch([0 0 0],[0 0 0],'r','facealpha',0.2,'edgealpha',0);
Stg0lineH = plot(nan,'-k','LineWidth',2);
Stg1lineH = plot(nan,'-k','LineWidth',2);
pathH = plot(nan,nan,'color',[0.5 0.5 0.5],'LineWidth',0.5);

xlim([-ArmReach_m, ArmReach_m])
ylim([-ArmReach_m*0.5, ArmReach_m])
%% Dynamic sim
target_GRD_m = [-0.2 0.2];
[stage0Pos_rd, stage1Pos_rd] = CartToAng(target_GRD_m);
stage0Rate_rdps = 0.1;
stage1Rate_rdps = 0.2;


dt_s = 0.00002;
time_s = 0;
cycles = 10;
maxTime_s = 10;
timeUntilNextFrame_s = 0;

   [stage0Pos_GRD_m, stage1Pos_GRD_m] = AngToCart(stage0Pos_rd, stage1Pos_rd);
target_GRD_m = [0.2 0.2];
targetIdx = 0;
theta_rd = 0;

pancakeCenters_GRD_m = [0.1 0.2; 0.0 0.2; -0.1 0.2; -0.1 0.1; 0.0 0.1; 0.1 0.1];
pancakeIdx = 1;

while time_s < maxTime_s
    
    time_s = time_s + dt_s;
    timeUntilNextFrame_s = timeUntilNextFrame_s - dt_s;
    
    usablePrintHeight = 0.9*PrintHeight_m;
    usablePrintWidth = 0.9*PrintWidth_m;
    
    
    
    theta_rd = theta_rd + 0.001;
    
    if theta_rd > 2*pi*10
        pancakeIdx = pancakeIdx + 1;
        theta_rd = 0;
    end
   
    target_GRD_m = pancakeCenters_GRD_m(pancakeIdx,:) + [sin(theta_rd) cos(theta_rd)]*theta_rd * 0.0007;

%     % Rectangle tracing
%     if true %targetIdx == 0.0 || norm(stage1Pos_GRD_m - target_GRD_m) < 0.01
%         targetIdx = targetIdx + 0.0001;
%         targets = [-0.5*usablePrintHeight,0.5*usablePrintHeight, 0.5*usablePrintHeight, -0.5*usablePrintHeight,  -0.5*usablePrintHeight;
%                    usablePrintWidth+ BaseOffset_m + 0.05*PrintWidth_m, usablePrintWidth+ BaseOffset_m+ 0.05*PrintWidth_m, BaseOffset_m+ 0.05*PrintWidth_m, BaseOffset_m+ 0.05*PrintWidth_m, usablePrintWidth+ BaseOffset_m]';
%         
%         target_GRD_m = interp1([1;2;3;4;5],targets,mod(targetIdx,4)+1);
%     end
    
    [tgtStage0Pos_rd, tgtStage1Pos_rd, ~, ~, ~] = CartToAng(target_GRD_m);
        
    s1AngError = tgtStage1Pos_rd - stage1Pos_rd;
    if abs(s1AngError) > pi
        s1AngError = -s1AngError + sign(s1AngError) * pi
    end
    stage0Rate_rdps = (tgtStage0Pos_rd - stage0Pos_rd)*1000;
    stage1Rate_rdps = (tgtStage1Pos_rd - stage1Pos_rd)*1000;
    
    ratelimit_rdps = 40;
    stage0Rate_rdps = sign(stage0Rate_rdps) * min(ratelimit_rdps, abs(stage0Rate_rdps));
    stage1Rate_rdps = sign(stage1Rate_rdps) * min(ratelimit_rdps, abs(stage1Rate_rdps));
    
    % Limit the rates
    
%     Stage1Vel_GRD_mps = [ 0.1 0.0];
%    [stage0Rate_rdps, stage1Rate_rdps] = CartVelToAngVel(Stage1Vel_GRD_mps, stage0Pos_rd, stage1Pos_rd);
   
   stage0Pos_rd = stage0Pos_rd + dt_s * stage0Rate_rdps;
   stage1Pos_rd = stage1Pos_rd + dt_s * stage1Rate_rdps;
   
   [stage0Pos_GRD_m, stage1Pos_GRD_m] = AngToCart(stage0Pos_rd, stage1Pos_rd);

   
   if timeUntilNextFrame_s < 0

        set(Stg0lineH,"XData",[0 stage0Pos_GRD_m(1,1)])
        set(Stg0lineH,"YData",[0 stage0Pos_GRD_m(1,2)])
        set(Stg1lineH,"XData",[stage0Pos_GRD_m(1,1) stage1Pos_GRD_m(1,1)])
        set(Stg1lineH,"YData",[stage0Pos_GRD_m(1,2) stage1Pos_GRD_m(1,2)])
      
        set(areaH,"XData",[0 stage0Pos_GRD_m(1,1) stage1Pos_GRD_m(1,1)])
        set(areaH,"YData",[0 stage0Pos_GRD_m(1,2) stage1Pos_GRD_m(1,2)])
        
        set(pathH,"XData",[get(pathH,"XData") stage1Pos_GRD_m(1,1)])
        set(pathH,"YData",[get(pathH,"YData") stage1Pos_GRD_m(1,2)])
        
        
        drawnow
        timeUntilNextFrame_s = 1/100;
        %pause(timeUntilNextFrame_s)
   end
   
   
end

function [Stage0Vel_rdps, Stage1Vel_rdps] = CartVelToAngVel(Stage1Vel_GRD_mps, Stage0Pos_rd, Stage1Pos_rd)
    global stage0Length_m stage1Length_m

    [~, stage1Pos_GRD_m] = AngToCart(Stage0Pos_rd, Stage1Pos_rd);

    [~, ~, hypAng_rd, innerAng_rd, targetHyp_m] = CartToAng(stage1Pos_GRD_m);
    
    Stage0Vel_rdps = (stage1Pos_GRD_m(2) * Stage1Vel_GRD_mps(1) - stage1Pos_GRD_m(1) * Stage1Vel_GRD_mps(2)) / targetHyp_m^2 - ...
        (stage1Pos_GRD_m(1) * Stage1Vel_GRD_mps(1)+ stage1Pos_GRD_m(2) * Stage1Vel_GRD_mps(2))/(sin(innerAng_rd)*2*stage0Length_m*targetHyp_m);
    
    Stage1Vel_rdps = -(stage1Pos_GRD_m(1) * Stage1Vel_GRD_mps(1)+ stage1Pos_GRD_m(2) * Stage1Vel_GRD_mps(2))/(sin(hypAng_rd)*stage0Length_m * stage1Length_m);
%     denom = stage0Length_m * (tan(phi_rd)*cos(Stage0Pos_rd)-sin(Stage0Pos_rd));
%     
%     if abs(denom) > 1e-4 && abs(cos(phi_rd)) > 1e-4
%         Stage0Vel_rdps = (Stage1Vel_GRD_mps(2) + tan(phi_rd)*Stage1Vel_GRD_mps(1))/denom;
%         phidot_rdps = (Stage1Vel_GRD_mps(1) - stage0Length_m * Stage0Vel_rdps) / (stage1Length_m * cos(phi_rd));
%        Stage1Vel_rdps = phidot_rdps - Stage0Vel_rdps;
%  
%     else
%         Stage0Vel_rdps = 0;
%         Stage1Vel_rdps = 0;
%         error('stop')
%     end
    
end

function [Stage0Pos_rd, Stage1Pos_rd, hypAng_rd, innerAng_rd, targetHyp_m] = CartToAng(stage1Pos_GRD_m)

global stage0Length_m stage1Length_m
C_S0L2_PLUS_S1L2_m = stage0Length_m^2+stage1Length_m^2;
C_S0L2_MINUS_S1L2_m = stage0Length_m^2-stage1Length_m^2;
C_Inv_2_TIMES_S0L_TIMES_S1L_1pm2 = 1.0 / (2.0 * stage0Length_m*stage1Length_m);

targetHypSqrd_m2 = dot(stage1Pos_GRD_m,stage1Pos_GRD_m);
targetHyp_m = sqrt(targetHypSqrd_m2);
targetAng_rd = atan(stage1Pos_GRD_m(1)/stage1Pos_GRD_m(2));
hypAng_rd = acos((C_S0L2_PLUS_S1L2_m-targetHypSqrd_m2) * C_Inv_2_TIMES_S0L_TIMES_S1L_1pm2);
innerAng_rd = acos((C_S0L2_MINUS_S1L2_m+targetHypSqrd_m2)/(2*stage0Length_m*targetHyp_m));

% TODO be smart about selecting which of the two solutions to use.
direction = -1; %-sign(targetAng_rd);
Stage0Pos_rd = targetAng_rd + direction * innerAng_rd;
Stage1Pos_rd = -direction * (pi - hypAng_rd);

end


function [stage0Pos_GRD_m, stage1Pos_GRD_m] = AngToCart(Stage0Pos_rd, Stage1Pos_rd)
global stage0Length_m stage1Length_m

stage0Pos_GRD_m = [sin(Stage0Pos_rd), cos(Stage0Pos_rd)] * stage0Length_m;
stage1PosRelST0_GRD_m = [sin(Stage0Pos_rd + Stage1Pos_rd), cos(Stage0Pos_rd + Stage1Pos_rd)] * stage1Length_m;

stage1Pos_GRD_m = stage0Pos_GRD_m + stage1PosRelST0_GRD_m;

end

