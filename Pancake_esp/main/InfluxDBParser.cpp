#include "InfluxDBParser.h"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <vector>

// Helper to parse ISO8601 timestamps of the form
// "YYYY-MM-DDTHH:MM:SS.mmmZ" or without fractional seconds.
static bool parse_iso8601(const std::string &token, time_t &out) {
    std::string ts = token;
    if (!ts.empty() && ts.back() == 'Z') {
        ts.pop_back();
    }
    size_t dot = ts.find('.');
    if (dot != std::string::npos) {
        ts = ts.substr(0, dot);
    }
    struct tm tm = {};
    if (!strptime(ts.c_str(), "%Y-%m-%dT%H:%M:%S", &tm)) {
        return false;
    }
#if defined(_BSD_SOURCE) || defined(_GNU_SOURCE) || defined(__APPLE__)
    out = timegm(&tm);
#else
    out = mktime(&tm); // Fall back to local time if timegm is unavailable
#endif
    return true;
}

std::string get_last_non_empty_line(const std::string &body) {
    std::istringstream stream(body);
    std::string line;
    std::string last;

    while (std::getline(stream, line)) {
        // Check if line has any non-whitespace character
        auto it = std::find_if(line.begin(), line.end(), [](unsigned char ch) {
            return !std::isspace(ch);
        });
        if (it != line.end()) {
            last = line;
        }
    }

    return last;
}

bool parse_influxdb_command(const std::string &body, InfluxDBCommand &cmd) {
    // Look for data marker in response to ensure there is a result row
    if (body.find(",_result,0,") == std::string::npos) {
        return false;
    }

    std::string last_line = get_last_non_empty_line(body);
    if (last_line.empty()) {
        return false;
    }

    std::vector<std::string> tokens;
    std::stringstream ss(last_line);
    std::string item;
    while (std::getline(ss, item, ',')) {
        tokens.push_back(item);
    }

    if (tokens.size() < 7) {
        return false;
    }

    time_t timestamp;
    if (!parse_iso8601(tokens[5], timestamp)) {
        return false;
    }

    cmd.timestamp = timestamp;
    cmd.payload = tokens[6];
    return true;
}
