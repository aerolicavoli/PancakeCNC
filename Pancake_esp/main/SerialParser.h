
#ifndef SERIAL_PARSER_H
#define SERIAL_PARSER_H

#include <stdint.h>
#include <string.h>

#define STX 0x02 // Start delimiter
#define ETX 0x03 // End delimiter
#define ESC 0x10 // Escape character

// OP codes
#define MSG_COMMAND 0x01
#define MSG_TELEMETRY 0x02
#define MSG_LOG 0x03

#define CNC_SPIRAL_OPCODE 0x11
#define CNC_JOG_OPCODE 0x12
#define CNC_WAIT_OPCODE 0x13
#define CNC_SINE_OPCODE 0x14
#define CNC_CONSTANT_SPEED_OPCODE 0x15
#define CNC_CONFIG_MOTOR_LIMITS_OPCODE 0x16
#define CNC_CONFIG_PUMP_CONSTANT_OPCODE 0x17
#define CNC_ARC_OPCODE 0x18
#define CNC_PUMP_PURGE_OPCODE 0x19
#define CNC_CONFIG_ACCEL_SCALE_OPCODE 0x1A
// Config opcodes
#define CNC_CONFIG_MOTOR_LIMITS_OPCODE 0x16
#define CNC_CONFIG_PUMP_CONSTANT_OPCODE 0x17
#define CNC_CONFIG_ACCEL_SCALE_OPCODE 0x1A
typedef struct
{
    uint8_t OpCode;
    uint8_t payloadLength;
    uint8_t payload[256];
} ParsedMessag_t;

bool ParseMessage(const uint8_t *data, size_t &ReadIndex, const size_t length,
                  ParsedMessag_t &message);

#endif // SERIAL_PARSER_H
