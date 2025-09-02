#ifndef INFLUXDB_PARSER_H
#define INFLUXDB_PARSER_H

#include <string>

// Return the last non-empty line from an InfluxDB CSV response.
// Lines containing only whitespace are ignored. If no such line exists,
// an empty string is returned.
std::string get_last_non_empty_line(const std::string &body);

#endif // INFLUXDB_PARSER_H
