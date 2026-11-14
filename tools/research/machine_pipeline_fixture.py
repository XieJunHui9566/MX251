"""Build inputs and independent result checking for the shared-machine pipeline.

The triangle and its private pointer helper are extracted unchanged from the
SDK. LCD writes become trace callbacks. Control is a portable synthetic byte
workload, including aliased address arguments and a shadowed automatic local.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import statistics
import subprocess
import sys
import time

REPO = Path(__file__).resolve().parents[2]


def create(source, output):
    output.mkdir(parents=True, exist_ok=False)
    data = source.read_bytes()
    start = data.index(b'static void _swap(')
    pos = data.index(b'\nvoid Fill_Triangel(', start)
    end = data.index(b'{', pos) + 1
    depth = 1
    while depth:
        depth += (data[end] == 123) - (data[end] == 125)
        end += 1
    (output / 'machine-triangle.c').write_bytes(
        b'/* Extracted unchanged from SDK gui.c: _swap and Fill_Triangel. */\n'
        b'#include "machine-board.h"\n' + data[start:end] + b'\n')
    files = ['machine-board.h', 'machine-triangle.c', 'machine-control.c', 'machine-trace.c', 'machine-board.c']
    for name in files:
        if name != 'machine-triangle.c':
            shutil.copy2(REPO / 'core/sources/compiler/src/mcs251/tests' / name, output / name)
    units = [dict(name='u'+str(i), file=f, backend='legacy' if f == 'machine-board.c' else 'ordinary-wide',
                  stackAuto=True, defines=[]) for i, f in enumerate(files[1:])]
    config = dict(schema=2, target='AI8051U-34K64-32', model='small', optimization='speed',
                  backend='ordinary-wide', explicitAbi=False, standard='sdcc11', runtime='standard',
                  defines=[], files=files, units=units, archives=[], link=[u['name'] for u in units]+['@startup'])
    write(output/'mx251.json', config)
    write(output/'source.json', dict(origin=str(source), originSha256=sha(source),
          functions=['_swap', 'Fill_Triangel'], files={f: sha(output/f) for f in files}))


def expected(kind, seed):
    if kind:
        state, length, status, result = seed & 3, 0x1234, 0, 0x13579bdf
        length += 7
        if seed & 1:
            length = 0
        else:
            result ^= 0x5678 ^ length
        for i in range(32):
            octet = (seed+i) & 255
            octet = ((octet << 1) | (octet >> 7)) & 255
            if state == 0:
                length += octet
                status = 0x101 if octet & 128 else 0x202
            elif state == 1:
                length ^= (octet << 8) | i
                status += 3
            elif state == 2:
                if octet < 128:
                    status ^= length
                else:
                    length -= octet
            else:
                status = (status & 0xff00) | octet
                length += status
            length &= 65535
            status &= 65535
            result = ((result << 3) ^ length ^ (status << 16) ^ result) & 0xffffffff
            state = (state+1) & 3
        return result
    vertices = [(2+(seed & 7), 10+seed), (3+((seed >> 1) & 7), 40+(seed & 7)),
                (4+((seed >> 2) & 7), 20+(seed & 15))]
    (y0, x0), (y1, x1), (y2, x2) = sorted(vertices, key=lambda p: p[0])
    spans = []
    if y0 == y2:
        spans.append((min(x0, x1, x2), y0, max(x0, x1, x2)))
    else:
        # Signed division truncates toward zero, matching C251 and MX251.
        divide = lambda a, b: (abs(a)//b) * (-1 if a < 0 else 1)
        sa = sb = 0
        last = y1 if y1 == y2 else y1-1
        y = y0
        while y <= last:
            a, b = x0+divide(sa, y1-y0), x0+divide(sb, y2-y0)
            sa += x1-x0
            sb += x2-x0
            spans.append((min(a, b) & 65535, y, max(a, b) & 65535))
            y += 1
        # In the SDK expression, unsigned int multiplication occurs before
        # assignment to long. Preserve that 16-bit C arithmetic here.
        sa, sb = ((x2-x1)*(y-y1)) & 65535, ((x2-x0)*(y-y0)) & 65535
        while y <= y2:
            a, b = (x1+divide(sa, y2-y1)) & 65535, (x0+divide(sb, y2-y0)) & 65535
            sa += x2-x1
            sb += x2-x0
            spans.append((min(a, b), y, max(a, b)))
            y += 1
    result = 0x12345678
    for a, y, b in spans:
        for value in [a, y, b, y, 0x5a00+seed]:
            result = ((result << 5) ^ result ^ value) & 0xffffffff
    return result


def check(data, reference=expected):
    samples = {}
    pos = 0
    while True:
        pos = data.find(b'MIR1', pos)
        if pos < 0 or pos+16 > len(data):
            break
        frame = data[pos:pos+16]
        pos += 1
        checksum = 0
        for octet in frame[:14]:
            checksum ^= octet
        if frame[-2:] != b'\r\n' or checksum:
            continue
        kind, seed = frame[4:6]
        assert kind < 2 and seed < 64
        value = int.from_bytes(frame[6:10], 'big')
        assert value == reference(kind, seed), (kind, seed, hex(value), hex(reference(kind, seed)))
        assert frame[12] == 0, ('timer-overflow', kind, seed)
        ticks = int.from_bytes(frame[10:12], 'big')
        samples.setdefault(f'{kind}:{seed}', []).append(ticks)
    assert len(samples) == 128, ('missing-cases', len(samples))
    ticks = {key: statistics.median(values) for key, values in samples.items()}
    return dict(status='PASS', cases=128, frames=sum(map(len, samples.values())), ticks=ticks,
                aggregateTicks={str(k): sum(ticks[f'{k}:{i}'] for i in range(64)) for k in range(2)})


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write(path, value):
    path.write_bytes((json.dumps(value, indent=2)+'\n').encode())


def capture(image, output, checker=check, max_frames=128*4):
    import serial
    from serial.tools import list_ports
    output.mkdir(parents=True, exist_ok=False)
    tool = REPO/'tools/stc_link1d/stc_link1d.py'
    devices = json.loads(subprocess.check_output([sys.executable, str(tool), 'list']))
    device = next(d for d in devices if d['product'] == 'STC-USB-Link1D')
    ports = [p for p in list_ports.comports() if p.serial_number == device['serial'] and p.location.endswith(':x.1')]
    assert len(ports) == 1
    run = subprocess.run([sys.executable, str(tool), 'flash', '--hex', str(image), '--sha256', sha(image),
                          '--device-path', device['path'], '--output', str(output/'flash')], capture_output=True)
    (output/'flash.log').write_bytes(run.stdout+run.stderr)
    assert run.returncode == 0, run.stderr
    uart = serial.Serial()
    uart.port, uart.baudrate, uart.timeout = ports[0].device, 115200, .2
    uart.dtr = uart.rts = False
    uart.open()
    data = bytearray()
    start = time.monotonic()
    try:
        while time.monotonic()-start < 25 and len(data) < max_frames*16:
            data.extend(uart.read(4096))
    finally:
        uart.close()
    (output/'uart.bin').write_bytes(data)
    result = checker(data)
    result.update(firmwareSha256=sha(image), device=device, port=ports[0].device)
    write(output/'result.json', result)
    print(json.dumps({k: v for k, v in result.items() if k not in ('ticks', 'device', 'values', 'mismatches')}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('create'); p.add_argument('--source', type=Path, required=True); p.add_argument('--output', type=Path, required=True)
    p = sub.add_parser('capture'); p.add_argument('--hex', type=Path, required=True); p.add_argument('--output', type=Path, required=True)
    p = sub.add_parser('check'); p.add_argument('capture', type=Path)
    args = parser.parse_args()
    if args.command == 'create': create(args.source, args.output)
    elif args.command == 'capture': capture(args.hex, args.output)
    else: print(json.dumps(check(args.capture.read_bytes()), indent=2))
