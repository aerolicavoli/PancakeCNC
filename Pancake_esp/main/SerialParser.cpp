#include "SerialParser.h"

bool ParseMessage(const uint8_t *data, size_t &ReadIndex, const size_t length,
                  ParsedMessag_t &message)
{
    /*
    if (length < 5)
    {
        // Minimum message size not met
        return false;
    }

    if (data[ReadIndex++] != STX)
    {
        // Start delimiter not found
        return false;
    }
    */

    message.OpCode = data[ReadIndex++];
    message.payloadLength = data[ReadIndex++];

    /*
    if (message.payloadLength > (length - 5))
    {
        // Payload length mismatch
        return false;
    }
        */

    memcpy(message.payload, &data[ReadIndex], message.payloadLength);
    ReadIndex += message.payloadLength;

    /*
    // Check checksum
    uint8_t checksum = 0;
    for (int i = 0; i < message->payloadLength; i++)
    {
        checksum ^= message->payload[i];
    }


    if (checksum != data[ReadIndex++])
    {
        // Checksum mismatch
        return false;
    }

    if (data[ReadIndex++] != ETX)
    {
        // End delimiter not found
        return false;
    }
    */
    return true;
}