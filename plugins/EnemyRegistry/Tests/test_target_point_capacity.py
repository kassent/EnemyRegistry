"""Offline TU5 instruction contract, not a live point-membership acceptance test."""

from pathlib import Path
import re
import struct
import unittest


SOURCE = Path(__file__).resolve().parents[1] / "EnemyArrayCapacityPatches.cpp"
# Ghidra TU5 MonsterHunterWilds.exe, EnemyManager.update @ 0x1455F0AA0.
# Exact bytes @ 0x1455F15C2, including the unchanged 13-category loop.
NATIVE_LOOP = bytes.fromhex(
    "48 FF C3 48 83 FB 0D 0F 85 21 FE FF FF "
    "49 FF C6 49 81 FE 96 00 00 00 48 8B 7C 24 50 0F 85 AC FD FF FF"
)
BODY_START = 0x1455F0AA0
LOOP_START = 0x1455F15C2


class TargetPointCapacityTests(unittest.TestCase):
    def setUp(self):
        self.source = SOURCE.read_text(encoding="utf-8")
        matches = re.findall(
            r'\{update, "([0-9A-F ]+)", (0x[0-9A-F]+), (\d+), ENEMY_COUNT\}',
            self.source,
        )
        self.assertEqual(len(matches), 1)
        pattern, scan, immediate = matches[0]
        self.pattern = bytes.fromhex(pattern)
        self.scan = int(scan, 16)
        self.immediate = int(immediate)

    def test_actual_native_instruction_and_branch_destination(self):
        self.assertIn('ResolveMethodBody(context, "app.EnemyManager", "update()")', self.source)
        offset = NATIVE_LOOP.index(self.pattern)
        # 短pattern从CMP开始，不再包含前面的3字节INC；实际imm32地址不变。
        self.assertEqual(LOOP_START + offset, 0x1455F15D2)
        self.assertEqual(struct.unpack_from("<I", NATIVE_LOOP, offset + self.immediate)[0], 150)
        self.assertEqual(self.pattern, bytes.fromhex("49 81 FE 96 00 00 00"))
        self.assertEqual(self.immediate, 3)
        self.assertEqual(LOOP_START + offset + self.immediate, 0x1455F15D5)
        self.assertLessEqual(LOOP_START - BODY_START + len(NATIVE_LOOP), self.scan)
        # JNZ rel32 at 0x1455F15DE returns to per-raw pack read, not the category loop.
        displacement = struct.unpack_from("<i", NATIVE_LOOP, len(NATIVE_LOOP) - 4)[0]
        self.assertEqual(LOOP_START + len(NATIVE_LOOP) + displacement, 0x1455F1390)

    def test_live_tdb_body_relocation_does_not_require_ghidra_offset(self):
        # Read-only live capture capacity-runtime-contract-guid16.json:
        # TDB body 0x14A005620, short CMP pattern starts at +0xB1E,
        # whereas Ghidra TU5 starts at +0xB32. The production patch scans bytes.
        runtime_entry = 0x14A005620
        runtime_loop = 0x14A00612E
        body = bytes(runtime_loop - runtime_entry) + NATIVE_LOOP
        body = body.ljust(self.scan, b"\xCC")
        hit = body.index(self.pattern)
        self.assertEqual(hit, 0xB1E)
        self.assertEqual(body.count(self.pattern), 1)
        self.assertEqual(runtime_entry + hit + self.immediate, 0x14A006141)
        displacement = struct.unpack_from("<i", NATIVE_LOOP, len(NATIVE_LOOP) - 4)[0]
        self.assertEqual(runtime_loop + len(NATIVE_LOOP) + displacement, 0x14A005EFC)

    def test_only_raw_upper_bound_changes_for_multiple_custom_species(self):
        offset = NATIVE_LOOP.index(self.pattern) + self.immediate
        for capacity in (151, 152, 153, 256):
            patched = bytearray(NATIVE_LOOP)
            struct.pack_into("<I", patched, offset, capacity)
            self.assertEqual(patched[:offset], NATIVE_LOOP[:offset])
            self.assertEqual(patched[offset + 4:], NATIVE_LOOP[offset + 4:])
            self.assertEqual(struct.unpack_from("<I", patched, offset)[0], capacity)
            self.assertEqual(patched[:7], bytes.fromhex("48 FF C3 48 83 FB 0D"))

    def test_bounded_unique_pattern_and_existing_fail_closed_collector(self):
        body = bytes(LOOP_START - BODY_START) + NATIVE_LOOP
        body = body.ljust(self.scan, b"\xCC")
        self.assertEqual(body.count(self.pattern), 1)
        # Wrong register or vanilla bound drift is not accepted by this contract.
        changed = self.pattern.replace(bytes.fromhex("49 81 FE"), bytes.fromhex("49 81 FF"))
        self.assertNotIn(changed, body)
        self.assertIn("size_t expectedCount = 1", self.source)
        self.assertIn("count != contract.expectedCount", self.source)
        self.assertIn("HookUtils::LookupFunctionPattern", self.source)
        self.assertNotIn("SafeWrite", self.source)  # Existing collector defers all writes.


if __name__ == "__main__":
    unittest.main()
