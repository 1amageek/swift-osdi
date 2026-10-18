#!/usr/bin/env python3
"""Measure private-arena behavior through the exact production evaluator."""
import argparse
import hashlib
import json
from pathlib import Path
import signal
import struct
import subprocess
import tempfile

from verify_worker import Control
from benchmark_models import output_bits, require, save, selected_families, sha, validate_limits


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--measure-program', required=True, type=Path)
    parser.add_argument('--snapshots-report', required=True, type=Path)
    parser.add_argument('--families', nargs='+', help='Explicit manifest entries; default includes every entry')
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--requests', default=100, type=int)
    parser.add_argument('--timeout-seconds', default=300.0, type=float)
    args = parser.parse_args()
    require(0 < args.requests <= 100000 and 0 < args.timeout_seconds < float('inf'),
            'Invalid request count or timeout')
    args.measure_program = args.measure_program.resolve()
    args.output = args.output.resolve()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('x'):
        pass
    report = {'schema': 1, 'passed': False, 'families': {},
              'scope': 'Untimed direct production program evaluation of explicitly selected exact model '
                       'snapshots, using the same arithmetic/identity/arena sources as the worker. '
                       'This does not measure IPC latency, latest-model or physical qualification.',
              'counterScope': 'Arena allocations, reallocations, frees and realloc-copy bytes; '
                              'live/peak requested payload bytes exclude block headers and unused '
                              'reserved capacity. Other explicit data copies and OS allocations '
                              'are outside these counters. Cache release precedes bulk arena close; '
                              'remaining live claims are invalidated by close, not individually freed.',
              'sourceSHA256': sha(Path(__file__)),
              'measureProgram': {'path': str(args.measure_program), 'sha256': sha(args.measure_program)},
              'snapshotsReport': {'path': str(args.snapshots_report.resolve()), 'sha256': sha(args.snapshots_report)},
              'steadyRequests': args.requests, 'timeoutSeconds': args.timeout_seconds}

    def expired(_signal, _frame):
        raise TimeoutError('Complete private-arena measurement deadline expired')

    previous = signal.signal(signal.SIGALRM, expired)
    signal.setitimer(signal.ITIMER_REAL, args.timeout_seconds)
    try:
        snapshots = json.loads(args.snapshots_report.read_text())
        families = selected_families(snapshots, args.families)
        report['selectedFamilies'] = families
        report['excludedFamilies'] = [name for name in snapshots['families'] if name not in families]
        with tempfile.TemporaryDirectory(prefix='osdi-arena-') as temporary:
            directory = Path(temporary)
            for family_index, family in enumerate(families):
                point = snapshots['families'][family]
                validate_limits(point)
                tape = Path(point['programPath']).read_bytes()
                setup = Path(point['setupPath']).read_bytes()
                require(hashlib.sha256(tape).hexdigest() == point['programSHA256']
                        and hashlib.sha256(setup).hexdigest() == point['setupSHA256'],
                        f'{family}: source bytes differ from snapshot identities')
                require(tape[40:72].hex() == point['semanticProfileSHA256'],
                        f'{family}: semantic profile identity differs')
                control = Control()
                control.raw = struct.unpack_from('<I', tape, 32)[0]
                control.descriptor = struct.unpack_from('<I', tape, 28)[0]
                control.active = struct.unpack_from('<I', setup, 28)[0]
                control.arena = point['arenaBytes']
                control.minBits = 128
                control.maxBits = control.bits = point['precisionBits']
                control.maxWork = point['maximumWork']
                require(len(point['contexts']) == 2 and len(point['vectors']) == 4
                        and all(len(row) == control.active for row in point['vectors']),
                        f'{family}: incomplete request')
                for target, context in zip((control.candidate, control.reference), point['contexts']):
                    target.mode, target.high, target.low = context['mode'], context['high'], context['low']
                del target
                expected = output_bits(point['outputs'])
                paths = [directory / (str(family_index) + suffix) for suffix in
                         ('.program', '.setup', '.request', '.expected', '.output')]
                for path, payload in zip(paths, (tape, setup, bytes(control) + output_bits(point['vectors']), expected)):
                    path.write_bytes(payload)
                command = [str(args.measure_program), *(str(path) for path in paths),
                           str(args.requests), str(point['work'])]
                execution = subprocess.run(command, text=True, capture_output=True,
                                           timeout=args.timeout_seconds)
                entry = {'programSHA256': point['programSHA256'], 'setupSHA256': point['setupSHA256'],
                         'semanticProfileSHA256': point['semanticProfileSHA256'],
                         'precisionBits': point['precisionBits'], 'arenaBytes': point['arenaBytes'],
                         'maximumWork': point['maximumWork'], 'logicalWorkPerRequest': point['work'],
                         'expectedOutputBitsSHA256': hashlib.sha256(expected).hexdigest(),
                         'exitCode': execution.returncode, 'stderr': execution.stderr,
                         'stdout': execution.stdout}
                report['families'][family] = entry
                save(args.output, report)
                require(execution.returncode == 0, f'{family}: measurement process failed')
                actual = paths[4].read_bytes()
                require(actual == expected, f'{family}: final complete output bits differ')
                entry['actualOutputBitsSHA256'] = hashlib.sha256(actual).hexdigest()
                entry['measurement'] = json.loads(execution.stdout)
                require(entry['measurement']['allOutputsBitIdentical']
                        and entry['measurement']['arenaReleased'], f'{family}: incomplete measurement')
                require(all(entry['measurement'][key] == entry[key] for key in
                            ('precisionBits', 'arenaBytes', 'maximumWork', 'logicalWorkPerRequest')),
                        f'{family}: measured limits or work differ from the explicit snapshot')
                counters = entry['measurement']['snapshots']
                entry['deltas'] = {}
                for name, before, after in (('firstRequest', 'prepared', 'firstRequest'),
                                            ('steadyRequests', 'firstRequest', 'steadyRequests')):
                    entry['deltas'][name] = {key: counters[after][key] - counters[before][key]
                                            for key in ('allocations', 'reallocations', 'frees', 'copiedBytes', 'liveBytes')}
                    entry['deltas'][name]['peakBytesAfter'] = counters[after]['peakBytes']
                save(args.output, report)
                print(json.dumps({'family': family, 'deltas': entry['deltas']}), flush=True)
        require(sha(args.measure_program) == report['measureProgram']['sha256']
                and sha(args.snapshots_report) == report['snapshotsReport']['sha256'],
                'Measurement executable or snapshot report changed during execution')
        report['passed'] = True
    except BaseException as error:
        report['failure'] = {'type': type(error).__name__, 'message': str(error)}
        raise
    finally:
        signal.setitimer(signal.ITIMER_REAL, 0)
        signal.signal(signal.SIGALRM, previous)
        save(args.output, report)


if __name__ == '__main__':
    main()
