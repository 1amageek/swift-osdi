#!/usr/bin/env python3
"""Compile the unchanged BSIM-BULK 107.2.1 source with full residuals and provenance."""
from argparse import ArgumentParser
from pathlib import Path
import json
import shutil
import signal
import time

from build import HEADER_SHA, digest, run, save

SOURCE_SHA = '4db89e3997e9eec1fd5949cf2751f415a483d37b61cdd37aabd1febfc5cb7fb8'


def main(args):
    started = time.monotonic()
    compiler = args.compiler.resolve(strict=True)
    source = args.source.resolve(strict=True)
    includes = args.includes.resolve(strict=True)
    header = args.header.resolve(strict=True)
    provenance = args.compiler_profile_receipt.resolve(strict=True)
    if digest(source) != SOURCE_SHA or digest(header) != HEADER_SHA:
        raise RuntimeError('Unchanged BSIM 107.2.1 source or pinned ABI header identity mismatch.')
    json.loads(provenance.read_text())
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    snapshots = output/'input-snapshots'
    snapshots.mkdir()
    shutil.copyfile(source, snapshots/'bsimbulk.va')
    shutil.copyfile(header, snapshots/'osdi_0_4.h')
    shutil.copyfile(provenance, snapshots/'compiler-profile-receipt.json')
    shutil.copytree(includes, snapshots/'includes')
    commands = []
    version = run([compiler, '--version'], output, 'compiler-version', commands)
    help_text = run([compiler, '--help'], output, 'compiler-help', commands)
    if '--full-residuals' not in help_text:
        raise RuntimeError('Compiler does not advertise --full-residuals.')
    library = output/'bsimbulk107-full-residuals.osdi'
    run([compiler, '--full-residuals', '-O', '1', '--target', 'aarch64-apple-darwin',
         '-I', snapshots/'includes', snapshots/'bsimbulk.va', '-o', library],
        output, 'compile-bsim', commands)
    # Verify the generated image without modifying its signed bytes.
    run(['/usr/bin/codesign', '--verify', '--verbose=2', library], output, 'verify-signature', commands)
    receipt = {'schema': 'osdi.full-residuals-artifact.v1', 'profile': 'full-residuals',
               'sourceSHA256': SOURCE_SHA, 'headerSHA256': HEADER_SHA,
               'includeSHA256': {str(path.relative_to(snapshots/'includes')): digest(path)
                                 for path in sorted((snapshots/'includes').rglob('*')) if path.is_file()},
               'compiler': {'path': str(compiler), 'sha256': digest(compiler), 'version': version.strip()},
               'compilerProfileReceiptSHA256': digest(provenance),
               'artifact': {'path': str(library), 'sha256': digest(library), 'byteCount': library.stat().st_size},
               'builderSHA256': digest(__file__), 'commands': commands,
               'seconds': time.monotonic()-started}
    save(output/'artifact-receipt.json', receipt)
    print(json.dumps(receipt['artifact']))


if __name__ == '__main__':
    parser = ArgumentParser(description=__doc__)
    for argument in ('compiler', 'source', 'includes', 'header', 'compiler-profile-receipt', 'output'):
        parser.add_argument('--'+argument, type=Path, required=True)
    def interrupted(signum, frame):
        raise KeyboardInterrupt(f'BSIM compiler helper interrupted by signal {signum}')
    signal.signal(signal.SIGTERM, interrupted)
    main(parser.parse_args())
