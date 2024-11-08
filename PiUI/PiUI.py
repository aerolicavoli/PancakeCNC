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

# Message types
MSG_TYPE_COMMAND = 0x01
MSG_TYPE_TELEMETRY = 0x02
MSG_TYPE_LOG = 0x03

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
    message.extend(payload)
    checksum = calculate_checksum(payload)
    message.append(checksum)
    message.append(ETX)
    return message

def parse_message(data):
    if len(data) < 5:
        print("Received data too short")
        return None

    if data[0] != STX or data[-1] != ETX:
        print("Invalid start or end delimiter")
        return None

    message_type = data[1]
    payload_length = data[2]

    if payload_length != len(data) - 5:
        print("Payload length mismatch")
        return None

    payload = data[3:3+payload_length]
    checksum = data[-2]

    if calculate_checksum(payload) != checksum:
        print("Checksum mismatch")
        return None

    return {
        'message_type': message_type,
        'payload': payload
    }

def read_from_port(ser):
    buffer = bytearray()
    while True:
        if ser.in_waiting > 0:
            data = ser.read(ser.in_waiting)
            buffer.extend(data)
            while True:
                if STX in buffer:
                    start_idx = buffer.index(STX)
                    if ETX in buffer[start_idx:]:
                        end_idx = buffer.index(ETX, start_idx)
                        complete_msg = buffer[start_idx:end_idx+1]
                        result = parse_message(complete_msg)
                        if result:
                            handle_received_message(result)
                        # Remove processed message from buffer
                        del buffer[:end_idx+1]
                    else:
                        # ETX not found yet, wait for more data
                        break
                else:
                    # STX not found, remove data before current position
                    del buffer[:]
                    break
        else:
            # No data, sleep for a short time to yield control
            time.sleep(0.01)
        # Check for incomplete message without ETX
        if len(buffer) > 0 and ETX not in buffer:
            try:
                ascii_msg = ''.join([chr(b) if chr(b) in string.printable else '.' for b in buffer])
                print(f"Incomplete message received: {ascii_msg}")
            except UnicodeDecodeError:
                print("Incomplete message received: Unable to decode as ASCII")
            # Clear the buffer after printing
            buffer.clear()

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
    # Initialize serial port with additional parameters
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

    threading.Thread(target=read_from_port, args=(ser,), daemon=True).start()

    print("Serial communication started. Type 'help' for commands.")

    while True:
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

if __name__ == '__main__':
    main()
