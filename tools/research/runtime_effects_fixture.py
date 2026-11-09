"""SDK-independent integer pipelines using the established board timer/UART."""
import argparse
from pathlib import Path
import shutil
import machine_pipeline_fixture as board


def expected(kind, seed):
    if kind == 0:
        value, result = 0xf1234567 ^ (seed * 0x10001), 0x13579bdf
        for i in range(12):
            divisor = (seed + i + 3) * 257 + 1
            quotient, remainder = divmod(value, divisor)
            result ^= quotient + remainder
            value = ((value ^ result) * (0x1021 + i) + quotient) & 0xffffffff
        return result ^ value
    value, bias, result = -123456789 + seed * 76543, 314159 - seed * 193, 0x2468ace0
    for i in range(16):
        divisor = (17 + seed + i) * (-1 if i & 1 else 1)
        quotient = abs(value) // abs(divisor) * (-1 if (value < 0) != (divisor < 0) else 1)
        remainder = value - quotient * divisor
        result = ((result << 3) ^ (quotient + bias) ^ remainder) & 0xffffffff
        value = quotient * 17 + remainder * 31 - bias
        bias += remainder
        assert -(1 << 31) <= value < 1 << 31 and -(1 << 31) <= bias < 1 << 31
    return (result ^ value ^ bias) & 0xffffffff


def create(output):
    output.mkdir(parents=True, exist_ok=False)
    tests = board.REPO / 'core/sources/compiler/src/mcs251/tests'
    shutil.copy2(tests / 'runtime-arithmetic.c', output / 'runtime-arithmetic.c')
    shutil.copy2(tests / 'machine-board.h', output / 'machine-board.h')
    text = (tests / 'machine-board.c').read_text()
    text = text.replace('static u8 checksum;', 'u32 unsigned_pipeline(u8);\nu32 signed_calibration(u8);\nstatic u8 checksum;')
    start, end = text.index('        x0 ='), text.index('        TH0 =')
    text = text[:start] + text[end:]
    start, end = text.index('        if (kind == 0)'), text.index('        TR0 = 0; ticks')
    text = text[:start] + '        result = kind ? signed_calibration(input) : unsigned_pipeline(input);\n' + text[end:]
    text = text.replace('ticks, x0, y0, x1, y1, x2, y2', 'ticks')
    (output / 'runtime-board.c').write_bytes(text.encode())
    units = [dict(name='arithmetic',file='runtime-arithmetic.c',backend='ordinary-wide',stackAuto=True,defines=[]),
             dict(name='board',file='runtime-board.c',backend='legacy',stackAuto=True,defines=[])]
    files = ['runtime-arithmetic.c', 'machine-board.h', 'runtime-board.c']
    board.write(output / 'mx251.json',dict(schema=2,target='AI8051U-34K64-32',model='small',optimization='speed',
        backend='ordinary-wide',explicitAbi=False,standard='sdcc11',runtime='standard',defines=[],files=files,
        units=units,archives=[],link=['arithmetic','board','@startup']))
    board.write(output / 'source.json',dict(files={f:board.sha(output/f) for f in files}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command',required=True)
    p = sub.add_parser('create'); p.add_argument('--output',type=Path,required=True)
    p = sub.add_parser('capture'); p.add_argument('--hex',type=Path,required=True); p.add_argument('--output',type=Path,required=True)
    args = parser.parse_args()
    if args.command == 'create': create(args.output)
    else: board.capture(args.hex,args.output,lambda data:board.check(data,expected))
