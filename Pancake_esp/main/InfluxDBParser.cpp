#include "InfluxDBParser.h"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <vector>
#include <cstdint>

// Calculate days since Unix epoch for a given civil date.
// Algorithm adapted from Howard Hinnant's date algorithms:
// https://howardhinnant.github.io/date_algorithms.html
static int days_from_civil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);      // [0, 399]
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; // [0, 365]
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;     // [0, 146096]
    return era * 146097 + static_cast<int>(doe) - 719468;
}

static time_t utc_mktime(const struct tm &tm) {
#if defined(_WIN32)
    struct tm tmp = tm;
    return _mkgmtime(&tmp);
#elif defined(__GLIBC__)
    struct tm tmp = tm;
    return timegm(&tmp);
#else
    return static_cast<time_t>(days_from_civil(tm.tm_year + 1900,
                                              static_cast<unsigned>(tm.tm_mon + 1),
                                              static_cast<unsigned>(tm.tm_mday))) * 86400 +
           tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec;
#endif
}

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
    out = utc_mktime(tm);
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
    if (tokens.size() > 9) {
        cmd.hash = tokens[9];
    } else {
        cmd.hash.clear();
    }
    return true;
}

size_t parse_influxdb_command_list(const std::string &body, std::vector<InfluxDBCommand> &out) {
    // Quick check for presence of data rows
    if (body.find(",_result,") == std::string::npos) {
        return 0;
    }

    std::istringstream stream(body);
    std::string line;
    size_t count = 0;
    while (std::getline(stream, line)) {
        // Skip comments/headers
        if (line.empty() || line[0] == '#') continue;
        // Expect data rows to contain ",_result,"
        if (line.find(",_result,") == std::string::npos) continue;

        std::vector<std::string> tokens;
        std::stringstream ss(line);
        std::string item;
        while (std::getline(ss, item, ',')) {
            tokens.push_back(item);
        }
        if (tokens.size() < 7) continue;

        time_t timestamp;
        if (!parse_iso8601(tokens[5], timestamp)) continue;

        InfluxDBCommand cmd;
        cmd.timestamp = timestamp;
        cmd.payload = tokens[6];
        if (tokens.size() > 9) {
            cmd.hash = tokens[9];
        }
        out.push_back(std::move(cmd));
        ++count;
    }
    return count;
}
