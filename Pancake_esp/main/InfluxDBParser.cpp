#include "InfluxDBParser.h"
#include <sstream>
#include <algorithm>
#include <cctype>

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
