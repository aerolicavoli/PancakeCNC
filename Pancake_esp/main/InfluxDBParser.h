#ifndef INFLUXDB_PARSER_H
#define INFLUXDB_PARSER_H

#include <string>
#include <ctime>

// Simple structure to hold a command extracted from InfluxDB.
// The payload string remains base64 encoded as received from the CSV
// response so that the caller can decide how and when to decode it.
struct InfluxDBCommand {
    time_t timestamp;      // UTC timestamp of the command
    std::string payload;   // Base64 encoded payload
};

// Return the last non-empty line from an InfluxDB CSV response.
// Lines containing only whitespace are ignored. If no such line exists,
// an empty string is returned.
std::string get_last_non_empty_line(const std::string &body);

// Parse an InfluxDB annotated CSV response and extract the timestamp and
// payload from the last non-empty data line. Returns true on success and
// populates the provided InfluxDBCommand structure.
bool parse_influxdb_command(const std::string &body, InfluxDBCommand &cmd);

#endif // INFLUXDB_PARSER_H
