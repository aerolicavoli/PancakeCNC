#pragma once

#include <cstdint>

constexpr uint8_t CNC_SPIRAL_OPCODE = 0x11;
constexpr uint8_t CNC_JOG_OPCODE = 0x12;
constexpr uint8_t CNC_WAIT_OPCODE = 0x13;
constexpr uint8_t CNC_SINE_OPCODE = 0x14;
constexpr uint8_t CNC_CONSTANT_SPEED_OPCODE = 0x15;
constexpr uint8_t CNC_CONFIG_MOTOR_LIMITS_OPCODE = 0x16;
constexpr uint8_t CNC_CONFIG_PUMP_CONSTANT_OPCODE = 0x17;
constexpr uint8_t CNC_ARC_OPCODE = 0x18;
constexpr uint8_t CNC_PUMP_PURGE_OPCODE = 0x19;
constexpr uint8_t CNC_CONFIG_ACCEL_SCALE_OPCODE = 0x1A;
