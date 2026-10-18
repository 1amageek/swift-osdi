#!/usr/bin/env python3
"""Compare fresh-process precision workers on the same bounded snapshot workload."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import signal
import statistics
import struct
import sys
import tempfile
import time

from verify_worker import Worker


HEAP_COLUMNS = ('malloc', 'calloc', 'realloc', 'free', 'requestedBytes')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def output_bits(rows):
    return b''.join(struct.pack('<' + 'd' * len(row), *row) for row in rows)


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def selected_families(snapshots, requested):
    available = snapshots['families']
    require(isinstance(available, dict) and available, 'Snapshot manifest has no families')
    selected = list(available) if requested is None else requested
    require(selected and len(selected) == len(set(selected))
            and all(name in available for name in selected),
            'Selected family names must be unique entries in the snapshot manifest')
    return selected


def validate_limits(point):
    # Worker helper fixes the admitted minimum at 128; every other limit is explicit.
    for key, minimum, maximum in (('precisionBits', 128, (1 << 32) - 1),
                                  ('arenaBytes', 1, sys.maxsize),
                                  ('maximumWork', 1, (1 << 64) - 1),
                                  ('work', 1, (1 << 64) - 1)):
        require(type(point[key]) is int and minimum <= point[key] <= maximum,
                f'Snapshot {key} is outside the supported control range')
    require(point['work'] <= point['maximumWork'], 'Recorded work exceeds the explicit work limit')


def save(path, report):
    # Only this invocation may replace the report it reserved with O_EXCL.
    descriptor, temporary = tempfile.mkstemp(prefix=path.name + '.', dir=path.parent)
    try:
        with os.fdopen(descriptor, 'w') as stream:
            json.dump(report, stream, sort_keys=True, indent=2, allow_nan=False)
            stream.write('\n')
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def measure(worker_path, point, tape, setup, expected, requests, probe, active_worker):
    started = time.perf_counter_ns()
    worker = Worker(worker_path, tape, setup_bytes=setup, arena=point['arenaBytes'],
                    work=point['maximumWork'], precision=point['precisionBits'],
                    allocation_probe=probe)
    active_worker[0] = worker
    try:
        require(worker.start() == 0, 'Worker startup failed')
        cold = time.perf_counter_ns() - started
        for target, context in zip((worker.control.candidate, worker.control.reference), point['contexts']):
            target.mode, target.high, target.low = context['mode'], context['high'], context['low']
        del target
        started = time.perf_counter_ns()
        first = worker.evaluate_vectors(point['vectors'])
        first_time = time.perf_counter_ns() - started
        first_diagnostics = (worker.control.usedBits, worker.control.work)
        results = [None] * requests
        diagnostics = [None] * requests
        started = time.perf_counter_ns()
        for index in range(requests):
            results[index] = worker.evaluate_vectors(point['vectors'])
            diagnostics[index] = (worker.control.usedBits, worker.control.work)
        elapsed = time.perf_counter_ns() - started

        # Compare every component of every output slab outside the timed interval.
        require(isinstance(first, list) and output_bits(first) == expected,
                'First request output differs from the supplied snapshot bits')
        require(all(isinstance(value, list) and output_bits(value) == expected for value in results),
                'Steady request output differs from the supplied snapshot bits')
        expected_diagnostics = (point['precisionBits'], point['work'])
        require(first_diagnostics == expected_diagnostics
                and all(value == expected_diagnostics for value in diagnostics),
                'Used precision or logical work differs from the snapshot')
        require(worker.control.arena == point['arenaBytes']
                and worker.control.maxWork == point['maximumWork']
                and worker.control.bits == worker.control.maxBits == point['precisionBits'],
                'Worker request limits changed')
        entry = {
            'coldCreateToReadyNanoseconds': cold,
            'firstRequestNanoseconds': first_time,
            'steadyTotalNanoseconds': elapsed,
            'steadyNanosecondsPerRequest': elapsed / requests,
            'steadyRequestsPerSecond': requests * 1e9 / elapsed,
            'allOutputsBitIdenticalToSnapshot': True,
            'allUsedPrecisionAndWorkUnchanged': True,
            'requestLimitsPreserved': True,
            'usedPrecisionBits': point['precisionBits'],
            'logicalWorkPerRequest': point['work'],
            'peakLiveHeapBytes': None,
            'heapAllocationLifetimes': None,
        }
        if probe:
            samples = worker.allocationSamples
            require(len(samples) == requests + 2, 'Incomplete allocation probe samples')
            entry['allocationProbe'] = {
                'columns': HEAP_COLUMNS,
                'startup': samples[0],
                'firstRequest': samples[1],
                'steadyPerRequest': samples[2:],
                'steadyTotal': [sum(row[index] for row in samples[2:]) for index in range(5)],
                'allEvaluationHeapCountersZero': all(not any(row) for row in samples[1:]),
            }
    finally:
        try:
            exit_code = worker.close()
        finally:
            active_worker[0] = None
    require(exit_code == 0, f'Worker exited with status {exit_code}')
    entry['workerExit'] = exit_code
    return entry


def spread(values):
    median = statistics.median(values)
    return {'median': median, 'minimum': min(values), 'maximum': max(values),
            'medianAbsoluteDeviation': statistics.median(abs(value - median) for value in values)}


def summarize(rounds):
    fields = ('coldCreateToReadyNanoseconds', 'firstRequestNanoseconds',
              'steadyNanosecondsPerRequest', 'steadyRequestsPerSecond')
    result = {variant: {field: spread([row[variant][field] for row in rounds]) for field in fields}
              for variant in ('before', 'after')}
    ratios = [row['before']['steadyNanosecondsPerRequest'] / row['after']['steadyNanosecondsPerRequest']
              for row in rounds]
    result['pairedBeforeOverAfterSteadyLatencyRatio'] = spread(ratios)
    result['pairedRatiosByRound'] = ratios
    result['ratioOfMedianSteadyLatencies'] = (result['before']['steadyNanosecondsPerRequest']['median']
                                             / result['after']['steadyNanosecondsPerRequest']['median'])
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before-worker', required=True, type=Path)
    parser.add_argument('--after-worker', required=True, type=Path)
    parser.add_argument('--snapshots-report', required=True, type=Path)
    parser.add_argument('--families', nargs='+', help='Explicit manifest entries; default includes every entry')
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--requests', default=10, type=int, help='Steady requests per fresh worker (default: 10)')
    parser.add_argument('--rounds', default=5, type=int, help='Paired rounds per family (default: 5)')
    parser.add_argument('--allocation-probe', type=Path)
    parser.add_argument('--timeout-seconds', default=600.0, type=float,
                        help='Deadline for the complete run, with up to five seconds for worker cleanup')
    args = parser.parse_args()
    require(args.requests > 0 and args.rounds > 0 and 0 < args.timeout_seconds < float('inf'),
            'Requests, rounds and timeout must be positive and finite')
    workers = {'before': args.before_worker.resolve(), 'after': args.after_worker.resolve()}
    probe = args.allocation_probe.resolve() if args.allocation_probe else None
    args.output = args.output.resolve()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    descriptor = os.open(args.output, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
    os.close(descriptor)
    active_worker = [None]

    def deadline(_signal, _frame):
        if active_worker[0] is not None:
            try:
                os.kill(active_worker[0].pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        raise TimeoutError('Complete benchmark deadline expired')

    report = {
        'schema': 1,
        'scope': 'Generic interval arithmetic on explicitly selected exact model programs at fixed raw points; '
                 'not full-circuit performance, latest-model qualification, or physical qualification.',
        'timingScope': 'Same Python pack/wake/read, output-list materialization and work/precision '
                       'diagnostic reads on every steady request. Optional allocation interposition '
                       'is identical for both workers. Complete output bit checks occur after timing.',
        'coldScope': 'Each variant/round uses a fresh process. Creation through ready includes slab '
                     'construction/copy, spawn and worker initialization. Input bytes are already staged; '
                     'machine or filesystem cache coldness is not asserted.',
        'memoryScope': 'Allocation probe reports request counters and requested bytes, not peak live '
                       'heap, allocation lifetimes, internal arena activity, or total zero-copy behavior. '
                       'Peak/lifetime values are unavailable and recorded as null.',
        'comparisonScope': 'Descriptive median, range, median absolute deviation and paired ratios; '
                           'no speed threshold or statistical significance is inferred.',
        'sourceSHA256': sha(Path(__file__)),
        'helperSHA256': sha(Path(__file__).with_name('verify_worker.py')),
        'python': sys.version,
        'machine': platform.machine(),
        'platform': platform.platform(),
        'driverConcurrency': 1,
        'requestsPerRound': args.requests,
        'roundsPerFamily': args.rounds,
        'timeoutSeconds': args.timeout_seconds,
        'families': {},
        'passed': False,
    }
    previous_handler = signal.signal(signal.SIGALRM, deadline)
    signal.setitimer(signal.ITIMER_REAL, args.timeout_seconds)
    try:
        report['workers'] = {key: {'path': str(path), 'sha256': sha(path)} for key, path in workers.items()}
        report['snapshotsReport'] = {'path': str(args.snapshots_report.resolve()), 'sha256': sha(args.snapshots_report)}
        if probe:
            report['allocationProbe'] = {'path': str(probe), 'sha256': sha(probe)}
        snapshots = json.loads(args.snapshots_report.read_text())
        families = selected_families(snapshots, args.families)
        report['selectedFamilies'] = families
        report['excludedFamilies'] = [name for name in snapshots['families'] if name not in families]
        staged = {}
        for family in families:
            point = snapshots['families'][family]
            validate_limits(point)
            require(isinstance(point['outputs'], list), f'{family}: snapshot has no successful output')
            program_path, setup_path = Path(point['programPath']), Path(point['setupPath'])
            tape, setup = program_path.read_bytes(), setup_path.read_bytes()
            require(hashlib.sha256(tape).hexdigest() == point['programSHA256']
                    and hashlib.sha256(setup).hexdigest() == point['setupSHA256'],
                    f'{family}: immutable program/setup identity differs')
            require(tape[40:72].hex() == point['semanticProfileSHA256'],
                    f'{family}: semantic profile identity differs')
            require(len(point['vectors']) == 4 and len(point['contexts']) == 2,
                    f'{family}: incomplete vectors or contexts')
            for high, low in ((point['vectors'][0], point['vectors'][1]),
                              (point['vectors'][2], point['vectors'][3])):
                require(len(high) == len(low) and all(h + l == h for h, l in zip(high, low)),
                        f'{family}: input pair is not normalized')
            expected = output_bits(point['outputs'])
            staged[family] = (point, tape, setup, expected)
            report['families'][family] = {
                'workload': {key: point[key] for key in (
                    'programPath', 'setupPath', 'programSHA256', 'setupSHA256', 'semanticProfileSHA256',
                    'precisionBits', 'arenaBytes', 'maximumWork', 'work', 'vectors', 'contexts')},
                'expectedOutputBitsSHA256': hashlib.sha256(expected).hexdigest(),
                'rounds': [],
            }
        save(args.output, report)
        for round_index in range(args.rounds):
            order = ('before', 'after') if round_index % 2 == 0 else ('after', 'before')
            for family in families:
                point, tape, setup, expected = staged[family]
                row = {'round': round_index + 1, 'order': order}
                report['families'][family]['rounds'].append(row)
                for variant in order:
                    row[variant] = measure(workers[variant], point, tape, setup, expected,
                                           args.requests, probe, active_worker)
                    save(args.output, report)
                print(json.dumps({'family': family, 'round': round_index + 1,
                                  'beforeSteadyNs': row['before']['steadyNanosecondsPerRequest'],
                                  'afterSteadyNs': row['after']['steadyNanosecondsPerRequest']}), flush=True)
        for family in families:
            report['families'][family]['summary'] = summarize(report['families'][family]['rounds'])
        for variant, path in workers.items():
            require(sha(path) == report['workers'][variant]['sha256'], 'Worker binary changed during measurement')
        require(sha(args.snapshots_report) == report['snapshotsReport']['sha256'],
                'Snapshot report changed during measurement')
        if probe:
            require(sha(probe) == report['allocationProbe']['sha256'], 'Allocation probe changed during measurement')
        report['passed'] = True
    except BaseException as error:
        report['failure'] = {'type': type(error).__name__, 'message': str(error)}
        raise
    finally:
        signal.setitimer(signal.ITIMER_REAL, 0)
        signal.signal(signal.SIGALRM, previous_handler)
        save(args.output, report)


if __name__ == '__main__':
    main()
