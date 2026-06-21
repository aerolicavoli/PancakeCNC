# Telemetry Budget

This document summarizes the registered telemetry points, their update rates, and the expected InfluxDB line-protocol payload size per transmit period.

## Scheduling model

Telemetry aggregation runs once every `BUFFER_ADD_PERIOD_MS` (`1000 ms`, or `1 Hz`). The transmit task runs every `TRANSMITPERIOD_MS` (`900 ms`) and sends whatever has accumulated in the working telemetry buffer since the previous transmit attempt.

Each telemetry point is registered with its own period. There are no separate runtime buckets; the aggregate task walks the registry and publishes any point whose configured period has elapsed.

Log lines are also added to the same telemetry buffer when present, but they are event-driven and are **not** included in the fixed-rate budget below.

## Fixed-rate telemetry points

| Rate | Period | Points | Measurements |
| --- | ---: | ---: | --- |
| `1 Hz` | `1000 ms` | 10 | `tipPos_X_m`, `tipPos_Y_m`, `targetPos_X_m`, `targetPos_Y_m`, `S0_Speed_degps`, `S0_TargetSpeed_degps`, `S1_Speed_degps`, `S1_TargetSpeed_degps`, `Pump_Speed_degps`, `Pump_TargetSpeed_degps` |
| `0.25 Hz` | `4000 ms` | 8 | `targetPos_S0_deg`, `targetPos_S1_deg`, `plannedTarget_S0_deg`, `plannedTarget_S1_deg`, `plannedDelta_S0_deg`, `plannedDelta_S1_deg`, `S0_Pos_deg`, `S1_Pos_deg` |
| `0.05 Hz` | `20000 ms` | 13 | `espTemp_C`, `limitBlocked_S0`, `limitBlocked_S1`, `S0_LimitSwitch`, `S1_LimitSwitch`, `cartesianBoundaryCorner0_X_m`, `cartesianBoundaryCorner0_Y_m`, `cartesianBoundaryCorner1_X_m`, `cartesianBoundaryCorner1_Y_m`, `cartesianBoundaryCorner2_X_m`, `cartesianBoundaryCorner2_Y_m`, `cartesianBoundaryCorner3_X_m`, `cartesianBoundaryCorner3_Y_m` |

## Size assumptions

The fixed-rate byte estimates assume the current line protocol format:

```text
<measurement>,location=us-midwest data=<value> <timestamp>\n
```

Assumptions used for the estimate:

- `<value>` is formatted by `%.5f`; the estimate uses `0.00000` (`7` bytes). Negative values or values with more integer digits add bytes.
- `<timestamp>` is a 13-digit millisecond timestamp.
- One newline is included per telemetry point.
- HTTP headers, TLS overhead, TCP/IP framing, and event-driven log telemetry are excluded.

## Payload budget by cadence

| Cadence contribution | Points | Bytes per cadence event | Average bytes per second |
| --- | ---: | ---: | ---: |
| `1 Hz` points | 10 | ~632 B every 1 s | ~632.00 B/s |
| `0.25 Hz` points | 8 | ~514 B every 4 s | ~128.50 B/s |
| `0.05 Hz` points | 13 | ~915 B every 20 s | ~45.75 B/s |
| **Total fixed-rate average** | 31 | N/A | **~806.25 B/s** |

## Expected bytes per transmit period

Because the transmit task runs every `900 ms` and aggregation runs every `1000 ms`, not every transmit tick contains a new aggregate sample. Over a long-running average, fixed-rate telemetry produces:

```text
806.25 B/s * 0.9 s/transmit period = ~725.6 B/transmit period
```

Representative fixed-rate transmit payloads are:

| Transmit payload case | Approximate payload bytes | Notes |
| --- | ---: | --- |
| Empty fixed-rate transmit | 0 B | Possible when the 900 ms transmit task wakes before a new 1000 ms aggregate cycle has added data. |
| 1 Hz-only aggregate | ~632 B | Most aggregate cycles. |
| 1 Hz + 0.25 Hz aggregate | ~1,146 B | Every 4 seconds. |
| 1 Hz + 0.25 Hz + 0.05 Hz aggregate | ~2,061 B | Every 20 seconds, when all registered periods align. |

The fixed telemetry buffer size is `6000 B` with a warning threshold of `5500 B`, so the largest fixed-rate aligned payload is currently about `2.1 kB`, leaving roughly `3.9 kB` of buffer headroom for queued log lines and larger-than-assumed numeric values.
