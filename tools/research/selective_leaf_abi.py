"""Build one measured ABI4 trial for a frequently called, closed integer leaf.

Uses the existing compiler's ABI4 contract. Sources and the baseline are read
only; the selected image is named in selection.json. This is an explicit
research entry point, not a change to the default compiler ABI.
"""
from __future__ import annotations

import argparse
from collections import Counter
import json
import os
from pathlib import Path
import re
import subprocess

from call_layout import audit, ihex, read_unit, sha


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def code_lines(text):
    return [line.split(";", 1)[0].strip() for line in text.splitlines()]


def only_direct_references(text, symbol):
    """Labels/exports and direct calls are the only migratable references."""
    token = re.compile(r"(?<![\w$])" + re.escape(symbol) + r"(?![\w$])")
    allowed = re.compile(r"(?:" + re.escape(symbol) + r":{1,2}|\.globl\s+" +
                         re.escape(symbol) + r"|ecall\s+" + re.escape(symbol) + r")")
    return all(not token.search(line) or allowed.fullmatch(line)
               for line in code_lines(text))


def select_candidates(units, calls, library_refs, startup, minimum_gross):
    definitions = Counter(name for unit in units for name in unit["definitions"])
    candidates = []
    for unit in units:
        text = "\n".join(unit["lines"])
        for name, signature in unit["definitions"].items():
            match = re.fullmatch(r"(v|[iu](?:8|16|32))\(([iu](?:8|16|32)),([iu](?:8|16|32))\)", signature)
            if not match or definitions[name] != 1 or name in library_refs:
                continue
            if unit["abi"].get(name) != (2, 0, 0) or any(
                unit["flags"].get(name, {}).get(k) != "0"
                for k in ("isr", "addressTaken", "opaque", "indirect")
            ):
                continue
            if not re.search(r"^\s*\.globl\s+" + re.escape(name) + r"\s*$", text, re.M):
                continue
            body = [i for i in unit["instructions"] if i.owner == name]
            if not body or any(re.match(r"(?:[a-z]*call|ejmp|ljmp)\s+(?!__wm_)", i.source) for i in body):
                continue
            if any(re.search(r"\b(?:opaque|indirect)=1\b", "\n".join(u["lines"])) for u in units):
                continue
            if not all(only_direct_references("\n".join(u["lines"]), name) for u in units):
                continue
            if re.search(r"(?<![\w$])" + re.escape(name) + r"(?![\w$])", startup):
                continue
            machine = [(u["name"], i.owner, i.address) for u in units for i in u["instructions"]
                       if re.fullmatch(r"ecall\s+" + re.escape(name), i.source)]
            paired = [c for c in calls if c["target"] == name and c["signature"] == signature]
            if not machine or set(machine) != {(c["unit"], c["owner"], c["address"]) for c in paired}:
                continue
            gross = sum(c["layouts"]["abi4"].get("grossPushCodeBytes", 0) +
                        c["layouts"]["abi4"].get("grossCleanupCodeBytes", 0) for c in paired)
            if gross < minimum_gross:
                continue
            ret = "void" if match[1] == "v" else match[1]
            candidates.append(dict(symbol=name, unit=unit["name"], signature=signature,
                                   transport=f"{ret}({match[2]},{match[3]})",
                                   calls=len(machine), grossStackCodeBytes=gross))
    return sorted(candidates, key=lambda row: (-row["grossStackCodeBytes"], row["symbol"]))


def final_binding(root, selected):
    """Check every final call's relocation against the one ABI4 definition."""
    memory = ihex(root / "firmware.hex")
    config = read_json(root / "project.snapshot.json")
    units = [read_unit(root, u["name"], memory) for u in config["units"]]
    old, new = selected["symbol"], "___m251r4_" + selected["symbol"][1:]
    owners = [u for u in units if new in u["labels"]]
    if len(owners) != 1 or owners[0]["abi"].get(new) != (4, 0, 0):
        raise ValueError("ABI4 definition was not emitted exactly once")
    address = owners[0]["labels"][new]
    count = 0
    for u in units:
        if any(re.search(r"(?<![\w$])" + re.escape(old) + r"(?![\w$])", l)
               for l in code_lines("\n".join(u["lines"]))):
            raise ValueError("old ABI symbol remains in candidate")
        for ins in u["instructions"]:
            if re.fullmatch(r"ecall\s+" + re.escape(new), ins.source):
                if len(ins.data) != 4 or ins.data[0] != 0x9a or int.from_bytes(ins.data[1:], "big") != address:
                    raise ValueError("candidate call relocation differs from ABI4 definition")
                count += 1
    if not count:
        raise ValueError("candidate has no bound calls")
    return dict(symbol=new, address=address, calls=count)


def run(args):
    baseline, core, output = args.baseline.resolve(), args.core.resolve(), args.output.resolve()
    parent = read_json(baseline / "receipt.json")
    config = read_json(baseline / "project.snapshot.json")
    if config["runtime"] != "standard" or config["explicitAbi"] or config["archives"] or any(
        u["backend"] != "ordinary-wide" or not u["stackAuto"] or not u["file"].endswith(".c")
        for u in config["units"]
    ):
        raise ValueError("trial requires ordinary stack-auto C units and standard runtime")
    manifest = read_json(core / "core.json")
    if manifest["compiler"]["sha256"] != parent["compilerSha256"]:
        raise ValueError("trial must use the baseline compiler")
    # The compiler, source snapshot and relocated baseline are bound before
    # deriving the internal contract; no SDK name or function-name filter.
    analysis = audit(baseline)
    memory = ihex(baseline / "firmware.hex")
    units = [read_unit(baseline, u["name"], memory) for u in config["units"]]
    refs = set()
    for library in (core / "toolchain/lib").glob("mcs251*/*.lib"):
        refs.update(re.findall(rb"\bS (\S+) Ref[0-9A-Fa-f]+", library.read_bytes()))
    refs = {r.decode("ascii") for r in refs}
    keep = []
    if parent["functionGc"]["enabled"]:
        gc_path = baseline / "function-gc/manifest.json"
        if sha(gc_path) != parent["functionGc"]["manifestSha256"]:
            raise ValueError("baseline function-GC manifest changed")
        keep = [r.split(":", 2)[2] for r in read_json(gc_path)["roots"] if r.startswith("fn:")]
        refs.update(keep)
    candidates = select_candidates(units, analysis["calls"], refs,
                                   (baseline / "startup.asm").read_text(encoding="latin1"), args.minimum_gross)
    output.mkdir(parents=True, exist_ok=False)
    result = dict(schema=1, status="NO_CANDIDATE", candidates=candidates,
                  baseline=str(baseline), baselineCodeBytes=parent["codeBytes"],
                  baselineReceiptSha256=sha(baseline / "receipt.json"),
                  compilerSha256=parent["compilerSha256"], hardwareExecuted=False,
                  runtimeRatio=None, selectedBuild=str(baseline), defaultAbiChanged=False)
    if candidates:
        chosen = candidates[0]
        contract = output / "internal-abi4.txt"
        contract.write_bytes(("MX251-INTERNAL-ABI4-V2\n" + chosen["symbol"] + "\t" + chosen["transport"] + "\n").encode("ascii"))
        env = {k: v for k, v in os.environ.items() if not k.upper().startswith(("MX251_", "MCS251_"))}
        env["MX251_INTERNAL_ABI4_CONTRACT"] = str(contract)
        build = output / "candidate"
        command = [args.powershell, "-NoProfile", "-File", str(core / "build/qualified/Build.ps1"),
                   "-Project", str(args.project.resolve()), "-ToolchainRoot", str(core / "toolchain"),
                   "-ProfileRoot", str(core / "profiles" / config["target"]), "-OutputDirectory", str(build),
                   "-WideValueLimit", str(parent["wideCapacity"]["valueLimit"]),
                   "-WideNodeLimit", str(parent["wideCapacity"]["nodeLimit"]),
                   "-OrderedAsm", parent["orderedAsm"]["requested"]]
        if args.cmake_input:
            command += ["-CMakeInput", str(args.cmake_input.resolve())]
        for field, option in [("c251BitComplement", "C251BitComplement"),
                              ("wordCompareImmediate", "WordCompareImmediate"),
                              ("rangeNarrowMultiply", "RangeNarrowMultiply"),
                              ("constantRotateFusion", "ConstantRotateFusion")]:
            if parent.get(field):
                command.append("-" + option)
            elif field == "wordCompareImmediate":
                command.append("-WordCompareImmediate:$false")
        if parent["functionGc"]["enabled"]:
            command.append("-FunctionGc")
            if keep:
                command += ["-FunctionGcKeep", ",".join(keep)]
        if parent["volatileDataWide"]["enabled"] or parent["objectAddressNative"]["enabled"] or parent.get("calleeCostEvidence") or parent["leafCallFeedback"]["enabled"]:
            raise ValueError("baseline uses an additional experiment; compare it separately")
        result.update(chosen=chosen, contractSha256=sha(contract), command=command)
        result["status"] = "BUILDING"
        (output / "selection.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        process = subprocess.run(command, env=env, capture_output=True)
        (output / "build.log").write_bytes(process.stdout + process.stderr)
        if process.returncode:
            raise RuntimeError(f"candidate build failed ({process.returncode}); see {output / 'build.log'}")
        after = read_json(build / "receipt.json")
        if ({p: v["sha256"] for p, v in parent["inputs"].items()} !=
            {p: v["sha256"] for p, v in after["inputs"].items()} or
            config != read_json(build / "project.snapshot.json") or
            parent["profileLockSha256"] != after["profileLockSha256"] or
            parent["compilerSha256"] != after["compilerSha256"]):
            raise ValueError("candidate input/configuration differs from baseline")
        binding = final_binding(build, chosen)
        audit(build)
        saving = parent["codeBytes"] - after["codeBytes"]
        result.update(status="SMALLER_IMAGE" if saving > 0 else "KEEP_BASELINE",
                      candidateCodeBytes=after["codeBytes"], savedBytes=saving,
                      finalBinding=binding, candidateReceiptSha256=sha(build / "receipt.json"),
                      candidateFirmwareSha256=sha(build / "firmware.hex"),
                      selectedBuild=str(build if saving > 0 else baseline))
    (output / "selection.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({k: result[k] for k in ("status", "baselineCodeBytes", "selectedBuild")} | {
        "savedBytes": result.get("savedBytes", 0)}, indent=2))
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("baseline", "core", "output", "project"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--cmake-input", type=Path)
    parser.add_argument("--powershell", default="pwsh")
    parser.add_argument("--minimum-gross", type=int, default=64,
                        help="minimum observed stack-transport bytes to justify one full build")
    run(parser.parse_args())
