"""Independent layout and stale-evidence guards; no compiler/hardware needed."""
import itertools
import json
from pathlib import Path
import tempfile
import unittest

from call_layout import Instruction, audit, ihex, layout, read_unit, sha, signature_widths, stack_setup


class LayoutTests(unittest.TestCase):
    def test_four_words(self):
        rows = layout([2] * 5, "packed8")
        self.assertEqual([r["registers"] for r in rows],
                         [["R6", "R7"], ["R4", "R5"], ["R14", "R15"], ["R12", "R13"], None])
        self.assertEqual(rows[-1]["entryOffset"], -4)

    def test_old_layouts(self):
        self.assertEqual(layout([4], "abi2")[0]["registers"], ["A", "B", "DPH", "DPL"])
        rows = layout([3, 1, 2, 4], "abi4")
        self.assertEqual(rows[0]["registers"], ["R5", "R6", "R7"])
        self.assertEqual(rows[1]["registers"], ["R15"])
        self.assertEqual([r["entryOffset"] for r in rows[2:]], [-4, -8])

    def test_alignment_and_no_backfill(self):
        rows = layout([1, 4, 1], "packed8")
        self.assertEqual(rows[0]["registers"], ["R7"])
        self.assertEqual(rows[1]["registers"], ["R12", "R13", "R14", "R15"])
        self.assertIsNone(rows[2]["registers"])
        self.assertEqual(layout([4, 2, 1], "packed8")[-1]["registers"], ["R13"])

    def test_exhaustive_physical_invariants(self):
        # All 87,381 signatures up to eight arguments, not just examples.
        for count in range(9):
            for widths in itertools.product((1, 2, 3, 4), repeat=count):
                occupied, stacked, depth = set(), False, 0
                for row in layout(list(widths), "packed8"):
                    registers = row["registers"]
                    if registers is None:
                        stacked = True
                        depth += row["width"]
                        self.assertEqual(row["entryOffset"], -2 - depth)
                    else:
                        self.assertFalse(stacked)
                        self.assertEqual(len(registers), row["width"])
                        self.assertFalse(occupied.intersection(registers))
                        self.assertTrue(set(registers) <= {f"R{i}" for i in (*range(4, 8), *range(12, 16))})
                        occupied.update(registers)

    def test_unsupported_signature_is_not_guessed(self):
        self.assertEqual(signature_widths("v(u16,p3_6,f32)"), [2, 3, 4])
        self.assertEqual(signature_widths("v()"), [])
        for sig in ("v(u64)", "v(bit)", "v(p1_1)", "bad", "v(s(x))"):
            self.assertIsNone(signature_widths(sig))


class EvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def test_hex_guards(self):
        p = self.root / "a.hex"
        p.write_text(":0200000400FFFB\n:040100009AFF020060\n:00000001FF\n")
        self.assertEqual(ihex(p), {0xFF0100: 0x9A, 0xFF0101: 0xFF, 0xFF0102: 2, 0xFF0103: 0})
        for text in (":040100009AFF020021\n:00000001FF\n",
                     ":00000001FF\n:00000001FF\n", ":0000000000\n",
                     ":0100000001FE\n:0100000001FE\n:00000001FF\n"):
            p.write_text(text)
            with self.assertRaises(ValueError):
                ihex(p)

    def test_listing_binding(self):
        source = "\tecall\t_target"
        (self.root / "u0.asm").write_text(source + "\n")
        rst = self.root / "u0.rst"
        rst.write_text(f"      FF0100 {'9A FF 02 00':<16} [12] {1:4d} {source}\n")
        memory = dict(zip(range(0xFF0100, 0xFF0104), b"\x9a\xff\x02\x00"))
        self.assertEqual(len(read_unit(self.root, "u0", memory)["instructions"]), 1)
        memory[0xFF0103] = 1
        with self.assertRaisesRegex(ValueError, "RST/HEX"):
            read_unit(self.root, "u0", memory)
        (self.root / "u0.asm").write_text("\tecall\t_changed\n")
        with self.assertRaisesRegex(ValueError, "stale ASM/RST"):
            read_unit(self.root, "u0", memory)
        rst.write_text("")
        with self.assertRaisesRegex(ValueError, "unbound"):
            read_unit(self.root, "u0", memory)

    def test_audit_and_source_drift(self):
        lines = [
            "; mx251-function begin schema=1 symbol=_target isr=0 addressTaken=0 opaque=0 indirect=0",
            "; mx251-scalar-definition schema=1 symbol=_target signature=v(u16,u16)",
            "_target:", "\teret",
            "; mx251-bank-contract schema=1 symbol=_target abi=2 bank=0 naked=0",
            "; mx251-function end schema=1 symbol=_target",
            "; mx251-function begin schema=1 symbol=_caller isr=0 addressTaken=0 opaque=0 indirect=0",
            "; mx251-scalar-call schema=1 owner=_caller target=_target signature=v(u16,u16) values=?,?",
            "_caller:", "\tpush\twr14", "\tecall\t_target", "\tdec\tspx,#2", "\teret",
            "; mx251-function end schema=1 symbol=_caller",
        ]
        encoded = {3: (0xFF0100, ""), 4: (0xFF0100, "AA"), 9: (0xFF0101, ""),
                   10: (0xFF0101, "CA 79"), 11: (0xFF0103, "9A FF 01 00"),
                   12: (0xFF0107, "1B FD"), 13: (0xFF0109, "AA")}
        (self.root / "u0.asm").write_text("\n".join(lines) + "\n")
        listing = []
        for number, (address, data) in encoded.items():
            listing.append(f"      {address:06X} {data:<16} [12] {number:4d} {lines[number - 1]}")
        (self.root / "u0.rst").write_text("\n".join(listing) + "\n")
        data = b"".join(bytes.fromhex(value[1]) for value in encoded.values())
        record = bytes([len(data), 1, 0, 0]) + data
        hex_path = self.root / "firmware.hex"
        hex_path.write_text(":0200000400FFFB\n:" + (record + bytes([-sum(record) & 255])).hex() + "\n:00000001FF\n")
        source = self.root / "fixture.c"
        source.write_text("/* frozen source identity */\n")
        receipt = dict(status="complete", firmwareSha256=sha(hex_path), codeBytes=len(data),
                       compilerSha256="fixture", target="fixture", model="small",
                       inputs={"fixture.c": dict(path=str(source), sha256=sha(source))})
        (self.root / "receipt.json").write_text(json.dumps(receipt))
        report = audit(self.root)
        self.assertEqual(report["pairedStaticCalls"], 1)
        self.assertEqual(report["summary"]["packed8"]["grossPushCodeBytes"], 2)
        self.assertEqual(report["summary"]["packed8"]["grossCleanupCodeBytes"], 2)
        self.assertFalse(report["productionAbiAdded"])
        source.write_text("/* changed */\n")
        with self.assertRaisesRegex(ValueError, "source differs"):
            audit(self.root)

    @staticmethod
    def setup_unit(sources):
        return dict(lines=sources, instructions=[
            Instruction(i, 100 + i * 4, b"\x00\x00", text, "f")
            for i, text in enumerate(sources, 1) if not text.endswith(":")])

    def test_saved_registers_not_counted_as_parameters(self):
        unit = self.setup_unit(["push dr24", "push wr6", "sub wr16,wr16", "push wr16",
                                "ecall _target", "dec spx,#4", "pop dr24"])
        pushes, cleanup = stack_setup(unit, 4, 4)
        self.assertEqual([p.source for p in pushes], ["push wr16", "push wr6"])
        self.assertEqual([ins.source for ins in cleanup], ["dec spx,#4"])

    def test_multiple_cleanup_instructions(self):
        unit = self.setup_unit(["push wr6", "push wr14", "push wr12", "ecall _target",
                                "dec spx,#4", "dec spx,#2", "dec spx,#2"])
        pushes, cleanup = stack_setup(unit, 3, 6)
        self.assertEqual(len(pushes), 3)
        self.assertEqual(len(cleanup), 2)  # The final adjustment is a local frame.

    def test_unknown_setup_is_not_free(self):
        for sources in (["push wr6", "ecall _target", "dec spx,#1"],
                        ["push wr6", "pop wr6", "ecall _target", "dec spx,#2"],
                        ["push wr6", "label:", "ecall _target", "dec spx,#2"],
                        ["push wr6", "mov spx,dr4", "ecall _target", "dec spx,#2"]):
            unit = self.setup_unit(sources)
            index = next(i for i, ins in enumerate(unit["instructions"]) if ins.source == "ecall _target")
            self.assertIsNone(stack_setup(unit, index, 2))


if __name__ == "__main__":
    unittest.main()
