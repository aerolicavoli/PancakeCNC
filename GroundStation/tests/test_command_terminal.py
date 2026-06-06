import struct
import unittest

from GroundStation.CommandTerminal import _build_command_packet


class CommandTerminalPacketTests(unittest.TestCase):
    def test_pump_purge_accepts_negative_speed(self):
        packet = _build_command_packet("pump_purge pumpSpeed_degps=-300 duration_ms=500")

        self.assertIsNotNone(packet)
        opcode, payload_len = packet[:2]
        speed, duration = struct.unpack("<fi", packet[2:])

        self.assertEqual(opcode, 0x19)
        self.assertEqual(payload_len, struct.calcsize("<fi"))
        self.assertEqual(speed, -300.0)
        self.assertEqual(duration, 500)


if __name__ == "__main__":
    unittest.main()
