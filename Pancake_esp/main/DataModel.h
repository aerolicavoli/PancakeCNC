#ifndef DATA_MODEL_H
#define DATA_MODEL_H

#include <time.h>

#define CMD_PAYLOAD_MAX_LEN 256

typedef struct {
    time_t timestamp;
    char payload[CMD_PAYLOAD_MAX_LEN];
} raw_cmd_payload_t;

typedef struct {
    time_t timestamp;
    uint8_t opcode;
    uint8_t instructions[CMD_PAYLOAD_MAX_LEN];
    uint8_t instruction_length;
} decoded_cmd_payload_t;

#endif // DATA_MODEL_H
