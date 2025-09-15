#ifndef INFLUXDB_PARSER_H
#define INFLUXDB_PARSER_H

#include <string>
#include <vector>
#include <ctime>

// Simple structure to hold a command extracted from InfluxDB.
// The payload string remains base64 encoded as received from the CSV
// response so that the caller can decide how and when to decode it.
struct InfluxDBCommand {
    time_t timestamp;      // UTC timestamp of the command
    std::string payload;   // Base64 encoded payload
    std::string hash;      // Command hash tag
};

// Return the last non-empty line from an InfluxDB CSV response.
std::string get_last_non_empty_line(const std::string &body);

// Parse only the last command (legacy helper)
bool parse_influxdb_command(const std::string &body, InfluxDBCommand &cmd);

// Parse all commands contained in the CSV body. Appends to 'out'.
// Returns the number of commands parsed.
size_t parse_influxdb_command_list(const std::string &body, std::vector<InfluxDBCommand> &out);

#endif // INFLUXDB_PARSER_H
