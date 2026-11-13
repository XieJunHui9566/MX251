"""Paired p108 source repair and UART DMA echo check; frozen corpus stays intact."""
import argparse
import concurrent.futures
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

from unified_sdk_matrix import one, pin, read, save

REPO = Path(__file__).resolve().parents[2]


def build(config_path, core, output, transport_fixture=False):
    config = read(config_path)
    old_core = Path(config['core'])
    config['toolFiles'] = [str(core/Path(p).relative_to(old_core)) if Path(p).is_relative_to(old_core) else p
                           for p in config['toolFiles']]
    config['toolFiles'].append(str(Path(__file__).resolve()))
    config.update(core=str(core.resolve()), output=str(output.resolve()), excludeIds=[])
    source = Path(config['corpus'])/'projects/p108'
    dest = output/'projects/p108'
    dest.mkdir(parents=True, exist_ok=False)
    meta = read(source/'inputs.json')
    shutil.copy2(source/'inputs.json', dest/'original-inputs.json')
    native = dest/'native'
    native.mkdir()
    before = []
    for item in meta['files']:
        p = source/'native'/item['name']
        assert pin(p)['sha256'] == item['copySha256']
        before.append(pin(p))
        shutil.copy2(p, native/p.name)
    adapted = source/('mx251-braces' if (source/'mx251-braces').is_dir() else 'mx251')
    mx = dest/'mx251'
    shutil.copytree(adapted/'src', mx/'src')
    for name in ('CMakeLists.txt', 'mx251.json', 'dialect.json'):
        shutil.copy2(adapted/name, mx/name)
    changes = []
    for path, pattern, replacement in (
        (native/'isr.asm', rb'(?im)^(\s*)JMP[ \t]+006BH[ \t]*(\r?)$', rb'\1RETI\2'),
        (mx/'src/isr.asm', rb'(?m)^ljmp 0xff006b(\r?)$', rb'reti\1'),
    ):
        old = pin(path)
        data, count = re.subn(pattern, replacement, path.read_bytes())
        assert count == 1, (path, count)
        path.write_bytes(data)
        changes.append(dict(before=old, after=pin(path)))
    if transport_fixture:
        # Separate hardware fixture only: allow USB packet gaps within one DMA frame.
        # Do not use this C configuration change for the repaired benchmark size.
        for path in (native/'UART1.c', mx/'src/uart1.c'):
            old = pin(path)
            data, count = re.subn(rb'(UR1TOTH\s*=\s*)0x00(\s*;)', rb'\g<1>0x04\2', path.read_bytes())
            assert count == 1, (path, count)
            path.write_bytes(data)
            changes.append(dict(before=old, after=pin(path)))
    scope = 'p108-usb-timeout-fixture' if transport_fixture else 'p108-repaired'
    manifest = dest/'repaired-inputs.json'
    save(manifest, dict(id=scope, originalManifest=pin(dest/'original-inputs.json'),
         originalNativeInputs=before, changes=changes,
         nativeInputs=[pin(p) for p in sorted(native.iterdir())],
         mxInputs=[pin(p) for p in sorted(mx.rglob('*')) if p.is_file()],
         repair='Shared unused extended-vector terminal: absolute JMP 006BH -> RETI in both dialects.',
         cSourcesChanged=transport_fixture, usbTimeoutFixture=transport_fixture,
         frozenCorpusChanged=False, sameInputAsOriginalCohort=False))
    save(output/'configuration.json', config)
    save(output/'tools.json', [pin(p) for p in config['toolFiles']])
    historical = next(r['keilBytes'] for r in read(config['ledger'])['rows'] if r['id'] == 'p108')
    project = dict(id='p108', sourceManifest=pin(manifest), historicalKeilBytes=historical)
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        results = list(pool.map(lambda suite: one(config, project, suite), ('keil', 'mx251')))
    save(output/'comparison.json', dict(scope=scope, sourceManifest=pin(manifest),
         core=pin(core/'core.json'), results=results,
         ratioPercent=(100*results[1]['payloadBytes']/results[0]['payloadBytes'])
         if all(r['status'] == 'BUILT' for r in results) else None))
    assert all(r['status'] == 'BUILT' for r in results), results


def capture(hex_path, output, lengths):
    import serial
    from serial.tools import list_ports
    output.mkdir(parents=True, exist_ok=False)
    tool = REPO/'tools/stc_link1d/stc_link1d.py'
    device = next(d for d in json.loads(subprocess.check_output([sys.executable, str(tool), 'list']))
                  if d['product'] == 'STC-USB-Link1D')
    ports = [p for p in list_ports.comports()
             if p.serial_number == device['serial'] and p.location.endswith(':x.1')]
    assert len(ports) == 1
    flash = subprocess.run([sys.executable, str(tool), 'flash', '--hex', str(hex_path),
                           '--sha256', pin(hex_path)['sha256'], '--device-path', device['path'],
                           '--output', str(output/'flash')], capture_output=True)
    (output/'flash.log').write_bytes(flash.stdout+flash.stderr)
    assert flash.returncode == 0, flash.stderr
    uart = serial.Serial()
    uart.port, uart.baudrate, uart.timeout = ports[0].device, 115200, .1
    uart.dtr = uart.rts = False
    uart.open()
    cases = []
    try:
        # Startup text may finish before the serial handle opens; echo is the criterion.
        startup = bytearray()
        deadline = time.monotonic()+1
        while time.monotonic() < deadline:
            startup.extend(uart.read(256))
            if startup.endswith(b'TimeOut Test Programme!\r\n'):
                break
        (output/'startup.bin').write_bytes(startup)
        for index, length in enumerate(lengths):
            payload = bytes((i*73+index*19) & 255 for i in range(length))
            uart.reset_input_buffer()
            uart.write(payload)
            uart.flush()
            expected = f'cnt={length}\r\n'.encode('ascii')+payload
            received = bytearray()
            deadline = time.monotonic()+3
            while len(received) < len(expected) and time.monotonic() < deadline:
                received.extend(uart.read(len(expected)-len(received)))
            (output/f'{index:02}-tx.bin').write_bytes(payload)
            (output/f'{index:02}-rx.bin').write_bytes(received)
            cases.append(dict(length=length, received=len(received), passed=bytes(received) == expected))
            if not cases[-1]['passed']:
                break
    finally:
        uart.close()
    result = dict(status='PASS' if len(cases) == len(lengths) and all(c['passed'] for c in cases) else 'FAIL',
                  requestedLengths=lengths,
                  cases=cases, firmware=pin(hex_path), device=device, port=ports[0].device,
                  firmwareExecutionTime=None)
    save(output/'result.json', result)
    print(json.dumps({k: v for k, v in result.items() if k != 'device'}), flush=True)
    assert result['status'] == 'PASS'


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('build')
    p.add_argument('--config', type=Path, required=True)
    p.add_argument('--core', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--transport-fixture', action='store_true')
    p = sub.add_parser('capture')
    p.add_argument('--hex', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--lengths', default='1,7,31,255,256,511,17,513,1023')
    args = parser.parse_args()
    if args.command == 'build':
        build(args.config, args.core, args.output, args.transport_fixture)
    else:
        capture(args.hex, args.output, [int(n) for n in args.lengths.split(',')])
