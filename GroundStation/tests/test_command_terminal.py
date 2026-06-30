import os
import struct
import sys
import tempfile
import types
import unittest
from unittest import mock

# CommandTerminal only needs requests when writing packets, but requests is not
# installed in all test environments. Provide a lightweight import stub so
# serialization tests can import the module without exercising network writes.
sys.modules.setdefault("requests", types.SimpleNamespace())

from GroundStation.CommandTerminal import _build_command_packet, _build_pump_purge_payload, _send_command


class CommandTerminalPacketTests(unittest.TestCase):
    def test_pump_purge_payload_preserves_negative_speed(self):
        payload = _build_pump_purge_payload({"pumpSpeed_degps": -300, "duration_ms": 500})

        speed, duration = struct.unpack("<fi", payload)

        self.assertEqual(speed, -300.0)
        self.assertEqual(duration, 500)

    def test_pump_purge_accepts_negative_speed(self):
        packet = _build_command_packet("pump_purge pumpSpeed_degps=-300 duration_ms=500")

        self.assertIsNotNone(packet)
        opcode, payload_len = packet[:2]
        speed, duration = struct.unpack("<fi", packet[2:])

        self.assertEqual(opcode, 0x19)
        self.assertEqual(payload_len, struct.calcsize("<fi"))
        self.assertEqual(speed, -300.0)
        self.assertEqual(duration, 500)

    def test_local_origin_packet(self):
        packet = _build_command_packet("local_origin OriginX_m=0.12 OriginY_m=0.34")

        self.assertIsNotNone(packet)
        opcode, payload_len = packet[:2]
        origin_x, origin_y = struct.unpack("<ff", packet[2:])

        self.assertEqual(opcode, 0x1F)
        self.assertEqual(payload_len, struct.calcsize("<ff"))
        self.assertAlmostEqual(origin_x, 0.12)
        self.assertAlmostEqual(origin_y, 0.34)

    def test_run_file_can_call_run_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            child = os.path.join(tmp, "child.cake")
            parent = os.path.join(tmp, "parent.cake")
            with open(child, "w", encoding="utf-8") as f:
                f.write("local_origin OriginX_m=0.1 OriginY_m=0.2\n")
            with open(parent, "w", encoding="utf-8") as f:
                f.write("run_file child.cake 1\n")

            with mock.patch("GroundStation.CommandTerminal.GCODE_DIR", tmp):
                with mock.patch("GroundStation.CommandTerminal._write_packet") as write_packet:
                    self.assertTrue(_send_command("run_file parent.cake 1"))

        write_packet.assert_called_once()
        self.assertEqual(write_packet.call_args.args[0][0], 0x1F)

    def test_run_file_disallows_recursion(self):
        with tempfile.TemporaryDirectory() as tmp:
            recursive = os.path.join(tmp, "recursive.cake")
            with open(recursive, "w", encoding="utf-8") as f:
                f.write("run_file recursive.cake 1\n")

            with mock.patch("GroundStation.CommandTerminal.GCODE_DIR", tmp):
                with self.assertRaisesRegex(ValueError, "Recursive run_file call disallowed"):
                    _send_command("run_file recursive.cake 1")

    def test_run_file_rejects_paths(self):
        with self.assertRaisesRegex(ValueError, "file name"):
            _send_command("run_file GroundStation/GCode/TestProgram.cake 1")

    def test_run_file_rejects_non_cake_extension(self):
        with self.assertRaisesRegex(ValueError, r"\.cake"):
            _send_command("run_file TestProgram.txt 1")


if __name__ == "__main__":
    unittest.main()
