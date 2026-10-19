#!/usr/bin/env python3
"""Build and run a fresh, exact-revision public API consumer on macOS 26+ arm64."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import signal
import struct
import subprocess
import sys
import time
from urllib.parse import unquote, urlparse


HEADER_SHA256 = '0f3eeb9ef3c9f0bea9b3c59f34c632555e2367f6e28722e219d8c3f1d655a16f'


def require(condition, detail):
    if not condition:
        raise RuntimeError(detail)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def save(path, value):
    Path(path).write_text(json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + '\n')


class Runner:
    def __init__(self, scratch, swift, timeout):
        self.scratch = scratch
        self.timeout = timeout
        self.commands = []
        for name in ('home', 'tmp', 'logs'):
            (scratch / name).mkdir()
        self.env = {'HOME': str(scratch / 'home'), 'TMPDIR': str(scratch / 'tmp') + '/',
                    'PATH': str(swift.parent) + ':/usr/bin:/bin:/usr/sbin:/sbin',
                    'LANG': 'en_US.UTF-8', 'LC_ALL': 'en_US.UTF-8'}

    def run(self, arguments, cwd=None, expected=0):
        arguments = [str(value) for value in arguments]
        stem = self.scratch / 'logs' / f'{len(self.commands):03d}'
        record = {'arguments': arguments, 'cwd': str(cwd or self.scratch),
                  'stdout': str(stem.with_suffix('.stdout')), 'stderr': str(stem.with_suffix('.stderr'))}
        self.commands.append(record)
        start = time.monotonic()
        with open(record['stdout'], 'wb') as stdout, open(record['stderr'], 'wb') as stderr:
            child = subprocess.Popen(arguments, cwd=cwd or self.scratch, env=self.env,
                                     stdout=stdout, stderr=stderr, start_new_session=True)
            try:
                child.wait(timeout=self.timeout)
            except BaseException:
                try:
                    os.killpg(child.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                child.wait(timeout=5)
                record.update(exitCode=child.returncode, seconds=time.monotonic() - start)
                raise
        record.update(exitCode=child.returncode, seconds=time.monotonic() - start)
        require(child.returncode == expected,
                f'Expected exit {expected}, got {child.returncode}: {arguments}; see {record["stderr"]}')
        return Path(record['stdout']).read_text().strip()

    def checkout(self, url, revision, name):
        destination = self.scratch / name
        self.run(['/usr/bin/git', 'clone', '--no-local', '--no-checkout', '--', url, destination])
        self.run(['/usr/bin/git', '-C', destination, 'checkout', '--detach', revision])
        actual = self.run(['/usr/bin/git', '-C', destination, 'rev-parse', 'HEAD'])
        require(actual == revision, f'{name}: checkout identity differs')
        require(not self.run(['/usr/bin/git', '-C', destination, 'status', '--porcelain']),
                f'{name}: checkout is not clean')
        return destination


def package_identity(url):
    return unquote(urlparse(url).path).rstrip('/').rsplit('/', 1)[-1].removesuffix('.git').lower()


def is_local_git_url(url):
    parsed = urlparse(url)
    return ((parsed.scheme == 'file' and parsed.netloc in ('', 'localhost') and
             Path(unquote(parsed.path)).is_absolute()) or
            (not parsed.scheme and Path(url).is_absolute()))


def canonical_git_url(url):
    if is_local_git_url(url):
        parsed = urlparse(url)
        return Path(unquote(parsed.path) if parsed.scheme else url).resolve().as_uri().rstrip('/').removesuffix('.git')
    parsed = urlparse(url)
    return parsed._replace(scheme=parsed.scheme.lower(), netloc=parsed.netloc.lower(),
                           path=parsed.path.rstrip('/').removesuffix('.git')).geturl()


def foundation_binding(source, expected_revision):
    source_manifest = (source / 'Package.swift').read_text()
    declarations = re.findall(
        r'\.package\s*\(\s*url:\s*"([^"]*CircuiteFoundation(?:\.git)?)"\s*,\s*'
        r'(revision:\s*"[0-9a-f]{40}"|exact:\s*"[0-9]+\.[0-9]+\.[0-9]+"|'
        r'\.exact\s*\(\s*"[0-9]+\.[0-9]+\.[0-9]+"\s*\))\s*\)', source_manifest)
    require(len(declarations) == 1, 'Expected one exact-version or revision-only Foundation declaration')
    url, requirement = declarations[0]
    value = re.search(r'"([^"]+)"', requirement).group(1)
    version = None if requirement.startswith('revision:') else value
    resolved = json.loads((source / 'Package.resolved').read_bytes())
    pins = [pin for pin in resolved['pins'] if pin['identity'] == package_identity(url) and
            canonical_git_url(pin['location']) == canonical_git_url(url)]
    require(len(pins) == 1, 'Expected one tracked Foundation resolution matching its manifest URL')
    state = pins[0]['state']
    require(state['revision'] == expected_revision, 'Tracked Foundation commit differs from expected revision')
    if version is not None:
        require(state.get('version') == version, 'Tracked Foundation version differs from exact manifest version')
    else:
        require(value == expected_revision and state.get('version') is None,
                'Revision-only Foundation declaration differs from tracked resolution')
    return url, version


def manifest(source_url, source_revision, source_version, foundation_url, foundation_revision, foundation_version):
    return '''// swift-tools-version: 6.4
import PackageDescription

let package = Package(
    name: "OSDIConsumer",
    platforms: [.macOS(.v26)],
    dependencies: [
        .package(url: SOURCE_URL, SOURCE_REQUIREMENT),
        .package(url: FOUNDATION_URL, FOUNDATION_REQUIREMENT)
    ],
    targets: [.executableTarget(name: "OSDIConsumer", dependencies: [
        .product(name: "OSDIRuntime", package: SOURCE_IDENTITY),
        .product(name: "OSDIContracts", package: SOURCE_IDENTITY),
        .product(name: "CircuiteFoundation", package: "CircuiteFoundation"),
        .product(name: "CircuiteFoundationCrypto", package: "CircuiteFoundation"),
        .product(name: "CircuiteFoundationFoundation", package: "CircuiteFoundation"),
        .product(name: "CircuiteFoundationFileSystem", package: "CircuiteFoundation")
    ])],
    swiftLanguageModes: [.v6]
)
'''.replace('SOURCE_IDENTITY', json.dumps(package_identity(source_url))).replace(
        'SOURCE_URL', json.dumps(source_url)).replace('SOURCE_REQUIREMENT',
            ('exact: ' + json.dumps(source_version)) if source_version else ('revision: ' + json.dumps(source_revision))).replace(
        'FOUNDATION_URL', json.dumps(foundation_url)).replace('FOUNDATION_REQUIREMENT',
            ('exact: ' + json.dumps(foundation_version)) if foundation_version else ('revision: ' + json.dumps(foundation_revision)))


def request(library):
    return {'schemaVersion': 2, 'operation': 'evaluate', 'libraryPath': str(library),
            'expectedSHA256': sha(library), 'expectedByteCount': library.stat().st_size,
            'limits': {'maximumDescriptors': 16, 'maximumMetadataBytes': 16777216, 'maximumStorageBytes': 16777216},
            'readBudget': {'maximumPageByteCount': 65536, 'maximumTotalByteCount': 16777216,
                           'maximumPageCount': 4096, 'maximumWorkUnitCount': 8192,
                           'maximumDurationNanoseconds': 5000000000},
            'descriptorIndex': 0, 'profile': 'dynamic-point', 'parameters': [], 'simulationParameters': [],
            'temperatureKelvin': 300.15, 'connectedTerminalCount': 1,
            'evaluationLimits': {'maximumWorkspaceBytes': 8388608, 'maximumDiagnosticCount': 256,
                                 'maximumDiagnosticBytes': 65536, 'maximumStringBytes': 65536},
            'context': {'mode': 'transient', 'timeSeconds': 4}, 'localUnknowns': [3], 'previousState': [2, 7]}


def round_trip(path):
    value = json.loads(path.read_bytes())
    copy = path.with_name(path.stem + '-reencoded.json')
    save(copy, value)
    require(json.loads(copy.read_bytes()) == value, f'Persisted report equality failed: {path}')
    return value


def same_bits(actual, expected):
    return [struct.pack('>d', float(value)) for value in actual] == [
        struct.pack('>d', float(value)) for value in expected]


def verify(args, runner):
    require(platform.system() == 'Darwin' and platform.machine() == 'arm64' and
            int(platform.mac_ver()[0].split('.')[0]) >= 26, 'Requires macOS 26+ arm64')
    swift_version = runner.run([args.swift, '--version'])
    exact_version = re.search(r'\bSwift version 6\.4\.0(?:\s|\()', swift_version) is not None
    tagged_release = (re.search(r'\bSwift version 6\.4(?:\s|\()', swift_version) is not None and
                      'swift-6.4-RELEASE' in swift_version)
    require((exact_version or tagged_release) and 'DEVELOPMENT' not in swift_version and
            '-dev' not in swift_version, 'Select the fixed Swift 6.4.0 release toolchain')
    source = runner.checkout(args.source_url, args.source_revision, 'source')
    source_manifest = (source / 'Package.swift').read_text()
    foundation_original, foundation_version = foundation_binding(source, args.foundation_revision)
    require(not args.source_version or foundation_version is not None,
            'A stable source version cannot depend on a revision-only Foundation package; use an exact stable Foundation version')
    if args.source_version:
        tag_revision = runner.run(['/usr/bin/git', '-C', source, 'rev-parse', f'refs/tags/{args.source_version}^{{commit}}'])
        require(tag_revision == args.source_revision, 'Source version tag differs from expected source commit')
    foundation = runner.checkout(args.foundation_url, args.foundation_revision, 'foundation')
    if foundation_version:
        tag_revision = runner.run(['/usr/bin/git', '-C', foundation, 'rev-parse', f'refs/tags/{foundation_version}^{{commit}}'])
        require(tag_revision == args.foundation_revision, 'Foundation version tag differs from expected Foundation commit')
    require(not re.search(r'\.package\s*\(\s*path\s*:', source_manifest) and
            not re.search(r'\.package\s*\(\s*path\s*:', (foundation / 'Package.swift').read_text()),
            'Source manifests must not contain local path dependencies')
    mirrors = []
    if canonical_git_url(foundation_original) != canonical_git_url(args.foundation_url):
        require(is_local_git_url(args.foundation_url),
                'A different Foundation URL is permitted only for explicit local-availability preflight')
        mirrors.append({'original': foundation_original, 'mirror': args.foundation_url})
    resolution_mode = ('local-availability-preflight' if mirrors or is_local_git_url(args.source_url)
                       else 'public-remote-no-overrides')
    templates = source / 'Tools' / 'OSDIConsumerVerification'
    consumer = runner.scratch / 'consumer'
    sources = consumer / 'Sources' / 'OSDIConsumer'
    sources.mkdir(parents=True)
    for name in ('ConsumerEntry.swift', 'ConsumerSnapshot.swift', 'ConsumerFailure.swift'):
        shutil.copyfile(templates / name, sources / name)
    # Earlier ordinary-only source revisions retain their original templates.
    if (templates/'PrecisionConsumerProbe.swift').is_file():
        shutil.copyfile(templates/'PrecisionConsumerProbe.swift', sources/'PrecisionConsumerProbe.swift')
    require(not args.precision or (sources/'PrecisionConsumerProbe.swift').is_file(),
            '--precision requires a source revision with the public protocol-5 probe')
    (consumer / 'Package.swift').write_text(manifest(args.source_url, args.source_revision, args.source_version,
                                                   foundation_original, args.foundation_revision, foundation_version))
    for package in (source, consumer):
        require(not any((package / relative).exists() for relative in (
            '.swiftpm/configuration/mirrors.json', '.swiftpm/config/mirrors.json')),
            f'Existing SwiftPM mirror configuration is not permitted: {package}')
        for mirror in mirrors:
            runner.run([args.swift, 'package', '--package-path', package, 'config', 'set-mirror',
                        '--original', mirror['original'], '--mirror', mirror['mirror']])
    fixtures = runner.scratch / 'fixtures'
    fixtures.mkdir()
    header = source / 'Tools' / 'OSDIEvaluationVerification' / 'osdi_0_4.h'
    require(sha(header) == HEADER_SHA256, 'Tracked OSDI ABI header identity changed')
    protocol = source / 'Tests' / 'OSDIEvaluationTests' / 'Fixtures' / 'protocol.c'
    clang_version = runner.run([args.clang, '--version'])
    libraries = []
    for name, mode in [('state', 12), ('fatal', 1)]:
        library = fixtures / f'{name}.dylib'
        runner.run([args.clang, '-std=c11', '-D_DARWIN_C_SOURCE', '-dynamiclib', '-Wall', '-Wextra', '-Werror',
                    '-fvisibility=default', '-I', header.parent, f'-DPROTOCOL_MODE={mode}', protocol, '-o', library])
        libraries.append(library)
    precision_inputs = None
    precision_arguments = []
    if args.precision:
        numeric_dir = runner.scratch / 'numeric'
        runner.run([sys.executable, source/'Tools/OSDIPrecisionVerification/build_worker.py',
                    '--output-dir', numeric_dir, '--cc', args.clang, '--pkg-config', args.pkg_config])
        numeric = json.loads((numeric_dir/'receipt.json').read_bytes())
        precision_fixtures = runner.scratch/'precision-fixture'
        runner.run([sys.executable, templates/'precision_fixture.py', precision_fixtures])
        precision_library = precision_fixtures/'precision.dylib'
        runner.run([args.clang, '-std=c11', '-D_DARWIN_C_SOURCE', '-dynamiclib', '-Wall', '-Wextra', '-Werror',
                    '-fvisibility=default', '-I', header.parent, '-I', source/'Sources/COSDI/include',
                    '-I', precision_fixtures, templates/'precision_fixture.c', '-o', precision_library])
        precision_program = precision_fixtures/'linear.program'
        exported = json.loads((precision_fixtures/'manifest.json').read_bytes())
        require(exported['programSHA256'] == sha(precision_program) and
                precision_program.read_bytes()[40:72].hex() == exported['semanticProfileSHA256'],
                'Analytic source export content/semantic bindings differ')
        precision_report = runner.scratch/'precision-report.json'
        worker = Path(numeric['worker']['path'])
        profile = numeric['numericProfile']['sha256']
        require(sha(worker) == numeric['worker']['sha256'], 'Precision worker changed after build')
        precision_arguments = [precision_library, worker, profile, sha(precision_program), exported['semanticProfileSHA256'], precision_report]
        precision_inputs = {'fixtureSourceSHA256': sha(templates/'precision_fixture.c'),
                            'fixtureGeneratorSHA256': sha(templates/'precision_fixture.py'),
                            'librarySHA256': sha(precision_library), 'programSHA256': sha(precision_program),
                            'programByteCount': precision_program.stat().st_size,
                            'workerSHA256': sha(worker), 'numericProfileSHA256': profile,
                            'semanticProfileSHA256': exported['semanticProfileSHA256'],
                            'workerReceipt': {'path': str(numeric_dir/'receipt.json'), 'sha256': sha(numeric_dir/'receipt.json')}}
    runner.run([args.swift, 'build', '--package-path', consumer, '--product', 'OSDIConsumer'])
    binary_dir = Path(runner.run([args.swift, 'build', '--package-path', consumer, '--show-bin-path']))
    # SwiftPM edits or symlinked sibling checkout sources would invalidate isolation.
    workspace = json.loads((consumer / '.build' / 'workspace-state.json').read_bytes())
    require(all(item.get('state', {}).get('name') != 'edited' for item in workspace.get('dependencies', [])),
            'Consumer contains an edited dependency')
    resolved = json.loads((consumer / 'Package.resolved').read_bytes())
    pins = {pin['identity']: pin['state'] for pin in resolved['pins']}
    source_state = pins.get(package_identity(args.source_url), {})
    foundation_identity = package_identity(args.foundation_url if mirrors else foundation_original)
    foundation_state = pins.get(foundation_identity, {})
    require(source_state.get('revision') == args.source_revision and
            foundation_state.get('revision') == args.foundation_revision, 'Consumer resolved revisions differ')
    if args.source_version:
        require(source_state.get('version') == args.source_version, 'Consumer did not resolve the exact SDK version')
    if foundation_version:
        require(foundation_state.get('version') == foundation_version, 'Consumer did not resolve the exact Foundation version')
    for path in (consumer / '.build' / 'checkouts').iterdir():
        require(path.resolve().is_relative_to(runner.scratch), 'Dependency source escapes scratch')
    staging = runner.scratch / 'staging'
    staging.mkdir()
    api_path = runner.scratch / 'api-report.json'
    runner.run([binary_dir / 'OSDIConsumer', *libraries, staging, api_path, *precision_arguments])
    api = round_trip(api_path)
    if args.precision:
        observed = round_trip(precision_report)
        require(observed['programDigest'] == precision_inputs['programSHA256'] and
                observed['profileDigest'] == precision_inputs['numericProfileSHA256'] and
                observed['semanticDigest'] == precision_inputs['semanticProfileSHA256'] and
                re.fullmatch('[0-9a-f]{64}', observed['setupDigest']) is not None,
                'Public precision snapshot identities differ')
        require(observed['representatives'] == [4294967295] and observed['kinds'] == [1, 2] and
                observed['coefficients'] == [1, 1] and all(observed[key] is True for key in
                ('wrongOperationRefused', 'limitRefused', 'closedRefused')),
                'Public borrowed conditional identity/refusal observations differ')
        require(observed['programByteCount'] == precision_inputs['programByteCount'] and
                observed['setupByteCount'] > 0 and observed['cumulativeWork'] > 0 and
                observed['peakRetainedBytes'] >= observed['copiedRetainedBytes'] > 0 and
                observed['closedSessionRetainedBytes'] == observed['copiedRetainedBytes'] and
                observed['finalRetainedBytes'] == 0 and
                observed['controlledRefusals'] == ['work', 'storage', 'cancelled'],
                'Public controlled snapshot bytes, cumulative admission, lifetime or typed refusals differ')
        precision_inputs['publicReport'] = {'path': str(precision_report), 'sha256': sha(precision_report)}
    runner.run([args.swift, 'build', '--package-path', source, '--product', 'osdi-evaluate'])
    cli_dir = Path(runner.run([args.swift, 'build', '--package-path', source, '--show-bin-path']))
    for name, library, expected_exit in [('state', libraries[0], 0), ('fatal', libraries[1], 1)]:
        value = request(library)
        request_path = runner.scratch / f'{name}-request.json'
        report_path = runner.scratch / f'{name}-cli-report.json'
        save(request_path, value)
        runner.run([cli_dir / 'osdi-evaluate', '--request', request_path, '--output', report_path], expected=expected_exit)
        report = round_trip(report_path)
        require(report['requestBinding'] == {'sha256': sha(request_path), 'byteCount': request_path.stat().st_size},
                'CLI request binding differs')
        require(report['input'] == value and report['artifactBinding'] == {
            'sha256': sha(library), 'byteCount': library.stat().st_size}, 'CLI artifact or input binding differs')
        if expected_exit == 0:
            require(report['succeeded'] is True, 'CLI success report failed')
            for field in ('f', 'q', 'g', 'c', 'nextState'):
                require(same_bits(report['evaluation'][field], api[field]), f'API/CLI {field} differs')
            require(report['evaluation']['modelFlags'] == 0, 'Unexpected successful model flags')
        else:
            require(report['succeeded'] is False and report.get('evaluation') is None and
                    report['failure']['reason'] == 'native_evaluation' and report['failure']['modelFlags'] == 2,
                    'CLI fatal refusal contract differs')
    require(not list(staging.iterdir()), 'Public API staging was retained')
    require(not list((runner.scratch / 'tmp').glob('osdi-*')), 'CLI staging was retained')
    return {'status': 'passed', 'qualification': 'External public API/CLI integration and C ABI transport only',
            'source': {'url': args.source_url, 'revision': args.source_revision, 'version': args.source_version},
            'foundation': {'url': args.foundation_url, 'revision': args.foundation_revision, 'version': foundation_version},
            'dependencyResolution': {'mode': resolution_mode, 'foundationOriginalURL': foundation_original,
                                     'mirrors': mirrors, 'overrideCount': len(mirrors)},
            'swift': {'path': str(args.swift), 'sha256': sha(args.swift), 'version': swift_version},
            'clang': {'path': str(args.clang), 'sha256': sha(args.clang), 'version': clang_version},
            'host': {'system': platform.system(), 'machine': platform.machine(), 'version': platform.mac_ver()[0]},
            'environment': runner.env, 'clientExperimentalFlags': [], 'testableImports': False,
            'precisionSelected': args.precision, 'precision': precision_inputs,
            'inputs': {'headerSHA256': sha(header), 'protocolSHA256': sha(protocol),
                       'runnerSHA256': sha(__file__), 'consumerManifestSHA256': sha(consumer / 'Package.swift'),
                       'consumerSources': {path.name: sha(path) for path in sources.glob('*.swift')}},
            'libraries': {path.name: sha(path) for path in libraries}, 'commands': runner.commands}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('source-url', 'source-revision', 'foundation-url', 'foundation-revision'):
        parser.add_argument('--' + name, required=True)
    parser.add_argument('--source-version', help='Exact stable SDK version whose tag must match --source-revision')
    parser.add_argument('--scratch', type=Path, required=True, help='New absolute directory outside the workspace')
    parser.add_argument('--swift', type=Path, default=Path('swift'), help='Swift executable (default: PATH)')
    parser.add_argument('--clang', type=Path, required=True, help='Absolute clang executable')
    parser.add_argument('--precision', action='store_true', help='Require the public protocol-5 source-fact probe')
    parser.add_argument('--pkg-config', help='Numeric discovery executable for --precision (default: caller PATH)')
    parser.add_argument('--timeout-seconds', type=int, default=600, help='Per-command bound, 1...3600 seconds')
    args = parser.parse_args()
    for revision in (args.source_revision, args.foundation_revision):
        require(re.fullmatch('[0-9a-f]{40}', revision) is not None, 'Supply full lowercase Git revision IDs')
    if args.source_version:
        require(re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+', args.source_version) is not None,
                'Supply a stable source version with major.minor.patch components')
    require(1 <= args.timeout_seconds <= 3600, 'Invalid timeout bound')
    require(args.scratch.is_absolute() and not args.scratch.exists(), 'Scratch must be a new absolute directory')
    workspace = Path(__file__).resolve().parents[3]
    require(not args.scratch.resolve().is_relative_to(workspace), 'Scratch must be outside the source workspace')
    selected_swift = shutil.which(str(args.swift))
    require(selected_swift is not None, 'Swift executable not found: ' + str(args.swift))
    args.swift = Path(selected_swift).absolute()
    for tool in (args.swift, args.clang):
        require(tool.is_absolute() and tool.is_file() and os.access(tool, os.X_OK), 'Supply absolute executable tools')
    if args.precision:
        selected = shutil.which(args.pkg_config or 'pkg-config')
        require(selected is not None, '--precision requires installed GMP/MPFR and pkg-config')
        args.pkg_config = str(Path(selected).resolve(strict=True))
    args.scratch.mkdir(parents=True)
    runner = Runner(args.scratch.resolve(), args.swift, args.timeout_seconds)
    def interrupted(number, frame):
        raise KeyboardInterrupt(f'Interrupted by signal {number}')
    signal.signal(signal.SIGTERM, interrupted)
    try:
        receipt = verify(args, runner)
        save(runner.scratch / 'receipt.json', receipt)
    except BaseException as error:
        save(runner.scratch / 'failure.json', {'status': 'failed', 'error': str(error), 'commands': runner.commands})
        raise
    print(f'External consumer passed: {runner.scratch / "receipt.json"}')


if __name__ == '__main__':
    main()
