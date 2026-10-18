#!/usr/bin/env python3
"""Run bounded standalone native tests with explicit optional qualification lanes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import signal
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
MODEL_INPUTS = ('OSDI_OPENVAF_COMPILER', 'OSDI_BSIM_LIBRARY', 'OSDI_BSIM_SHA256',
                'OSDI_S4_FAMILIES_MANIFEST', 'OSDI_PRECISION_MODEL',
                'OSDI_PRECISION_SEMANTIC_SHA256', 'OSDI_PRECISION_WORKER',
                'OSDI_PRECISION_NUMERIC_PROFILE_SHA256',
                'OSDI_SOI101_PRECISION_DIRECTORY')
LEGACY_INPUTS = ('OSDI_REAL_PRECISION_REFERENCE', 'OSDI_REAL_PRECISION_INDEPENDENT_RECEIPT',
                 'OSDI_REAL_PRECISION_REQUEST_DIRECTORY')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def execute(argv, output, label, timeout, environment, commands, expected=0):
    started = time.monotonic()
    stdout_path, stderr_path = output / (label + '.stdout'), output / (label + '.stderr')
    timed_out = False
    with stdout_path.open('w') as stdout, stderr_path.open('w') as stderr:
        child = subprocess.Popen([str(value) for value in argv], cwd=ROOT, env=environment,
                                 stdout=stdout, stderr=stderr, start_new_session=True)
        try:
            child.wait(timeout=timeout)
        except BaseException as error:
            timed_out = isinstance(error, subprocess.TimeoutExpired)
            try:
                os.killpg(child.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(child.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                child.wait(timeout=5)
            if not timed_out:
                raise
    commands.append({'argv': [str(value) for value in argv], 'exitCode': child.returncode,
                     'expectedExitCode': expected,
                     'timedOut': timed_out, 'seconds': time.monotonic() - started,
                     'stdout': str(stdout_path), 'stdoutSHA256': digest(stdout_path),
                     'stderr': str(stderr_path), 'stderrSHA256': digest(stderr_path)})
    if timed_out or child.returncode != expected:
        raise RuntimeError(f'{label} failed (exit={child.returncode}, timeout={timed_out}); see {stderr_path}')
    return stdout_path.read_text()


def check_qualification_python_profile(output, environment, commands):
    tool = ROOT / 'Tools/OSDIPrecisionVerification/verify_soi101.py'
    execute([sys.executable, tool, '--help'], output, 'qualification-python-normal',
            30, environment, commands)
    label = 'qualification-python-optimized'
    execute([sys.executable, '-O', tool, '--help'], output, label,
            30, environment, commands, expected=1)
    if 'assertion-enabled Python' not in (output / (label + '.stderr')).read_text():
        raise RuntimeError('Optimized qualification must explicitly require assertion-enabled Python')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--swift', required=True, type=Path)
    parser.add_argument('--clang', default='/usr/bin/clang', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--configuration', choices=('debug', 'release'), default='debug')
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--timeout', '--timeout-seconds', type=int, default=600)
    parser.add_argument('--sanitize', choices=('address',))
    parser.add_argument('--precision', action='store_true')
    parser.add_argument('--model-qualification', action='store_true')
    parser.add_argument('--legacy-soi-qualification', action='store_true',
                        help='Also run the original legacy SOI four-family precision receipt regression')
    args = parser.parse_args()
    args.model_qualification = args.model_qualification or args.legacy_soi_qualification
    if args.jobs < 1 or args.timeout < 1:
        parser.error('--jobs and --timeout must be positive')
    if platform.system() != 'Darwin' or platform.machine() != 'arm64':
        parser.error('Standalone native verification requires macOS arm64')
    if int(platform.mac_ver()[0].split('.')[0]) < 26:
        parser.error('Standalone native verification requires macOS 26 or newer')
    # Driver dispatch depends on argv[0]; retain the selected symlink name.
    swift, clang = args.swift.absolute(), args.clang.absolute()
    resolved_swift, resolved_clang = swift.resolve(strict=True), clang.resolve(strict=True)
    for tool, resolved in ((swift, resolved_swift), (clang, resolved_clang)):
        if not resolved.is_file() or not os.access(tool, os.X_OK):
            parser.error('Selected tool must be an executable file: ' + str(tool))
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    environment = dict(os.environ)
    environment['OSDI_TEST_CLANG'] = str(clang)
    for key in ('OSDI_MODEL_QUALIFICATION', 'OSDI_PRECISION_QUALIFICATION', 'OSDI_LEGACY_SOI_QUALIFICATION', 'OSDI_STANDALONE_FIXTURES'):
        environment.pop(key, None)
    commands = []
    receipt = {'schema': 'osdi.standalone-verification.v1', 'status': 'failed',
               'configuration': args.configuration, 'sanitizer': args.sanitize,
               'precisionSelected': args.precision, 'modelQualificationSelected': args.model_qualification,
               'legacySOIQualificationSelected': args.legacy_soi_qualification,
               'commands': commands}
    try:
        version = execute([swift, '--version'], output, 'swift-version', 30, environment, commands)
        exact_version = re.search(r'\bSwift version 6\.4\.0(?:\s|\()', version) is not None
        tagged_release = (re.search(r'\bSwift version 6\.4(?:\s|\()', version) is not None
                          and any('swift-6.4.0-RELEASE' in str(path) for path in (swift, resolved_swift)))
        if not (exact_version or tagged_release) or 'DEVELOPMENT' in version:
            raise RuntimeError('Select the fixed Swift 6.4.0 release toolchain')
        receipt['swift'] = {'path': str(swift), 'resolvedPath': str(resolved_swift),
                            'sha256': digest(resolved_swift), 'version': version}
        receipt['clang'] = {'path': str(clang), 'resolvedPath': str(resolved_clang),
                            'sha256': digest(resolved_clang)}
        check_qualification_python_profile(output, environment, commands)
        execute([sys.executable, ROOT / 'Tools/OSDIStandaloneVerification/prepare.py',
                 '--output', output / 'fixtures', '--clang', clang], output, 'prepare', 120,
                environment, commands)
        environment['OSDI_STANDALONE_FIXTURES'] = str(output / 'fixtures/manifest.json')
        if args.precision:
            command = [sys.executable, ROOT / 'Tools/OSDIPrecisionVerification/build_worker.py',
                       '--output-dir', output / 'numeric', '--cc', clang]
            if args.sanitize:
                command.append('--sanitize')
            execute(command, output, 'numeric-build', 120, environment, commands)
            numeric = json.loads((output / 'numeric/receipt.json').read_text())
            execute([sys.executable, ROOT / 'Tools/OSDIPrecisionVerification/verify_range.py',
                     '--emit-fixtures', output / 'ranges'], output, 'range-inputs', 30, environment, commands)
            environment['OSDI_PRECISION_QUALIFICATION'] = '1'
            environment['OSDI_RANGE_FIXTURES'] = str(output / 'ranges')
            # Model receipts bind their supplied worker/profile; preserve those exact inputs.
            if not args.model_qualification:
                environment['OSDI_PRECISION_WORKER'] = numeric['worker']['path']
                environment['OSDI_PRECISION_NUMERIC_PROFILE_SHA256'] = numeric['numericProfile']['sha256']
        if args.model_qualification:
            required = MODEL_INPUTS + (LEGACY_INPUTS if args.legacy_soi_qualification else ())
            missing = [key for key in required if not environment.get(key)]
            if missing:
                raise RuntimeError('Requested model qualification requires: ' + ', '.join(missing))
            environment['OSDI_MODEL_QUALIFICATION'] = '1'
            if args.legacy_soi_qualification:
                environment['OSDI_LEGACY_SOI_QUALIFICATION'] = '1'
        if args.sanitize:
            environment['ASAN_OPTIONS'] = 'halt_on_error=1:abort_on_error=1:detect_leaks=0'
            environment['UBSAN_OPTIONS'] = 'halt_on_error=1:abort_on_error=1'
        command = [swift, 'test', '--force-resolved-versions', '--package-path', ROOT, '--scratch-path', output / 'build',
                   '--configuration', args.configuration, '--jobs', args.jobs]
        if args.sanitize:
            command += ['--sanitize', args.sanitize]
        execute(command, output, 'swift-test', args.timeout, environment, commands)
        receipt['status'] = 'passed'
    except Exception as error:
        receipt['error'] = str(error)
        raise
    finally:
        (output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps({'status': receipt['status'], 'receipt': str(output / 'receipt.json')}))


if __name__ == '__main__':
    def interrupted(signum, frame):
        raise KeyboardInterrupt(f'Standalone verification interrupted by signal {signum}')
    signal.signal(signal.SIGTERM, interrupted)
    main()
