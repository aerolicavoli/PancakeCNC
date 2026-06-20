#include <cstdlib>
#include <string>
#include <vector>

#include "InfluxDBParser.h"
#include "TestHarness.h"

namespace
{
const char *kInfluxResponse =
    "#group,false,false,true,true,false,false,true,true,true,true\n"
    "#datatype,string,long,dateTime:RFC3339,dateTime:RFC3339,dateTime:RFC3339,string,string,string,string\n"
    "#default,_result,,,,,,,,\n"
    ",result,table,_start,_stop,_time,_value,_field,_measurement,device\n"
    ",_result,0,2026-01-01T00:00:00Z,2026-01-01T00:10:00Z,2026-01-01T00:01:02Z,EgE=,payload,commands,pancake\n"
    ",_result,0,2026-01-01T00:00:00Z,2026-01-01T00:10:00Z,2026-01-01T00:03:04.012Z,EgM=,payload,commands,pancake\n"
    "\n"
    ",_result,0,2026-01-01T00:00:00Z,2026-01-01T00:10:00Z,2026-01-01T00:03:04.123Z,EgI=,payload,commands,pancake\n";

void TestLastNonEmptyLineSkipsTrailingWhitespace()
{
    EXPECT_EQ(get_last_non_empty_line("\nfirst\n  \nsecond\n\t\n"), std::string("second"));
}

void TestParseLatestCommand()
{
    InfluxDBCommand cmd{};
    EXPECT_TRUE(parse_influxdb_command(kInfluxResponse, cmd));
    EXPECT_EQ(cmd.timestamp_ms, static_cast<int64_t>(1767225784123));
    EXPECT_EQ(cmd.payload, std::string("EgI="));
}

void TestParseCommandListPreservesOrder()
{
    std::vector<InfluxDBCommand> commands;
    EXPECT_EQ(parse_influxdb_command_list(kInfluxResponse, commands), static_cast<size_t>(3));
    EXPECT_EQ(commands.size(), static_cast<size_t>(3));
    EXPECT_EQ(commands[0].timestamp_ms, static_cast<int64_t>(1767225662000));
    EXPECT_EQ(commands[0].payload, std::string("EgE="));
    EXPECT_EQ(commands[1].timestamp_ms, static_cast<int64_t>(1767225784012));
    EXPECT_EQ(commands[1].payload, std::string("EgM="));
    EXPECT_EQ(commands[2].timestamp_ms, static_cast<int64_t>(1767225784123));
    EXPECT_EQ(commands[2].payload, std::string("EgI="));
}

void TestMalformedResponsesAreRejected()
{
    InfluxDBCommand cmd{};
    EXPECT_FALSE(parse_influxdb_command("# no data rows\n", cmd));
    EXPECT_FALSE(parse_influxdb_command(",_result,0,start,stop,not-a-timestamp,payload\n", cmd));

    std::vector<InfluxDBCommand> commands;
    EXPECT_EQ(parse_influxdb_command_list(",_result,0,start,stop,not-a-timestamp,payload\n", commands),
              static_cast<size_t>(0));
    EXPECT_TRUE(commands.empty());
}
} // namespace

int main()
{
    TestLastNonEmptyLineSkipsTrailingWhitespace();
    TestParseLatestCommand();
    TestParseCommandListPreservesOrder();
    TestMalformedResponsesAreRejected();

    PrintTestPassed("InfluxDBParser unit test");
    return EXIT_SUCCESS;
}
