"""Read C251/A251 OMF records and emit the verified relocatable subset.

The format is inferred from controlled C251/A251 objects and L251 output.
Symbol names are deliberately namespaced: object conversion is not a C ABI
adapter. Unsupported segment/relocation forms are reported before emission.
"""
import argparse
from collections import Counter
from dataclasses import asdict, dataclass, field
import hashlib
import json
from pathlib import Path
import struct


class OmfError(ValueError):
    pass


def records(data):
    at = 0
    while at < len(data):
        if at + 3 > len(data):
            raise OmfError(f'truncated record header at {at:#x}')
        kind, size = struct.unpack_from('<BH', data, at)
        end = at + 3 + size
        if not size or end > len(data) or sum(data[at:end]) & 255:
            raise OmfError(f'invalid record length/checksum at {at:#x}')
        yield kind, data[at+3:end-1], at
        at = end


def name_at(data, at):
    end = at + 1 + data[at]
    if end > len(data):
        raise OmfError('truncated counted name')
    return data[at+1:end].decode('ascii'), end


@dataclass
class Segment:
    name: str
    attributes: str
    size: int
    space: int


@dataclass
class Public:
    name: str
    segment: int
    flags: int
    offset: int
    space: int
    debug_type: int


@dataclass
class External:
    name: str
    space: int
    debug_type: int


@dataclass
class Relocation:
    offset: int
    kind: int
    target_kind: int
    target: int
    addend: int


@dataclass
class Chunk:
    address_mode: int
    segment: int
    flags: int
    offset: int
    data: str
    relocations: list = field(default_factory=list)


@dataclass
class Module:
    name: str
    segments: list = field(default_factory=list)
    publics: list = field(default_factory=list)
    externals: list = field(default_factory=list)
    chunks: list = field(default_factory=list)
    debug_relocations: int = 0


def read(path):
    modules, counts = [], Counter()
    current, owner = None, None
    # Library directory, producer/source, type/line/scope/register metadata.
    metadata = {0x70, 0x72, 0x80, 0x86, 0x98, 0xa6, 0xa8, 0xaa, 0xac,
                0xc8, 0xd0}
    for kind, data, at in records(Path(path).read_bytes()):
        counts[f'{kind:02X}'] += 1
        if kind == 0xc0:
            if current is not None:
                raise OmfError('nested module')
            current = Module(name_at(data, 0)[0])
            modules.append(current)
            owner = None
        elif kind == 0xc6:
            current, owner = None, None
        elif kind in metadata:
            owner = None
        elif current is None:
            raise OmfError(f'record {kind:02X} outside module at {at:#x}')
        elif kind == 0x84:
            name, end = name_at(data, 14)
            if end != len(data):
                raise OmfError('segment record has trailing data')
            current.segments.append(Segment(name, data[:8].hex(),
                int.from_bytes(data[8:12], 'little'), int.from_bytes(data[12:14], 'little')))
        elif kind in (0x88, 0x8a):
            pos = 0
            while pos < len(data):
                if kind == 0x88:
                    segment, flags, offset, space, debug_type = struct.unpack_from('<HBIHH', data, pos)
                    name, end = name_at(data, pos+11)
                    current.publics.append(Public(name, segment, flags, offset, space, debug_type))
                else:
                    space, debug_type = struct.unpack_from('<HH', data, pos)
                    name, end = name_at(data, pos+4)
                    current.externals.append(External(name, space, debug_type))
                pos = end
        elif kind == 0x94:
            reserved, segment, flags, offset = struct.unpack_from('<HHHI', data)
            if reserved not in (0, 1):
                raise OmfError('unknown data header')
            owner = Chunk(reserved, segment, flags, offset, data[10:].hex())
            current.chunks.append(owner)
        elif kind == 0xc1:
            owner = 'procedure-debug'
        elif kind == 0x96:
            if len(data) % 12 or owner is None:
                raise OmfError('relocation without a complete owner')
            for pos in range(0, len(data), 12):
                r = Relocation(*struct.unpack_from('<HBBII', data, pos))
                if owner == 'procedure-debug':
                    if r.kind != 0x15 or r.target_kind != 1:
                        raise OmfError('unknown procedure-debug relocation')
                    current.debug_relocations += 1
                else:
                    owner.relocations.append(r)
        else:
            raise OmfError(f'unsupported record {kind:02X} at {at:#x}')
    if current is not None:
        raise OmfError('unterminated module')
    return modules, dict(sorted(counts.items()))


def symbol(name):
    return '__omf_' + name.encode('ascii').hex()


def segment_symbol(namespace, module_index, segment_index):
    return f'__omf_{namespace}_m{module_index}_s{segment_index}'


# Low byte, relative byte, high byte, word, 24-bit address, EDATA word,
# C251 near pointer word, and a same-bank call/jump operand.
WIDTHS = {0x00: 1, 0x02: 1, 0x03: 1, 0x04: 2, 0x08: 3,
          0x0a: 2, 0x0d: 2, 0x0e: 2, 0x13: 2}
SPACES = {1: 'XDATA', 2: 'DATA', 5: 'CODE', 7: 'DATA'}


def unsupported(modules):
    reasons = Counter()
    for module in modules:
        for seg in module.segments:
            if seg.attributes not in ('0204000000000000', '0206000000000000'):
                reasons['segment-attributes:'+seg.attributes] += 1
            if seg.space not in SPACES:
                reasons[f'segment-class:{seg.space}'] += 1
        for p in module.publics:
            if p.flags or not 1 <= p.segment <= len(module.segments):
                reasons['absolute-or-special-public'] += 1
        for chunk in module.chunks:
            if chunk.address_mode:
                reasons['absolute-data'] += 1
            if not 1 <= chunk.segment <= len(module.segments):
                reasons['data-segment'] += 1
            if chunk.flags not in (0, 4):
                reasons[f'data-flags:{chunk.flags}'] += 1
            for r in chunk.relocations:
                if r.kind not in WIDTHS:
                    reasons[f'relocation:{r.kind:02X}'] += 1
                if r.target_kind not in (1, 2):
                    reasons[f'relocation-target:{r.target_kind}'] += 1
    return dict(sorted(reasons.items()))


def assembly(modules, namespace=None):
    missing = unsupported(modules)
    if missing:
        raise OmfError('unsupported OMF forms: '+json.dumps(missing, sort_keys=True))
    if namespace is None:
        namespace = hashlib.sha256(json.dumps([asdict(m) for m in modules], sort_keys=True).encode()).hexdigest()[:12]
    lines = ['; OMF251 object import; raw C251 ABI, explicit bridges required.',
             '.module omf251_import', '.source']
    names = {symbol(p.name) for m in modules for p in m.publics}
    for module in modules:
        for chunk in module.chunks:
            for r in chunk.relocations:
                if r.target_kind == 2:
                    if r.target >= len(module.externals):
                        raise OmfError('unknown external target')
                    names.add(symbol(module.externals[r.target].name))
    lines += ['.globl '+n for n in sorted(names)]
    for mi, module in enumerate(modules):
        for si, seg in enumerate(module.segments, 1):
            area = f'OMF_{namespace}_M{mi}_S{si}'
            base = segment_symbol(namespace, mi, si)
            lines += [f'; {module.name}: {seg.name} (OMF class {seg.space})',
                      f'.area {area} (REL,CON,{SPACES[seg.space]})', base+'::']
            # Public aliases preserve offsets even inside data directives.
            for p in module.publics:
                if p.segment == si:
                    if p.offset > seg.size:
                        raise OmfError('public outside segment')
                    lines.append(f'{symbol(p.name)} = {base}+{p.offset}')
            cursor = 0
            for chunk in sorted((c for c in module.chunks if c.segment == si), key=lambda c:c.offset):
                data = bytes.fromhex(chunk.data)
                if chunk.offset < cursor or chunk.offset+len(data) > seg.size:
                    raise OmfError('overlapping/out-of-range segment data')
                if chunk.offset > cursor:
                    lines.append(f'.ds {chunk.offset-cursor}')
                pos = 0
                for r in sorted(chunk.relocations, key=lambda r:r.offset):
                    width = WIDTHS[r.kind]
                    start = r.offset - (1 if r.kind == 0x13 else 0)
                    if start < pos or r.offset+width > len(data):
                        raise OmfError('overlapping/out-of-range relocation')
                    if r.target_kind == 1:
                        if not 1 <= r.target <= len(module.segments):
                            raise OmfError('unknown segment target')
                        target = segment_symbol(namespace, mi, r.target)
                    else:
                        if r.target >= len(module.externals):
                            raise OmfError('unknown external target')
                        target = symbol(module.externals[r.target].name)
                    expr = f'({target}+0x{r.addend:x})'
                    lines += byte_lines(data[pos:start])
                    if r.kind == 0x13:
                        op = {0x12:'lcall', 0x02:'ljmp'}.get(data[start])
                        if op is None:
                            raise OmfError('same-bank relocation without LCALL/LJMP')
                        lines.append(f'{op} {expr}')
                    elif r.kind == 0x02:
                        # A251 emits even local branch displacements as fixups.
                        # SDAS can resolve same-area label differences exactly.
                        if r.target_kind != 1 or r.target != si:
                            raise OmfError('relative branch to another segment')
                        delta = r.addend - (chunk.offset+r.offset+1)
                        if not -128 <= delta <= 127:
                            raise OmfError('relative branch out of range')
                        lines += byte_lines(bytes([delta & 255]))
                    elif r.kind == 0x00:
                        lines.append(f'.db {expr}')
                    elif r.kind == 0x03:
                        lines.append(f'.db ({expr} >> 8)')
                    elif r.kind == 0x08:
                        lines.append(f'.3byte {expr}')
                    elif r.kind == 0x0e:
                        # C251 far pointers store a zero byte, 24-bit bank,
                        # then the low word. A shifted SDAS word relocation
                        # does not select the bank; emit its byte explicitly.
                        lines += ['.db 0', f'.db ({expr} >> 16)']
                    else:
                        lines.append(f'.dw {expr}')
                    pos = r.offset+width
                lines += byte_lines(data[pos:])
                cursor = chunk.offset+len(data)
            if cursor < seg.size:
                lines.append(f'.ds {seg.size-cursor}')
    return '\n'.join(lines)+'\n'


def byte_lines(data):
    return ['.db '+','.join(f'0x{v:02x}' for v in data[i:i+16]) for i in range(0,len(data),16)]


def inspect(path):
    modules, counts = read(path)
    return dict(source=str(Path(path).resolve()), sha256=hashlib.sha256(Path(path).read_bytes()).hexdigest(),
                recordCounts=counts, modules=[asdict(m) for m in modules],
                unsupported=unsupported(modules), cAbiAdapted=False)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('object', type=Path)
    parser.add_argument('--assembly', type=Path)
    parser.add_argument('--inventory', type=Path, required=True)
    args = parser.parse_args()
    report = inspect(args.object)
    args.inventory.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    if args.assembly:
        args.assembly.write_text(assembly(read(args.object)[0]), encoding='ascii')
