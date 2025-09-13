import base64
from influxdb_client import InfluxDBClient, Point
from influxdb_client.client.write_api import SYNCHRONOUS
import time

# === CONFIGURATION ===
INFLUXDB_URL = "https://us-east-1-1.aws.cloud2.influxdata.com"
INFLUXDB_TOKEN = "oyju_LoaNkWX27nSllkyU3b2TyMcRDGMvQJlkmtHn41X2CnxiSAQNTbJpmn0Lttff8Ohp75gH6-cgODGcK1ipQ=="
INFLUXDB_ORG = "PancakeCNC"
INFLUXDB_BUCKET = "PancakeCNCCommand"
DEVICE_ID = "esp32s3_1"

# === YOUR BINARY COMMAND ===
binary_command = bytes([0xC0, 0xDE, 0x00, 0xFF, 0x01, 0x23])  # example binary data

# === ENCODE COMMAND AS BASE64 ===
encoded_command = base64.b64encode(binary_command).decode("ascii")

# === UNIQUE COMMAND ID ===
command_id = f"cmd_{int(time.time())}"

# === CONNECT TO INFLUXDB ===
client = InfluxDBClient(
    url=INFLUXDB_URL,
    token=INFLUXDB_TOKEN,
    org=INFLUXDB_ORG
)
write_api = client.write_api(write_options=SYNCHRONOUS)

# === BUILD POINT ===
point = (
    Point("command_queue")
    .tag("device", DEVICE_ID)
    .field("b64_command", encoded_command)
    .time(int(time.time() * 1000.0), write_precision="ms")
)

# === WRITE TO INFLUXDB ===
write_api.write(bucket=INFLUXDB_BUCKET, record=point)
print(f"Wrote command {command_id} to InfluxDB.")

client.close()
