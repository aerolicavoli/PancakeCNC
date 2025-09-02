#include <cassert>
#include <string>
#include "../Pancake_esp/main/InfluxDBParser.h"

int main() {
    // Response with trailing blank line
    std::string response = ",result,table,_start,_stop,_time,_value,_field,_measurement\n" 
                           ",_result,0,2025-09-02T00:38:37.276148751Z,2025-09-02T00:43:37.276148751Z,2025-09-02T00:41:06.847Z,aQtIZWxsbyBXb3JsZA==,data,cmd\n\n";
    std::string expected = ",_result,0,2025-09-02T00:38:37.276148751Z,2025-09-02T00:43:37.276148751Z,2025-09-02T00:41:06.847Z,aQtIZWxsbyBXb3JsZA==,data,cmd";
    assert(get_last_non_empty_line(response) == expected);

    // Response with all blank lines
    std::string blank_response = "\n\n";
    assert(get_last_non_empty_line(blank_response).empty());

    return 0;
}
