#!/usr/bin/env python3
"""Build deterministic C protocol/transport inputs without external device models."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import signal
import subprocess

ROOT = Path(__file__).resolve().parents[2]
PROTOCOL_NAMES = ('state', 'fatal', 'finish', 'stop', 'diagnostics', 'diagnostic_overflow',
                  'logged_error', 'limiting', 'unknown_flags', 'nonfinite_output',
                  'negative_noise', 'nonfinite_noise', 'time_state_charge', 'no_charge',
                  'negative_zero_noise')
HEADER_SHA256 = '0f3eeb9ef3c9f0bea9b3c59f34c632555e2367f6e28722e219d8c3f1d655a16f'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(argv, output, label):
    child = subprocess.Popen([str(value) for value in argv], stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, text=True, start_new_session=True)
    try:
        stdout, stderr = child.communicate(timeout=30)
    except BaseException:
        try:
            os.killpg(child.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        child.communicate(timeout=5)
        raise
    (output / (label + '.stdout')).write_text(stdout)
    (output / (label + '.stderr')).write_text(stderr)
    if child.returncode:
        raise RuntimeError(f'{label} exited {child.returncode}: {stderr}')
    return {'argv': [str(value) for value in argv], 'exitCode': child.returncode,
            'stdout': stdout, 'stderr': stderr}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--clang', default='/usr/bin/clang', type=Path)
    args = parser.parse_args()
    if platform.system() != 'Darwin' or platform.machine() != 'arm64':
        raise RuntimeError('Standalone native verification requires macOS arm64.')
    header = ROOT / 'Sources/COSDI/ABI/osdi_0_4.h'
    if digest(header) != HEADER_SHA256:
        raise RuntimeError('Pinned OSDI 0.4 header identity mismatch.')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if (output / 'manifest.json').exists():
        raise RuntimeError('Use a fresh fixture output directory.')
    clang = args.clang.resolve(strict=True)
    fixtures = ROOT / 'Tests/OSDIEvaluationTests/Fixtures'
    commands = [run([clang, '--version'], output, 'clang-version')]
    libraries = {}
    for mode, name in enumerate(PROTOCOL_NAMES):
        library = output / ('protocol-' + name + '.dylib')
        commands.append(run([clang, '-std=c11', '-Wall', '-Wextra', '-Werror', '-dynamiclib',
                             '-I', header.parent, f'-DPROTOCOL_MODE={mode}',
                             fixtures / 'protocol.c', '-o', library], output, name))
        libraries[name] = {'path': str(library), 'sha256': digest(library), 'mode': mode}
    worker = output / 'precision-transport-worker'
    commands.append(run([clang, '-std=c11', '-Wall', '-Wextra', '-Werror',
                         '-I', ROOT / 'Sources/COSDI/include',
                         fixtures / 'precision_transport_worker.c', '-o', worker], output, 'transport'))
    manifest = {'schema': 'osdi.standalone-fixtures.v1', 'protocolLibraries': libraries,
                'transportWorker': {'path': str(worker), 'sha256': digest(worker)},
                'headerSHA256': digest(header), 'clang': {'path': str(clang), 'sha256': digest(clang)},
                'sources': {name: digest(fixtures / name) for name in ('protocol.c', 'precision_transport_worker.c')},
                'commands': commands}
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps({'manifest': str(output / 'manifest.json')}))


if __name__ == '__main__':
    def interrupted(signum, frame):
        raise KeyboardInterrupt(f'Fixture builder interrupted by signal {signum}')
    signal.signal(signal.SIGTERM, interrupted)
    main()
