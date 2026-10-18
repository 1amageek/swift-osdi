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


def manifest(source_url, source_revision, foundation_url, foundation_revision):
    return '''// swift-tools-version: 6.4
import PackageDescription

let package = Package(
    name: "OSDIConsumer",
    platforms: [.macOS(.v26)],
    dependencies: [
        .package(url: SOURCE_URL, revision: SOURCE_REVISION),
        .package(url: FOUNDATION_URL, revision: FOUNDATION_REVISION)
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
        'SOURCE_URL', json.dumps(source_url)).replace('SOURCE_REVISION', json.dumps(source_revision)).replace(
        'FOUNDATION_URL', json.dumps(foundation_url)).replace('FOUNDATION_REVISION', json.dumps(foundation_revision))


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
                      'swift-6.4.0-RELEASE' in str(args.swift))
    require((exact_version or tagged_release) and 'DEVELOPMENT' not in swift_version and
            '-dev' not in swift_version, 'Select the fixed Swift 6.4.0 release toolchain')
    source = runner.checkout(args.source_url, args.source_revision, 'source')
    foundation = runner.checkout(args.foundation_url, args.foundation_revision, 'foundation')
    source_manifest = (source / 'Package.swift').read_text()
    require(not re.search(r'\.package\s*\(\s*path\s*:', source_manifest) and
            not re.search(r'\.package\s*\(\s*path\s*:', (foundation / 'Package.swift').read_text()),
            'Source manifests must not contain local path dependencies')
    pins = re.findall(r'\.package\(url:\s*"([^"]*CircuiteFoundation(?:\.git)?)",\s*revision:\s*"([0-9a-f]{40})"', source_manifest)
    require(len(pins) == 1 and pins[0][1] == args.foundation_revision,
            'Consumer Foundation revision must exactly match the runtime manifest pin')
    foundation_original = pins[0][0]
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
    (consumer / 'Package.swift').write_text(manifest(args.source_url, args.source_revision,
                                                   foundation_original, args.foundation_revision))
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
    runner.run([args.swift, 'build', '--package-path', consumer, '--product', 'OSDIConsumer'])
    binary_dir = Path(runner.run([args.swift, 'build', '--package-path', consumer, '--show-bin-path']))
    # SwiftPM edits or symlinked sibling checkout sources would invalidate isolation.
    workspace = json.loads((consumer / '.build' / 'workspace-state.json').read_bytes())
    require(all(item.get('state', {}).get('name') != 'edited' for item in workspace.get('dependencies', [])),
            'Consumer contains an edited dependency')
    resolved = json.loads((consumer / 'Package.resolved').read_bytes())
    pins = {pin['identity']: pin['state']['revision'] for pin in resolved['pins']}
    require(pins.get(package_identity(args.source_url)) == args.source_revision and
            args.foundation_revision in pins.values(), 'Consumer resolved revisions differ')
    for path in (consumer / '.build' / 'checkouts').iterdir():
        require(path.resolve().is_relative_to(runner.scratch), 'Dependency source escapes scratch')
    staging = runner.scratch / 'staging'
    staging.mkdir()
    api_path = runner.scratch / 'api-report.json'
    runner.run([binary_dir / 'OSDIConsumer', *libraries, staging, api_path])
    api = round_trip(api_path)
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
            'source': {'url': args.source_url, 'revision': args.source_revision},
            'foundation': {'url': args.foundation_url, 'revision': args.foundation_revision},
            'dependencyResolution': {'mode': resolution_mode, 'foundationOriginalURL': foundation_original,
                                     'mirrors': mirrors, 'overrideCount': len(mirrors)},
            'swift': {'path': str(args.swift), 'sha256': sha(args.swift), 'version': swift_version},
            'clang': {'path': str(args.clang), 'sha256': sha(args.clang), 'version': clang_version},
            'host': {'system': platform.system(), 'machine': platform.machine(), 'version': platform.mac_ver()[0]},
            'environment': runner.env, 'clientExperimentalFlags': [], 'testableImports': False,
            'inputs': {'headerSHA256': sha(header), 'protocolSHA256': sha(protocol),
                       'runnerSHA256': sha(__file__), 'consumerManifestSHA256': sha(consumer / 'Package.swift'),
                       'consumerSources': {path.name: sha(path) for path in sources.glob('*.swift')}},
            'libraries': {path.name: sha(path) for path in libraries}, 'commands': runner.commands}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('source-url', 'source-revision', 'foundation-url', 'foundation-revision'):
        parser.add_argument('--' + name, required=True)
    parser.add_argument('--scratch', type=Path, required=True, help='New absolute directory outside the workspace')
    parser.add_argument('--swift', type=Path, required=True, help='Absolute Swift 6.4.0 release executable')
    parser.add_argument('--clang', type=Path, required=True, help='Absolute clang executable')
    parser.add_argument('--timeout-seconds', type=int, default=600, help='Per-command bound, 1...3600 seconds')
    args = parser.parse_args()
    for revision in (args.source_revision, args.foundation_revision):
        require(re.fullmatch('[0-9a-f]{40}', revision) is not None, 'Supply full lowercase Git revision IDs')
    require(1 <= args.timeout_seconds <= 3600, 'Invalid timeout bound')
    require(args.scratch.is_absolute() and not args.scratch.exists(), 'Scratch must be a new absolute directory')
    workspace = Path(__file__).resolve().parents[3]
    require(not args.scratch.resolve().is_relative_to(workspace), 'Scratch must be outside the source workspace')
    for tool in (args.swift, args.clang):
        require(tool.is_absolute() and tool.is_file() and os.access(tool, os.X_OK), 'Supply absolute executable tools')
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
