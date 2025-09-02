#include <cassert>
#include <string>
#include <ctime>
#include "../Pancake_esp/main/InfluxDBParser.h"

int main() {
    // Response with trailing blank line
    std::string response = ",result,table,_start,_stop,_time,_value,_field,_measurement\n"
                           ",_result,0,2025-09-02T00:38:37.276148751Z,2025-09-02T00:43:37.276148751Z,2025-09-02T00:41:06.847Z,aQtIZWxsbyBXb3JsZA==,data,cmd\n\n";
    std::string expected = ",_result,0,2025-09-02T00:38:37.276148751Z,2025-09-02T00:43:37.276148751Z,2025-09-02T00:41:06.847Z,aQtIZWxsbyBXb3JsZA==,data,cmd";
    assert(get_last_non_empty_line(response) == expected);

    InfluxDBCommand cmd;
    assert(parse_influxdb_command(response, cmd));

    // Expected timestamp: 2025-09-02T00:41:06Z (milliseconds are ignored)
    struct tm tm = {};
    tm.tm_year = 2025 - 1900;
    tm.tm_mon = 9 - 1;
    tm.tm_mday = 2;
    tm.tm_hour = 0;
    tm.tm_min = 41;
    tm.tm_sec = 6;
    time_t expected_ts = timegm(&tm);
    assert(cmd.timestamp == expected_ts);
    assert(cmd.payload == "aQtIZWxsbyBXb3JsZA==");

    // Response with all blank lines
    std::string blank_response = "\n\n";
    InfluxDBCommand cmd_blank;
    assert(!parse_influxdb_command(blank_response, cmd_blank));
    assert(get_last_non_empty_line(blank_response).empty());

    return 0;
}
