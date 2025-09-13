import unittest
from PiUI.PiUI import build_message, STX, ETX, ESC

class TestBuildMessage(unittest.TestCase):
    def test_large_payload_with_escapes(self):
        # Payload containing many bytes that require escaping
        payload = bytes([STX, ETX, ESC] * 85)  # 255 bytes total
        msg = build_message(0x01, payload)
        # Expected length: STX + type + len + escaped payload + checksum + ETX
        expected_len = 5 + len(payload) * 2
        self.assertEqual(len(msg), expected_len)
        self.assertEqual(msg[0], STX)
        self.assertEqual(msg[1], 0x01)
        self.assertEqual(msg[2], len(payload))
        self.assertEqual(msg[-1], ETX)

if __name__ == '__main__':
    unittest.main()
