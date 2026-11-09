"""Read-only, final-image-bound call-layout feasibility study (Python 3.10+).

The packed layout is a proposal, not an implemented compiler ABI. Counts are
static call sites, not executions. Gross removable PUSH/cleanup bytes exclude
new register moves, spills, callee entry code and runtime/library changes.
"""
from __future__ import annotations

import argparse
from collections import Counter
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def ihex(path: Path) -> dict[int, int]:
    memory: dict[int, int] = {}
    base = 0
    ended = False
    for line in path.read_text(encoding="ascii").splitlines():
        if not line:
            continue
        if ended or not line.startswith(":"):
            raise ValueError("invalid HEX record order")
        raw = bytes.fromhex(line[1:])
        if len(raw) < 5 or len(raw) != raw[0] + 5 or sum(raw) & 255:
            raise ValueError("invalid HEX length/checksum")
        count, kind = raw[0], raw[3]
        offset = int.from_bytes(raw[1:3], "big")
        data = raw[4:-1]
        if kind == 0:
            if offset + count > 65536:
                raise ValueError("HEX record crosses a segment")
            for n, byte in enumerate(data):
                address = base + offset + n
                if address in memory:
                    raise ValueError("overlapping HEX records")
                memory[address] = byte
        elif kind in (2, 4) and count == 2 and offset == 0:
            base = int.from_bytes(data, "big") << (4 if kind == 2 else 16)
        elif kind == 1 and count == 0 and offset == 0:
            ended = True
        elif kind in (3, 5) and count == 4 and offset == 0:
            pass
        else:
            raise ValueError("unsupported HEX record")
    if not ended:
        raise ValueError("missing HEX EOF")
    return memory


def signature_widths(signature: str) -> list[int] | None:
    match = re.fullmatch(r"[^()]+\(([^()]*)\)", signature)
    if not match:
        return None
    widths = []
    for token in match[1].split(",") if match[1] else []:
        if re.fullmatch(r"[iu](8|16|32)", token):
            widths.append(int(token[1:]) // 8)
        elif token == "f32":
            widths.append(4)
        elif re.fullmatch(r"p3_\d+", token):
            # Storage kind is retained in the signature. This only measures
            # transport widths; it does not grant ABI4 pointer qualification.
            widths.append(3)
        else:
            return None
    return widths


def layout(widths: list[int], kind: str) -> list[dict]:
    """Physical byte slices in big-endian GPR order; 3-byte return address.

    packed8 preserves the same R4..R7/R12..R15 window as ABI4. A narrow
    parameter takes a word slot; a pointer/dword takes an aligned dword slot.
    Once a parameter spills, all later parameters remain on the stack.
    """
    if kind not in ("abi2", "abi4", "packed8") or any(w not in (1, 2, 3, 4) for w in widths):
        raise ValueError("unsupported layout or parameter width")
    rows, cursor, stack, exhausted = [], 0, 0, False
    for index, width in enumerate(widths):
        registers = None
        if kind == "abi2" and index == 0:
            registers = list(reversed(["DPL", "DPH", "B", "A"][:width]))
        elif kind == "abi4" and index < 2:
            end = 8 if index == 0 else 16
            registers = [f"R{i}" for i in range(end - width, end)]
        elif kind == "packed8" and not exhausted:
            slot = 2 if width <= 2 else 4
            aligned = (cursor + slot - 1) // slot * slot
            if aligned + slot <= 8:
                bank = 4 if aligned < 4 else 12
                # Within each dword group consume its low word first.
                end = bank + (4 if slot == 4 or aligned % 4 == 0 else 2)
                registers = [f"R{i}" for i in range(end - width, end)]
                cursor = aligned + slot
            else:
                exhausted = True
        row = {"argument": index + 1, "width": width, "registers": registers}
        if registers is None:
            stack += width
            row["entryOffset"] = -2 - stack
        rows.append(row)
    return rows


@dataclass
class Instruction:
    line: int
    address: int
    data: bytes
    source: str
    owner: str | None


def read_unit(root: Path, name: str, memory: dict[int, int]) -> dict:
    asm, rst = root / (name + ".asm"), root / (name + ".rst")
    lines = asm.read_bytes().decode("latin1").splitlines()
    owners: dict[int, str | None] = {}
    signatures: dict[tuple[str, str], set[str]] = {}
    definitions, flags, abi = {}, {}, {}
    owner = None
    for number, line in enumerate(lines, 1):
        begin = re.search(r"mx251-function begin schema=1 symbol=(\S+)(.*)", line)
        if begin:
            owner = begin[1]
            flags[owner] = dict(re.findall(r"(\w+)=(\d+)", begin[2]))
        owners[number] = owner
        definition = re.search(r"mx251-scalar-definition schema=1 symbol=(\S+) signature=(\S+)", line)
        if definition:
            definitions[definition[1]] = definition[2]
        call = re.search(r"mx251-scalar-call schema=1 owner=(\S+) target=(\S+) signature=(\S+)", line)
        if call:
            signatures.setdefault((call[1], call[2]), set()).add(call[3])
        bank = re.search(r"mx251-bank-contract schema=1 symbol=(\S+) abi=(\d+) bank=(\d+) naked=(\d+)", line)
        if bank:
            abi[bank[1]] = tuple(map(int, bank.groups()[1:]))
        if "mx251-function end schema=1" in line:
            owner = None
    instructions, labels = [], {}
    for line in rst.read_bytes().decode("latin1").splitlines():
        # ASxxxx relocated listing: address 6:12, bytes 13:28, cycles 29:34,
        # then source line number and the original ASM text.
        source = re.fullmatch(r"\s*(\d+) (.*)", line[34:])
        if not source or not re.fullmatch(r"[0-9A-Fa-f]{6}", line[6:12]):
            continue
        number, text = int(source[1]), source[2]
        if not 1 <= number <= len(lines) or text.rstrip() != lines[number - 1].rstrip():
            raise ValueError(f"stale ASM/RST pair: {name}:{number}")
        address = int(line[6:12], 16)
        label = re.fullmatch(r"\s*([\w$]+):{1,2}\s*", text)
        if label:
            labels[label[1]] = address
        encoded = line[13:28].strip()
        if not encoded or not re.fullmatch(r"[0-9A-Fa-f]{2}(?: [0-9A-Fa-f]{2})*", encoded):
            continue
        data = bytes.fromhex(encoded)
        op = text.strip().split(None, 1)[0] if text.strip() else ""
        if op.startswith((";", ".")) or not re.fullmatch(r"[A-Za-z]+", op):
            continue
        if any(memory.get(address + n) != byte for n, byte in enumerate(data)):
            raise ValueError(f"RST/HEX byte mismatch: {name}:{number}")
        instructions.append(Instruction(number, address, data, text.strip(), owners[number]))
    expected_calls = {n for n, text in enumerate(lines, 1) if re.fullmatch(r"\s*ecall\s+[\w$]+\s*", text)}
    if not expected_calls.issubset({ins.line for ins in instructions}):
        raise ValueError(f"unbound direct CALL in listing: {name}")
    return dict(name=name, lines=lines, instructions=instructions, labels=labels,
                signatures=signatures, definitions=definitions, flags=flags, abi=abi,
                inputs=[{"path": str(p.resolve()), "sha256": sha(p)} for p in (asm, rst)])


def push_width(text: str) -> int | None:
    if re.fullmatch(r"pushw\s+#.+", text):
        return 2
    match = re.fullmatch(r"push\s+(\S+)", text)
    if not match:
        return None
    operand = match[1].lower()
    if re.fullmatch(r"dr(?:[048]|1[26]|2[048])", operand):
        return 4
    if re.fullmatch(r"wr(?:[02468]|[12][02468]|30)", operand):
        return 2
    if re.fullmatch(r"r(?:[0-9]|1[0-5])|ar[0-7]|acc|a|b|dpl|dph|dpxl|0x[0-9a-f]{1,2}", operand):
        return 1
    return None


def stack_setup(unit: dict, index: int, payload: int) -> tuple[list[Instruction], list[Instruction]] | None:
    """Recognize a bounded straight-line suffix, never estimate missing setup.

    The caller-cleanup count must match the typed ABI2 stack payload. CALLs,
    labels, branches, pops and arbitrary SP writes are barriers. PUSH groups
    straddling the payload boundary are rejected, rather than counting saves.
    """
    ins = unit["instructions"]
    call = ins[index]
    if not payload:
        return [], []
    cleanup, released, last_line = [], 0, call.line
    for following in ins[index + 1:]:
        match = re.fullmatch(r"(dec|sub)\s+spx(?:,#(0x[0-9a-fA-F]+|[0-9]+))?", following.source)
        if (following.owner != call.owner or not match or
            (match[1] == "sub" and match[2] is None) or any(
                re.fullmatch(r"\s*[\w$]+:{1,2}\s*", s)
                for s in unit["lines"][last_line:following.line - 1]
            )):
            return None
        released += int(match[2], 0) if match[2] else 1
        cleanup.append(following)
        last_line = following.line
        if released >= payload:
            break
    if released != payload:
        return None
    pushes, found, next_line = [], 0, call.line
    for previous in reversed(ins[:index]):
        if previous.owner != call.owner:
            return None
        between = unit["lines"][previous.line:next_line - 1]
        if any(re.fullmatch(r"\s*[\w$]+:{1,2}\s*", s) for s in between):
            return None
        next_line = previous.line
        text = previous.source.split(";", 1)[0].strip()
        width = push_width(text)
        if width:
            pushes.append(previous)
            found += width
            if found >= payload:
                return (pushes, cleanup) if found == payload else None
        else:
            op = text.split(None, 1)[0].lower()
            if op not in ("mov", "movz", "movs", "clr", "anl", "orl", "xrl", "xch", "nop",
                          "add", "addc", "sub", "subb", "inc", "dec", "rl", "rlc", "rr", "rrc",
                          "swap", "srl", "sra", "mul", "div", "cpl") or re.search(
                r"\b(?:spx|dr60|sp|sph|0x81|0x84)\s*(?:,|$)", text
            ):
                return None
    return None


def audit(root: Path) -> dict:
    root = root.resolve()
    receipt_path, hex_path = root / "receipt.json", root / "firmware.hex"
    receipt = json.loads(receipt_path.read_text(encoding="utf-8-sig"))
    if receipt.get("status") != "complete" or receipt.get("firmwareSha256") != sha(hex_path):
        raise ValueError("receipt does not bind this complete firmware")
    memory = ihex(hex_path)
    if len(memory) != receipt["codeBytes"]:
        raise ValueError("receipt byte count differs from HEX")
    for record in receipt["inputs"].values():
        if sha(Path(record["path"])) != record["sha256"]:
            raise ValueError("source differs from frozen receipt: " + record["path"])
    names = sorted(p.stem for p in root.glob("u*.asm") if re.fullmatch(r"u\d+", p.stem))
    if not names:
        raise ValueError("no final unit listings")
    units = [read_unit(root, name, memory) for name in names]
    defs: dict[str, list] = {}
    for unit in units:
        for symbol, sig in unit["definitions"].items():
            if symbol in unit["labels"]:
                defs.setdefault(symbol, []).append((unit, sig))
    rows, omitted = [], Counter()
    for unit in units:
        for index, ins in enumerate(unit["instructions"]):
            match = re.fullmatch(r"ecall\s+([\w$]+)", ins.source)
            if not match:
                continue
            target = match[1]
            if len(ins.data) != 4 or ins.data[0] != 0x9A:
                raise ValueError("unexpected direct ECALL encoding")
            declarations = unit["signatures"].get((ins.owner, target), set())
            definition = defs.get(target, [])
            if len(declarations) != 1 or len(definition) != 1:
                omitted["unpaired-source-call-or-definition"] += 1
                continue
            callee, signature = definition[0]
            if declarations != {signature}:
                omitted["signature-mismatch"] += 1
                continue
            if int.from_bytes(ins.data[1:], "big") != callee["labels"][target]:
                raise ValueError("call relocation does not match typed definition")
            if callee["abi"].get(target) != (2, 0, 0) or any(
                callee["flags"][target].get(key) != "0" for key in ("isr", "addressTaken", "opaque", "indirect")
            ):
                omitted["special-or-unqualified-entry"] += 1
                continue
            widths = signature_widths(signature)
            if widths is None:
                omitted["unsupported-transport"] += 1
                continue
            payload = sum(widths[1:])
            setup = stack_setup(unit, index, payload)
            row = dict(unit=unit["name"], owner=ins.owner, target=target,
                       address=ins.address, signature=signature, originalStackPayload=payload,
                       setupRecognized=setup is not None, layouts={})
            for kind in ("abi4", "packed8"):
                proposal = layout(widths, kind)
                remaining = sum(p["width"] for p in proposal if p["registers"] is None)
                removed = payload - remaining
                result = dict(parameters=proposal, removedStackPayload=removed)
                if setup is not None:
                    pushes, cleanup = setup
                    covered, encoded = 0, 0
                    for push in pushes:
                        width = push_width(push.source)
                        if covered + width > removed:
                            break  # Do not count a partially removable PUSH.
                        covered += width
                        encoded += len(push.data)
                    result.update(wholePushPayload=covered, grossPushCodeBytes=encoded,
                                  grossCleanupCodeBytes=sum(len(ins.data) for ins in cleanup) if remaining == 0 else 0)
                row["layouts"][kind] = result
            rows.append(row)
    summary = {}
    for kind in ("abi4", "packed8"):
        summary[kind] = {
            field: sum(r["layouts"][kind].get(field, 0) for r in rows)
            for field in ("removedStackPayload", "wholePushPayload", "grossPushCodeBytes", "grossCleanupCodeBytes")
        }
    # Keep the next investigation tied to reachable final machine code, too.
    functions = []
    for unit in units:
        costs, calls, refused = {}, Counter(), {}
        for line in unit["lines"]:
            match = re.search(r"wide refusal function=(\S+) stage=\S+ reason=(\S+)", line)
            if match:
                refused["_" + match[1]] = match[2]
        for ins in unit["instructions"]:
            if ins.owner is None:
                continue
            cost = costs.setdefault(ins.owner, dict(codeBytes=0, pushPopCodeBytes=0, stackMoveCodeBytes=0))
            cost["codeBytes"] += len(ins.data)
            if re.match(r"(?:push|pushw|pop)\s", ins.source):
                cost["pushPopCodeBytes"] += len(ins.data)
            if re.match(r"mov\s", ins.source) and re.search(r"@(?:spx|dr60)\b", ins.source):
                cost["stackMoveCodeBytes"] += len(ins.data)
            match = re.fullmatch(r"ecall\s+([\w$]+)", ins.source)
            if match:
                calls[(ins.owner, match[1])] += 1
        for symbol, cost in costs.items():
            functions.append(dict(unit=unit["name"], symbol=symbol, **cost,
                                  refusal=refused.get(symbol),
                                  calls={target: n for (owner, target), n in calls.items() if owner == symbol}))
    return dict(schema=1, status="RESEARCH_LAYOUT_ONLY", hardwareExecuted=False,
                compilerChanged=False, productionAbiAdded=False, runtimeRatio=None,
                codeBytes=receipt["codeBytes"], compilerSha256=receipt["compilerSha256"],
                target=receipt["target"], model=receipt["model"], pairedStaticCalls=len(rows),
                setupRecognized=sum(r["setupRecognized"] for r in rows), omittedCalls=dict(omitted),
                summary=summary, calls=rows,
                functions=sorted(functions, key=lambda f: (-f["codeBytes"], f["unit"], f["symbol"])),
                inputs=[{"path": str(p), "sha256": sha(p)} for p in (receipt_path, hex_path)] +
                       [entry for unit in units for entry in unit["inputs"]],
                limitations=["Static sites, not executions or cycle estimates.",
                             "Proposal does not establish whole-program ABI migration closure.",
                             "Gross PUSH/cleanup savings exclude new moves, spills, callee entry and libraries.",
                             "Unrecognized setup is omitted from code-byte totals, never assumed free.",
                             "No compiled candidate image; no new size or runtime acceptance."])


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True, help="completed .build/mx251/latest directory")
    parser.add_argument("--output", type=Path, required=True, help="new JSON evidence path")
    args = parser.parse_args()
    report = audit(args.build)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("x", encoding="utf-8") as stream:
        json.dump(report, stream, indent=2)
        stream.write("\n")
    print(json.dumps({k: report[k] for k in ("codeBytes", "pairedStaticCalls", "setupRecognized", "omittedCalls", "summary")}, indent=2))
