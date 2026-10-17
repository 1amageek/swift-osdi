#!/usr/bin/env python3
"""Build trusted-native OSDI metadata fixtures and optional actual OpenVAF models."""
from argparse import ArgumentParser
from hashlib import sha256
from pathlib import Path
import json
import os
import platform
import signal
import shutil
import subprocess

HEADER_SHA256 = '0f3eeb9ef3c9f0bea9b3c59f34c632555e2367f6e28722e219d8c3f1d655a16f'
FIXTURES = [
    'valid_two', 'extended_stride', 'wrong_major', 'wrong_minor',
    'missing_required_symbol', 'undersized_stride', 'excessive_descriptors',
    'excessive_nodes', 'invalid_mapping_offset', 'overflow_jacobian_count',
    'misaligned_mapping_offset', 'invalid_jacobian_node', 'invalid_terminal_count',
    'null_nodes', 'missing_eval', 'invalid_residual_offset',
    'misaligned_jacobian_pointer', 'null_jacobian_entries', 'excessive_storage',
    'valid_ground_pair', 'invalid_ground_first_node', 'missing_setup_model',
    'valid_overprovisioned_jacobian_capacity', 'underprovided_jacobian_capacity',
    'valid_capacity_above_final_jacobian_count', 'excessive_jacobian_capacity',
]


def digest(path):
    return sha256(Path(path).read_bytes()).hexdigest()


def run(arguments, receipts):
    child = subprocess.Popen([str(item) for item in arguments], stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, text=True, start_new_session=True)
    try:
        stdout, stderr = child.communicate(timeout=30)
    except BaseException:
        # Scope cleanup to this command's process group, including linker children.
        try:
            os.killpg(child.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        child.communicate(timeout=5)
        raise
    receipts.append({'arguments': [str(item) for item in arguments],
                     'exitCode': child.returncode, 'stdout': stdout, 'stderr': stderr})
    if child.returncode:
        raise RuntimeError(f'Command failed ({child.returncode}): {arguments}\n{stderr}')
    return stdout.strip()


def build(args):
    if platform.system() != 'Darwin' or platform.machine() != 'arm64':
        raise RuntimeError('These native qualification fixtures require macOS arm64.')
    header = args.header.resolve(strict=True)
    if digest(header) != HEADER_SHA256:
        raise RuntimeError('OSDI header SHA-256 does not match the pinned ABI source.')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    # A reused output would invalidate constructor/destructor ownership evidence.
    if (output/'manifest.json').exists():
        raise RuntimeError('Output already contains a fixture manifest; use a unique directory.')
    sources = Path(__file__).resolve().parents[2]/'Tests'/'OSDIRuntimeTests'/'Fixtures'
    include = output/'include'
    include.mkdir(exist_ok=True)
    shutil.copyfile(header, include/'osdi_0_4.h')
    clang = Path(shutil.which(str(args.clang)) or str(args.clang)).resolve(strict=True)
    receipts = []
    version = run([clang, '--version'], receipts)
    libraries = {}
    for kind, name in enumerate(FIXTURES):
        library = output/f'{name}.dylib'
        marker = output/f'{name}.lifetime'
        if marker.exists():
            raise RuntimeError(f'Lifetime marker already exists: {marker}')
        run([clang, '-std=c11', '-dynamiclib', '-Wall', '-Wextra', '-Werror',
             '-fvisibility=default', '-I', include, f'-DFIXTURE_KIND={kind}',
             '-DFIXTURE_MARKER='+json.dumps(str(marker)), sources/'metadata.c',
             '-o', library], receipts)
        libraries[name] = {'path': str(library), 'sha256': digest(library),
                           'lifetimeMarker': str(marker), 'kind': kind,
                           'qualification': 'ABI metadata/lifetime only; callbacks refuse evaluation'}
    library = output/'wrong_architecture.dylib'
    marker = output/'wrong_architecture.lifetime'
    if marker.exists():
        raise RuntimeError(f'Lifetime marker already exists: {marker}')
    run([clang, '-arch', 'x86_64', '-std=c11', '-dynamiclib', '-Wall', '-Wextra',
         '-Werror', '-fvisibility=default', '-I', include, '-DFIXTURE_KIND=0',
         '-DFIXTURE_MARKER='+json.dumps(str(marker)), sources/'metadata.c',
         '-o', library], receipts)
    libraries['wrong_architecture'] = {
        'path': str(library), 'sha256': digest(library), 'lifetimeMarker': str(marker),
        'kind': 0, 'qualification': 'Thin x86_64 pre-dlopen architecture refusal only'}
    models = {}
    compiler = None
    if args.openvaf:
        executable = args.openvaf.resolve(strict=True)
        compiler = {'path': str(executable), 'sha256': digest(executable),
                    'version': run([executable, '--version'], receipts)}
        for name in ('linear_rc', 'two_models'):
            source = sources/f'{name}.va'
            library = output/f'{name}.osdi'
            run([executable, source, '-O', '0', '-o', library], receipts)
            models[name] = {'path': str(library), 'sha256': digest(library),
                            'sourceSHA256': digest(source),
                            'qualification': 'Real Verilog-A F/Q compilation and S1 metadata admission only'}
    manifest = {
        'schemaVersion': 1, 'host': {'system': platform.system(), 'machine': platform.machine()},
        'header': {'path': str(header), 'sha256': HEADER_SHA256},
        'builder': {'path': str(Path(__file__).resolve()), 'sha256': digest(__file__)},
        'clang': {'path': str(clang), 'sha256': digest(clang), 'version': version},
        'openvaf': compiler,
        'sources': {source.name: digest(source) for source in sorted(sources.iterdir())
                    if source.suffix in ('.c', '.va')},
        'libraries': libraries, 'models': models, 'commands': receipts,
    }
    (output/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    print(json.dumps({'manifest': str(output/'manifest.json'),
                      'metadataLibraries': len(libraries), 'actualModels': len(models)}))


if __name__ == '__main__':
    parser = ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--clang', type=Path, required=True)
    parser.add_argument('--header', type=Path, required=True)
    parser.add_argument('--openvaf', type=Path)
    def interrupted(signum, frame):
        raise KeyboardInterrupt(f'Builder interrupted by signal {signum}')

    signal.signal(signal.SIGTERM, interrupted)
    build(parser.parse_args())
