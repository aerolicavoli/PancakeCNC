#include "MotorControl.h"

#define MOTOR_CONTROL_PERIOD_MS 10
//QueueHandle_t CNCCommandQueue;
//QueueHandle_t CNCPathQueue;

// Create motor instances  /PULSE DIR
//static StepperMotor S0Motor(S0_MOTOR_PULSE, S0_MOTOR_DIR, 2000, "S0MOTOR");
//static StepperMotor S1Motor(S1_MOTOR_PULSE, S1_MOTOR_DIR, 2000, "S1MOTOR");
static StepperMotor PumpMotor(PUMP_MOTOR_PULSE, PUMP_MOTOR_DIR, 200.0, 0.9/4.0, "PUMPMOTOR");

void MotorControlInit()
{

    gpio_reset_pin(PUMP_MOTOR_PULSE);
    gpio_set_direction(PUMP_MOTOR_PULSE, GPIO_MODE_OUTPUT);

    gpio_reset_pin(PUMP_MOTOR_DIR);
    gpio_set_direction(PUMP_MOTOR_DIR, GPIO_MODE_OUTPUT);

    // Initialize timers for each motor
  //  S0Motor.InitializeTimers(MOTOR_CONTROL_PERIOD_MS);
 //   S1Motor.InitializeTimers(MOTOR_CONTROL_PERIOD_MS);
    PumpMotor.InitializeTimers(MOTOR_CONTROL_PERIOD_MS);


      
    //CNCCommandQueue = xQueueCreate(10, sizeof(uint32_t));

}

void MotorControlStart()
{
    xTaskCreate(MotorControlTask,
                 "MotorControl",
                 4096,
                 NULL,
                 1,
                 NULL);
}

void MotorControlTask( void *Parameters )
{
    // 100hz motor control loop
    unsigned int motorUpdatePeriod_Ticks = pdMS_TO_TICKS(MOTOR_CONTROL_PERIOD_MS);

    // 1hz motor loging
    unsigned int reportPeriod_frames = 1000;

    unsigned int frameNum = 0;
   // CNCMode currentMode = E_STOPPED;
    
    for( ;; )
    {
        // Process commands
        /*
        uint32_t buf;
        while (xQueueReceive(CNCCommandQueue, &buf, 0) == pdPASS)
        {
            switch buf
            {
                case E_STOPPED:
                    currentMode = E_STOPPED;
                    break;
                case E_CARTEASIANQUEUE

            }

        }
        xQueueReceive(queue1, &buf, 0);
        */

        // Update targets

        // Solve kinematics

        // Command Speed
   //     S0Motor.setTargetSpeed(100); // Hz
   //     S1Motor.setTargetSpeed(200); // Hz
        PumpMotor.setTargetSpeed(3600.0); // Hz

        // Process speed updates
    //    S0Motor.UpdateSpeed();
    //    S1Motor.UpdateSpeed();
    
        PumpMotor.UpdateSpeed();
        
        if ((frameNum % reportPeriod_frames) == 0)
        {
            PumpMotor.logStatus();
        }

        vTaskDelay(motorUpdatePeriod_Ticks);
        frameNum++;
    }
}