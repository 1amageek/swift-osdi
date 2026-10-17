#!/usr/bin/env python3
"""Run the actual-source cleanup-custody regression with a test-only failing C close.

After the normal Native debug package build, run:
  python3 Tools/OSDIVerification/fault_probe.py --products .build/out/Products/Debug \
    --swiftc <pinned-swiftc> --clang <clang> --sdk <macOS-SDK> --output <fresh-directory>

This compiles selected Runtime sources against the already-built real Contracts and
CircuiteFoundation products. It does not link production COSDI or add a runtime hook.
Each compiler/linker/probe invocation has the existing builder's 30-second timeout.
"""
from argparse import ArgumentParser
from pathlib import Path
import json
import platform
import shutil
import signal

from build import digest, run

OWNER_FILES = (
    'OSDILibrary', 'OSDILibraryOwner', 'OSDIStorageLease',
    'OSDIPendingLibraryCleanup', 'OSDINativeIssue', 'OSDIError', 'NativeIssueBridge',
)


def probe(args):
    if platform.system() != 'Darwin' or platform.machine() != 'arm64':
        raise RuntimeError('This regression is qualified for Native macOS arm64 only.')
    root = Path(__file__).resolve().parents[2]
    fixtures = Path(__file__).resolve().parent/'FaultProbe'
    products = args.products.resolve(strict=True)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    report_path = output/'fault-probe.json'
    if report_path.exists():
        raise RuntimeError('Use a fresh output directory, without a prior fault-probe.json.')
    # Swift dispatches by argv[0]; retain the swiftc symlink name.
    swiftc = args.swiftc.absolute()
    if not swiftc.is_file():
        raise RuntimeError(f'Swift compiler is missing: {swiftc}')
    clang = args.clang.resolve(strict=True)
    sdk = args.sdk.resolve(strict=True)
    sources = [root/'Sources/OSDIRuntime/LibraryLoading'/f'{name}.swift' for name in OWNER_FILES]
    sources += [root/'Sources/OSDIRuntime/StableStorage/OSDIStorage.swift']
    objects = [products/'OSDIContracts.o', products/'CircuiteFoundation.o']
    for path in objects:
        if not path.is_file():
            raise RuntimeError(f'Required resolved Native build product is missing: {path}')
    modules = [path for name in ('OSDIContracts', 'CircuiteFoundation')
               for path in (products/f'{name}.swiftmodule').glob('*.swiftmodule') if path.is_file()]
    if len(modules) != 2:
        raise RuntimeError('Expected the two Native arm64 resolved Swift modules.')
    evidence_inputs = sources + objects + modules + [swiftc, clang, fixtures/'CleanupCustodyProbe.swift', fixtures/'fake.c',
        fixtures/'control.h', root/'Sources/COSDI/include/COSDI.h', Path(__file__).resolve(),
        Path(__file__).resolve().parent/'build.py']
    hashes = {str(path): digest(path) for path in evidence_inputs}
    receipts = []
    report = {'scope': 'Initial and nested retry cleanup custody only; fake Cclose, actual Runtime sources and real dependency products.',
              'status': 'running', 'inputSHA256': hashes, 'commands': receipts}
    try:
        report['swiftVersion'] = run([swiftc, '--version'], receipts)
        report['clangVersion'] = run([clang, '--version'], receipts)
        modules = output/'modules'
        modules.mkdir(exist_ok=True)
        control = modules/'FakeControl'
        control.mkdir(exist_ok=True)
        shutil.copyfile(fixtures/'control.h', control/'control.h')
        (control/'module.modulemap').write_text('module FakeControl { header "control.h" export * }\n')
        native = modules/'COSDI'
        native.mkdir(exist_ok=True)
        shutil.copyfile(root/'Sources/COSDI/include/COSDI.h', native/'COSDI.h')
        (native/'module.modulemap').write_text('module COSDI { header "COSDI.h" export * }\n')
        fake_object = output/'fake.o'
        run([clang, '-std=c11', '-Wall', '-Wextra', '-Werror', '-isysroot', sdk,
             '-I', root/'Sources/COSDI/include', '-c', fixtures/'fake.c', '-o', fake_object], receipts)
        executable = output/'cleanup-custody-probe'
        run([swiftc, '-parse-as-library', '-swift-version', '6', '-strict-concurrency=complete',
             '-module-name', 'OSDIRuntime', '-sdk', sdk, '-I', products, '-I', modules,
             '-enable-experimental-feature', 'Lifetimes', '-enable-experimental-feature', 'LifetimeDependence',
             *sources, fixtures/'CleanupCustodyProbe.swift', fake_object, *objects, '-o', executable], receipts)
        text = run([executable], receipts)
        for delegated in ('false', 'true'):
            expected = f'PASS delegated={delegated}: nested helper retry reached outer catch; same owner survived; cleanup succeeded'
            if expected not in text.splitlines():
                raise RuntimeError(f'Missing completed custody regression: {delegated}')
        report['inputsUnchanged'] = all(digest(path) == expected for path, expected in hashes.items())
        if not report['inputsUnchanged']:
            raise RuntimeError('A source or dependency product changed during the probe.')
        report['status'] = 'passed'
        report['executableSHA256'] = digest(executable)
        print(json.dumps({'status': 'passed', 'report': str(report_path)}))
    except BaseException as error:
        report['status'] = 'failed'
        report['failure'] = str(error)
        raise
    finally:
        report_path.write_text(json.dumps(report, indent=2)+'\n')


if __name__ == '__main__':
    parser = ArgumentParser(description=__doc__)
    parser.add_argument('--products', type=Path, required=True)
    parser.add_argument('--swiftc', type=Path, required=True)
    parser.add_argument('--clang', type=Path, required=True)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    def interrupted(signum, frame):
        raise KeyboardInterrupt(f'Fault probe interrupted by signal {signum}')
    signal.signal(signal.SIGTERM, interrupted)
    probe(parser.parse_args())
