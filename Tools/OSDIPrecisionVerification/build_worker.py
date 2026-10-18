#!/usr/bin/env python3
"""Build and identify the isolated precision worker using installed static archives."""
import argparse
import hashlib
import json
import pathlib
import platform
import re
import subprocess
import native_build

ROOT = pathlib.Path(__file__).resolve().parents[2]

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output-dir', required=True, type=pathlib.Path)
    parser.add_argument('--sanitize', action='store_true')
    native_build.arguments(parser)
    args = parser.parse_args()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    selected = native_build.resolve(args)
    compiler, sdk = selected['compiler'], selected['sdk']
    mpfr, gmp = selected['numeric']['mpfr'], selected['numeric']['gmp']
    sources = sorted((ROOT / 'Sources/COSDIPrecision').rglob('*.c')) + [ROOT / 'Sources/OSDIPrecisionWorker/WorkerEntry.c']
    headers = sorted((ROOT / 'Sources/COSDIPrecision').rglob('*.h')) + [ROOT / 'Sources/COSDI/include/OSDICompilerPrecision.h', ROOT / 'Sources/COSDI/include/OSDIPrecisionProtocol.h', mpfr['header'], gmp['header']]
    archives = [mpfr['archive'], gmp['archive']]
    worker = output / 'osdi-precision-worker'
    command = [str(compiler), '-isysroot', str(sdk), '-std=c11', '-O2', '-g', '-Wall', '-Wextra', '-Werror', '-fno-fast-math', '-ffp-contract=off']
    if args.sanitize:
        command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    for path in [ROOT / 'Sources/COSDIPrecision/include', ROOT / 'Sources/COSDI/include', mpfr['include'], gmp['include']]:
        command += ['-I', str(path)]
    command += [str(path) for path in sources + archives] + ['-o', str(worker)]
    before = {str(p): digest(p) for p in sources + headers + archives}
    build_tools = [native_build.identity(__file__), native_build.identity(native_build.__file__)]
    subprocess.run(command, check=True, timeout=60)
    native_build.unchanged(selected['identities'] + build_tools)
    if before != {str(p): digest(p) for p in sources + headers + archives}:
        raise RuntimeError("Build inputs changed while the worker was compiling")
    libraries = subprocess.check_output(['otool', '-L', str(worker)], text=True, timeout=10)
    if not args.sanitize:
        for line in libraries.splitlines()[1:]:
            name = line.strip().split(' ')[0]
            if not name.startswith(('/usr/lib/', '/System/Library/')):
                raise RuntimeError('Qualified static worker has an unexpected dynamic dependency: ' + name)
    receipt = {'schema': 1, 'profile': 'native-stateless-directed-mpfr-static-v1',
        'sanitizers': args.sanitize, 'sdk': str(sdk), 'platform': platform.platform(), 'architecture': platform.machine(),
        'compiler': {'path': str(compiler), 'sha256': digest(compiler), 'version': subprocess.check_output([str(compiler), '--version'], text=True, timeout=10)},
        'buildSelection': selected['selection'],
        'buildTools': build_tools,
        'command': command, 'sources': [{'path': str(p), 'sha256': digest(p)} for p in sources + headers],
        'numericArchives': [{'path': str(p), 'sha256': digest(p)} for p in archives],
        'worker': {'path': str(worker), 'sha256': digest(worker)}, 'dynamicDependencies': libraries}
    mpfr_header = mpfr['header'].read_text()
    gmp_header = gmp['header'].read_text()
    mpfr_version = re.search(r'#define MPFR_VERSION_STRING\s+"([^"]+)"', mpfr_header).group(1)
    gmp_version = '.'.join(re.search(r'#define __GNU_MP_VERSION' + suffix + r'\s+(\d+)', gmp_header).group(1) for suffix in ['', '_MINOR', '_PATCHLEVEL'])
    profile = {'schema': 1, 'numericVersion': 'directed-mpfr-stateless-v1', 'wireVersion': 1, 'rangeWireVersion': 2, 'rangeSemantics': 'source-program-smooth-box-v2',
        'chartWireVersion': 3, 'chartSemantics': 'source-program-conditional-formula-v3',
        'chartWorkPolicy': 'session-cumulative-work-and-source-steps',
        'coverWireVersion': 4, 'coverSemantics': 'source-program-complete-decision-cover-v4',
        'coverWorkPolicy': 'session-cumulative-work-steps-guard-records-and-explicit-cover-transport-copied-bytes',
        'coverAuthority': 'independent-source-replay-checked-finite-decision-partitions-and-terminal-formulas-only', 'programVersion': 1,
        'targetABI': subprocess.check_output([str(compiler), '-dumpmachine'], text=True, timeout=10).strip(),
        'compiler': {'sha256': digest(compiler), 'version': '\n'.join(line for line in receipt['compiler']['version'].splitlines() if not line.startswith('InstalledDir:'))},
        'semanticFlags': ['-std=c11', '-O2', '-fno-fast-math', '-ffp-contract=off'],
        'sanitizers': args.sanitize, 'sdk': sdk.name,
        'numericLibraries': {'mpfr': {'version': mpfr_version, 'archiveSha256': digest(archives[0]), 'headerSha256': digest(mpfr['header'])},
            'gmp': {'version': gmp_version, 'archiveSha256': digest(archives[1]), 'headerSha256': digest(gmp['header'])}},
        'sources': [{'path': str(p.relative_to(ROOT)), 'sha256': digest(p)} for p in sources + [p for p in headers if p.is_relative_to(ROOT)]],
        'worker': {'sha256': receipt['worker']['sha256']}}
    profile_path = output / 'numeric-profile.json'
    profile_path.write_bytes(json.dumps(profile, sort_keys=True, separators=(',', ':'), ensure_ascii=False).encode('utf-8'))
    receipt['numericProfile'] = {'path': str(profile_path), 'sha256': digest(profile_path)}
    path = output / 'receipt.json' 
    path.write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps({'worker': str(worker), 'receipt': str(path), 'workerSha256': receipt['worker']['sha256'], 'numericProfile': str(profile_path), 'numericProfileSha256': digest(profile_path)}))

if __name__ == '__main__':
    main()
