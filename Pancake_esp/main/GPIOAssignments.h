// GPIOAssignments.h
#ifndef GPIOASSIGNMENTS_H
#define GPIOASSIGNMENTS_H

#include "driver/gpio.h"

#define ALIVE_LED GPIO_NUM_17

// MOTOR CONTROL
#define S0_MOTOR_PULSE GPIO_NUM_7
#define S0_MOTOR_DIR GPIO_NUM_8

#define S1_MOTOR_PULSE GPIO_NUM_9
#define S1_MOTOR_DIR GPIO_NUM_10
#define S0S1_MOTOR_ENABLE GPIO_NUM_14

#define PUMP_MOTOR_PULSE GPIO_NUM_4
#define PUMP_MOTOR_DIR GPIO_NUM_5
#define PUMP_MOTOR_ENABLE GPIO_NUM_6

// Define UART configuration
#define UART_NUM UART_NUM_2
#define UART_TX_PIN 11
#define UART_RX_PIN 13


#endif // GPIOAssignments