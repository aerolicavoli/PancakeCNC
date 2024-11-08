import serial
import threading
import sys
import time
import string

# Serial port configuration
SERIAL_PORT = '/dev/ttys0'  # Mac
SERIAL_PORT = '/dev/ttyAMA0'  # Pi
BAUD_RATE = 9600

# Protocol constants
STX = 0x02
ETX = 0x03
ESC = 0x10

# Message types
MSG_TYPE_COMMAND = 0x01
MSG_TYPE_TELEMETRY = 0x02
MSG_TYPE_LOG = 0x03  # New message type for log messages

def calculate_checksum(payload):
    checksum = 0
    for byte in payload:
        checksum ^= byte
    return checksum

def build_message(message_type, payload):
    message = bytearray()
    message.append(STX)
    message.append(message_type)
    message.append(len(payload))
    # Payload with escaping
    escaped_payload = bytearray()
    for byte in payload:
        if byte in (STX, ETX, ESC):
            escaped_payload.append(ESC)
            escaped_payload.append(byte ^ 0x20)
        else:
            escaped_payload.append(byte)
    message.extend(escaped_payload)
    checksum = calculate_checksum(payload)  # Checksum is calculated on original payload
    message.append(checksum)
    message.append(ETX)
    return message

def parse_message_stream(ser):
    STATE_WAIT_FOR_STX = 0
    STATE_READ_TYPE = 1
    STATE_READ_LENGTH = 2
    STATE_READ_PAYLOAD = 3
    STATE_READ_CHECKSUM = 4
    STATE_WAIT_FOR_ETX = 5

    state = STATE_WAIT_FOR_STX
    message_type = None
    payload_length = None
    payload = []
    checksum = 0
    escape_next = False

    while True:
        byte = ser.read(1)
        if not byte:
            # No data received, sleep briefly
            time.sleep(0.01)
            continue
        b = byte[0]
        # print(f"Data received: {byte}")

        # Handle escaping
        if escape_next:
            b ^= 0x20
            escape_next = False
        elif b == ESC:
            escape_next = True
            continue  # Get the next byte

        # State machine
        if state == STATE_WAIT_FOR_STX:
            if b == STX:
                state = STATE_READ_TYPE
                checksum = 0
                payload.clear()
        elif state == STATE_READ_TYPE:
            message_type = b
            state = STATE_READ_LENGTH
        elif state == STATE_READ_LENGTH:
            payload_length = b
            if payload_length == 0:
                state = STATE_READ_CHECKSUM  # No payload to read
            else:
                state = STATE_READ_PAYLOAD

        elif state == STATE_READ_PAYLOAD:
            payload.append(b)
            checksum ^= b
            if len(payload) == payload_length:
                state = STATE_READ_CHECKSUM
        elif state == STATE_READ_CHECKSUM:
            received_checksum = b
            if checksum == received_checksum:
                state = STATE_WAIT_FOR_ETX
            else:
                print("Checksum mismatch")
                state = STATE_WAIT_FOR_STX
        elif state == STATE_WAIT_FOR_ETX:
            if b == ETX:
                # Assemble the message and handle it
                message = {
                    'message_type': message_type,
                    'payload': bytes(payload)
                }
                handle_received_message(message)
            else:
                print("Invalid ETX")
            state = STATE_WAIT_FOR_STX  # Reset for next message
        else:
            state = STATE_WAIT_FOR_STX  # Reset on any unexpected state

def handle_received_message(message):
    if message['message_type'] == MSG_TYPE_TELEMETRY:
        payload = message['payload']
        # Process telemetry data
        print("Telemetry Data Received:")
        print(" ".join(f"0x{byte:02X}" for byte in payload))
    elif message['message_type'] == MSG_TYPE_LOG:
        payload = message['payload']
        try:
            log_msg = payload.decode('utf-8', errors='replace')
            print(f"Log Message: {log_msg}")
        except UnicodeDecodeError:
            print("Received invalid log message data.")
    else:
        print(f"Unknown message type: 0x{message['message_type']:02X}")

def main():
    try:
        ser = serial.Serial(
            SERIAL_PORT,
            BAUD_RATE,
            timeout=1,
            write_timeout=1,    # Set write timeout to prevent blocking
            rtscts=False,       # Disable RTS/CTS flow control
            dsrdtr=False        # Disable DSR/DTR flow control
        )
    except serial.SerialException as e:
        print(f"Could not open serial port {SERIAL_PORT}: {e}")
        sys.exit(1)

    threading.Thread(target=parse_message_stream, args=(ser,), daemon=True).start()

    print("Serial communication started. Type 'help' for commands.")

    while True:
        try:
            user_input = input("Enter command: ").strip()
            if user_input.lower() == 'exit':
                print("Exiting...")
                ser.close()
                sys.exit(0)
            elif user_input.lower() == 'help':
                print("Available commands:")
                print("  sendcmd <data>   - Send command with payload data in hex (e.g., sendcmd 01 02 03)")
                print("  gettelemetry     - Request telemetry data")
                print("  exit             - Exit the application")
            elif user_input.lower().startswith('sendcmd'):
                parts = user_input.split()
                if len(parts) < 2:
                    print("Usage: sendcmd <data>")
                    continue
                try:
                    payload = bytes(int(byte, 16) for byte in parts[1:])
                    message = build_message(MSG_TYPE_COMMAND, payload)
                    ser.write(message)
                    print(f"Sent command: {' '.join(f'0x{b:02X}' for b in message)}")
                except ValueError:
                    print("Invalid payload data. Use hex values separated by spaces.")
                except serial.SerialTimeoutException:
                    print("Write operation timed out. Is the serial device connected?")
                except serial.SerialException as e:
                    print(f"Serial write error: {e}")
            elif user_input.lower() == 'gettelemetry':
                payload = bytes()  # Empty payload
                message = build_message(MSG_TYPE_TELEMETRY, payload)
                ser.write(message)
                print(f"Sent telemetry request: {' '.join(f'0x{b:02X}' for b in message)}")
            else:
                print("Unknown command. Type 'help' for available commands.")
        except KeyboardInterrupt:
            print("\nExiting...")
            ser.close()
            sys.exit(0)

if __name__ == '__main__':
    main()
