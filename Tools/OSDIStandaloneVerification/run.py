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
import shutil
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
QUALIFICATION_SELECTORS = ('OSDI_MODEL_QUALIFICATION', 'OSDI_PRECISION_QUALIFICATION',
                          'OSDI_LEGACY_SOI_QUALIFICATION', 'OSDI_CONSTITUTIVE_ACTUAL_QUALIFICATION',
                          'OSDI_SIMULATION_REAL_QUALIFICATION')
GENERATED_FIXTURES = ('OSDI_STANDALONE_FIXTURES', 'OSDI_RANGE_FIXTURES', 'OSDI_CHART_FIXTURES',
                      'OSDI_DECISION_COVER_FIXTURES', 'OSDI_CONSTITUTIVE_FIXTURES',
                      'OSDI_SIMULATION_REAL_FIXTURES', 'OSDI_SIMULATION_REAL_LIBRARY', 'OSDI_RAW_GIVEN_LIBRARY')
TYPED_WORKER_BINDINGS = ('OSDI_SIMULATION_REAL_WORKER', 'OSDI_SIMULATION_REAL_NUMERIC_PROFILE_SHA256',
                         'OSDI_PRECISION_OLD_WORKER', 'OSDI_PRECISION_OLD_NUMERIC_PROFILE_SHA256')
LEGACY_REVISION = '2c9ee1f620406e7a479ac10a4fddfaa17bc23d6d'


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


def clear_qualification_inputs(environment):
    for key in QUALIFICATION_SELECTORS + GENERATED_FIXTURES + TYPED_WORKER_BINDINGS:
        environment.pop(key, None)


def select_sdk(output, environment, commands):
    selected = environment.get('SDKROOT')
    mode = 'caller' if selected else 'xcrun-macosx'
    if not selected:
        selected = execute(['/usr/bin/xcrun', '--sdk', 'macosx', '--show-sdk-path'],
                           output, 'sdk-selection', 30, environment, commands).strip()
    try:
        sdk = Path(selected).resolve(strict=True)
    except OSError as error:
        raise RuntimeError('Selected SDKROOT is unavailable') from error
    settings = next((sdk/name for name in ('SDKSettings.json', 'SDKSettings.plist') if (sdk/name).is_file()), None)
    if not sdk.is_dir() or settings is None:
        raise RuntimeError('Selected SDKROOT has no SDK settings identity')
    environment['SDKROOT'] = str(sdk)
    return {'path': str(sdk), 'selection': mode,
            'settings': {'path': str(settings), 'sha256': digest(settings)}}


def bind_precision_worker(environment, numeric, legacy_selected):
    # Only the legacy SOI receipt requires the caller's global worker/profile.
    if not legacy_selected:
        environment['OSDI_PRECISION_WORKER'] = numeric['worker']['path']
        environment['OSDI_PRECISION_NUMERIC_PROFILE_SHA256'] = numeric['numericProfile']['sha256']


def prepare_legacy_worker(output, clang, numeric, supplied, environment, commands):
    worker_key, profile_key = TYPED_WORKER_BINDINGS[2:]
    if supplied:
        if set(supplied) != {worker_key, profile_key}:
            raise RuntimeError('Both explicit old-worker inputs are required together')
        worker = Path(supplied[worker_key]).resolve(strict=True)
        profile = supplied[profile_key]
        if not worker.is_file() or not os.access(worker, os.X_OK) or not re.fullmatch('[0-9a-f]{64}', profile):
            raise RuntimeError('Invalid qualified old-worker artifact or numeric profile')
        return {'mode': 'caller-qualified-artifact', 'worker': {'path': str(worker), 'sha256': digest(worker)},
                'numericProfileSHA256': profile}
    source = output/'legacy-source'
    repository = 'https://github.com/1amageek/swift-osdi.git'
    git_environment = dict(environment)
    for key in list(git_environment):
        if key == 'GIT_CONFIG_COUNT' or key.startswith(('GIT_CONFIG_KEY_', 'GIT_CONFIG_VALUE_')):
            del git_environment[key]
    git_environment.update(GIT_CONFIG_NOSYSTEM='1', GIT_CONFIG_GLOBAL='/dev/null', GIT_TERMINAL_PROMPT='0')
    execute(['/usr/bin/git', 'clone', '--depth', '1', '--single-branch', '--branch', '1.1.0', repository, source],
            output, 'legacy-source-fetch', 120, git_environment, commands)
    revision = execute(['/usr/bin/git', '-C', source, 'rev-parse', 'HEAD'], output,
                       'legacy-source-revision', 30, git_environment, commands).strip()
    if revision != LEGACY_REVISION:
        raise RuntimeError('Public old-worker source tag differs from its qualified commit')
    execute([sys.executable, source/'Tools/OSDIPrecisionVerification/build_worker.py',
             '--output-dir', output/'legacy-numeric', '--cc', clang, '--sdk', numeric['sdk']],
            output, 'legacy-numeric-build', 120, environment, commands)
    old = json.loads((output/'legacy-numeric/receipt.json').read_text())
    def numeric_pins(receipt):
        return {entry['name']: (entry['header']['sha256'], entry['archive']['sha256'])
                for entry in receipt['buildSelection']['numeric']}
    if (numeric_pins(old) != numeric_pins(numeric) or
            old['compiler']['sha256'] != numeric['compiler']['sha256'] or
            old['buildSelection']['sdkSettings']['sha256'] != numeric['buildSelection']['sdkSettings']['sha256']):
        raise RuntimeError('Old-worker compiler/SDK/numeric inputs differ from the current worker')
    return {'mode': 'public-immutable-source', 'repository': repository, 'tag': '1.1.0', 'revision': revision,
            'worker': old['worker'], 'numericProfileSHA256': old['numericProfile']['sha256'],
            'buildReceipt': {'path': str(output/'legacy-numeric/receipt.json'),
                             'sha256': digest(output/'legacy-numeric/receipt.json')}}


def prepare_precision_inputs(output, clang, sanitized, environment, commands, numeric, legacy_worker=None):
    tools = ROOT / 'Tools/OSDIPrecisionVerification'
    generators = [('verify_range.py', ['--emit-fixtures', output/'ranges'], 'OSDI_RANGE_FIXTURES', 'ranges'),
                  ('chart_fixtures.py', [output/'charts'], 'OSDI_CHART_FIXTURES', 'charts'),
                  ('frontier_fixtures.py', [output/'covers'], 'OSDI_DECISION_COVER_FIXTURES', 'covers'),
                  ('constitutive_fixtures.py', [output/'identities', '--public-probes'], 'OSDI_CONSTITUTIVE_FIXTURES', 'identities'),
                  ('simulation_real_fixtures.py', [output/'simulation-real'], 'OSDI_SIMULATION_REAL_FIXTURES', 'simulation-real')]
    for name, arguments, _, label in generators:
        execute([sys.executable, tools/name, *arguments], output, label+'-inputs', 30, environment, commands)
    command = [sys.executable, tools/'verify_constitutive_identities.py', '--output-dir', output/'identity-proof',
               '--coefficient-bits', 4096, '--arena-bytes', 536870912, '--workspace-bytes', 134217728,
               '--host-memory-bytes', 1073741824, '--maximum-atoms', 4096, '--maximum-nodes', 1000000,
               '--work', 1000000000, '--steps', 1000000, '--case-timeout', 120, '--cc', clang, '--sdk', numeric['sdk']]
    if sanitized:command.append('--sanitize')
    execute(command, output, 'identity-source-check', 180, environment, commands)
    sys.path.insert(0,str(tools))
    try:
        from constitutive_fixtures import check_public_probe
        parity=check_public_probe(output/'identities',output/'identity-proof/fixtures')
    finally:sys.path.pop(0)
    library = output/'simulation-real/cold.dylib'
    execute([clang, '-isysroot', numeric['sdk'], '-dynamiclib', '-std=c11', '-O2',
             '-I', ROOT/'Sources/COSDI/ABI', '-I', ROOT/'Sources/COSDI/include', '-I', output/'simulation-real',
             ROOT/'Tools/OSDIConsumerVerification/precision_fixture.c', '-o', library],
            output, 'simulation-real-library', 30, environment, commands)
    raw_library = output/'simulation-real/raw-given.dylib'
    execute([clang, '-isysroot', numeric['sdk'], '-dynamiclib', '-std=c11', '-O2',
             '-I', ROOT/'Sources/COSDI/ABI', '-I', ROOT/'Sources/COSDI/include', '-I', output/'simulation-real',
             tools/'RawGivenSnapshotFixture.c', '-o', raw_library],
            output, 'raw-given-library', 30, environment, commands)
    old = prepare_legacy_worker(output, clang, numeric, legacy_worker, environment, commands)
    manifests={}
    selected = {}
    for _, _, selector, directory in generators:
        selected[selector]=str(output/directory)
        path=output/directory/'manifest.json';manifests[selector]={'path':str(path),'sha256':digest(path)}
    selected.update(OSDI_SIMULATION_REAL_QUALIFICATION='1', OSDI_SIMULATION_REAL_LIBRARY=str(library),
        OSDI_RAW_GIVEN_LIBRARY=str(raw_library),
        OSDI_SIMULATION_REAL_WORKER=numeric['worker']['path'],
        OSDI_SIMULATION_REAL_NUMERIC_PROFILE_SHA256=numeric['numericProfile']['sha256'],
        OSDI_PRECISION_OLD_WORKER=old['worker']['path'],
        OSDI_PRECISION_OLD_NUMERIC_PROFILE_SHA256=old['numericProfileSHA256'])
    environment.update(selected)
    proof=output/'identity-proof/receipt.json'
    return {'fixtureManifests':manifests,'publicProbeExactCParity':parity,
            'independentSourceCheck':{'path':str(proof),'sha256':digest(proof)},'actualModelsSelected':False,
            'typedSimulationReal': {'required': True, 'library': {'path': str(library), 'sha256': digest(library)},
                'rawGivenLibrary': {'path': str(raw_library), 'sha256': digest(raw_library)},
                'worker': numeric['worker'], 'numericProfile': numeric['numericProfile'], 'legacyWorker': old}}



def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--swift', default=Path('swift'), type=Path, help='Swift executable (default: PATH)')
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
    selected_swift = shutil.which(str(args.swift))
    if selected_swift is None:
        parser.error('Swift executable not found: ' + str(args.swift))
    swift, clang = Path(selected_swift).absolute(), args.clang.absolute()
    resolved_swift, resolved_clang = swift.resolve(strict=True), clang.resolve(strict=True)
    for tool, resolved in ((swift, resolved_swift), (clang, resolved_clang)):
        if not resolved.is_file() or not os.access(tool, os.X_OK):
            parser.error('Selected tool must be an executable file: ' + str(tool))
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    environment = dict(os.environ)
    environment['OSDI_TEST_CLANG'] = str(clang)
    legacy_worker = {key: environment[key] for key in TYPED_WORKER_BINDINGS[2:] if environment.get(key)} if args.precision else None
    clear_qualification_inputs(environment)
    if args.sanitize:
        environment['ASAN_OPTIONS'] = 'halt_on_error=1:abort_on_error=1:detect_leaks=0'
        environment['UBSAN_OPTIONS'] = 'halt_on_error=1:abort_on_error=1'
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
                          and 'swift-6.4-RELEASE' in version)
        if not (exact_version or tagged_release) or 'DEVELOPMENT' in version:
            raise RuntimeError('Select the fixed Swift 6.4.0 release toolchain')
        receipt['swift'] = {'path': str(swift), 'resolvedPath': str(resolved_swift),
                            'sha256': digest(resolved_swift), 'version': version}
        receipt['clang'] = {'path': str(clang), 'resolvedPath': str(resolved_clang),
                            'sha256': digest(resolved_clang)}
        receipt['sdk'] = select_sdk(output, environment, commands)
        check_qualification_python_profile(output, environment, commands)
        execute([sys.executable, ROOT / 'Tools/OSDIStandaloneVerification/prepare.py',
                 '--output', output / 'fixtures', '--clang', clang], output, 'prepare', 120,
                environment, commands)
        environment['OSDI_STANDALONE_FIXTURES'] = str(output / 'fixtures/manifest.json')
        if args.precision:
            command = [sys.executable, ROOT / 'Tools/OSDIPrecisionVerification/build_worker.py',
                       '--output-dir', output / 'numeric', '--cc', clang, '--sdk', receipt['sdk']['path']]
            if args.sanitize:
                command.append('--sanitize')
            execute(command, output, 'numeric-build', 120, environment, commands)
            numeric = json.loads((output / 'numeric/receipt.json').read_text())
            receipt['precisionInputs']=prepare_precision_inputs(output,clang,args.sanitize,environment,commands,numeric,legacy_worker)
            environment['OSDI_PRECISION_QUALIFICATION'] = '1'
            bind_precision_worker(environment, numeric, args.legacy_soi_qualification)
        if args.model_qualification:
            required = MODEL_INPUTS + (LEGACY_INPUTS if args.legacy_soi_qualification else ())
            missing = [key for key in required if not environment.get(key)]
            if missing:
                raise RuntimeError('Requested model qualification requires: ' + ', '.join(missing))
            environment['OSDI_MODEL_QUALIFICATION'] = '1'
            if args.legacy_soi_qualification:
                environment['OSDI_LEGACY_SOI_QUALIFICATION'] = '1'
        command = [swift, 'test', '--force-resolved-versions', '--package-path', ROOT, '--scratch-path', output / 'build',
                   '--configuration', args.configuration, '--jobs', args.jobs]
        if args.sanitize:
            command += ['--sanitize', args.sanitize]
        execute(command, output, 'swift-test', args.timeout, environment, commands)
        if digest(Path(receipt['sdk']['settings']['path'])) != receipt['sdk']['settings']['sha256']:
            raise RuntimeError('Selected SDK settings changed during qualification')
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
