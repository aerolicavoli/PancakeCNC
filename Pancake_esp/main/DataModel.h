#ifndef DATA_MODEL_H
#define DATA_MODEL_H

#include <stdint.h>
#include <time.h>

#define CMD_PAYLOAD_MAX_LEN 256
#define CMD_INSTRUCTION_PAYLOAD_MAX_LEN (CMD_PAYLOAD_MAX_LEN - 2)

typedef struct {
    int64_t timestamp_ms;
    char payload[CMD_PAYLOAD_MAX_LEN];
} raw_cmd_payload_t;

typedef struct {
    int64_t timestamp_ms;
    uint8_t opcode;
    uint8_t instructions[CMD_PAYLOAD_MAX_LEN];
    uint8_t instruction_length;
} decoded_cmd_payload_t;

#endif // DATA_MODEL_H
