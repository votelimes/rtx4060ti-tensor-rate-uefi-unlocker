import hashlib
import unittest
from read_safety_log import decode


def record(sequence):
    content = f'AD106 SAFETY v0.2.0\nsequence={sequence}\nphase=SIMULATION\n'.encode().ljust(4000, b' ')
    return (content + b'SHA256=' + hashlib.sha256(content).hexdigest().encode() + b'\nEND\n').ljust(4096, b' ')


class JournalReaderTests(unittest.TestCase):
    def test_latest_valid_sequence(self):
        self.assertEqual(decode(record(7) + record(6))['latest']['sequence'], 7)

    def test_torn_new_record_keeps_previous(self):
        damaged = bytearray(record(7)); damaged[100] ^= 1
        result = decode(bytes(damaged) + record(6))
        self.assertEqual(result['valid_slots'], 1)
        self.assertEqual(result['latest']['sequence'], 6)

    def test_both_corrupt_are_rejected(self):
        with self.assertRaises(ValueError):
            decode(bytes(8192))

    def test_truncated_file_is_rejected(self):
        with self.assertRaises(ValueError):
            decode(record(1))

    def test_wrong_format_with_valid_hash_is_rejected(self):
        wrong = bytearray(record(1)); wrong[0] = ord('X')
        wrong[4007:4071] = hashlib.sha256(wrong[:4000]).hexdigest().encode()
        with self.assertRaises(ValueError):
            decode(bytes(wrong) * 2)


if __name__ == '__main__':
    unittest.main()
