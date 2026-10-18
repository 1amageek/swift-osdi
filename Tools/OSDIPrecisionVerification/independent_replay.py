#!/usr/bin/env python3
"""Source-reproducible independent generic precision oracle and bounded replay.

Copyright (c) 2026 1amageek. Package license applies.
Adapted from owned CoreSpice verification tools; see oracle-provenance.json.
"""
import argparse
from fractions import Fraction
import hashlib
import json
import math
import os
from pathlib import Path
import resource
import shutil
import signal
import struct
import subprocess
import time
import native_build

HERE = Path(__file__).resolve().parent
HEADER = HERE.parents[1] / 'Sources/COSDI/include/OSDICompilerPrecision.h'
MAX_INPUT = 64 * 1024 * 1024
MAX_OUTPUT = 4 * 1024 * 1024

def identity(path):
    path = Path(path).resolve(strict=True)
    if path.stat().st_size > MAX_INPUT:
        raise ValueError('input_size')
    return {'path': str(path), 'bytes': path.stat().st_size,
            'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}


def write_json(path, value):
    path = Path(path)
    data = (json.dumps(value, indent=2, allow_nan=False) + '\n').encode()
    if len(data) > MAX_OUTPUT:
        raise ValueError('receipt_size')
    with path.open('xb') as stream:
        stream.write(data)


def run(argv, directory, stem, timeout=30):
    """Files bound child output; no pipe accumulation or shell execution."""
    directory = Path(directory)
    out, err = directory / (stem + '.stdout'), directory / (stem + '.stderr')
    def limits():
        resource.setrlimit(resource.RLIMIT_CPU, (timeout, timeout))
        resource.setrlimit(resource.RLIMIT_FSIZE, (MAX_OUTPUT, MAX_OUTPUT))
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    if shutil.disk_usage(directory).free < 64*1024*1024:
        raise RuntimeError('insufficient_validation_output_reserve')
    started = time.monotonic()
    with out.open('xb') as stdout, err.open('xb') as stderr:
        child = subprocess.Popen(['/usr/bin/time', '-l', *map(str, argv)],
                                 stdout=stdout, stderr=stderr, preexec_fn=limits,
                                 start_new_session=True, env={'PATH': '/usr/bin:/bin'})
        timed_out = False
        try:
            code = child.wait(timeout=timeout)
        except BaseException as error:
            try:
                os.killpg(child.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            code = child.wait(timeout=5)
            if not isinstance(error, subprocess.TimeoutExpired):
                raise
            timed_out = True
    result = {'argv': list(map(str, argv)), 'exitCode': code,
              'timedOut': timed_out, 'seconds': time.monotonic() - started,
              'stdout': identity(out), 'stderr': identity(err),
              'limits': {'wallSeconds': timeout, 'cpuSeconds': timeout,
                         'eachOutputBytes': MAX_OUTPUT, 'children': 1}}
    write_json(directory / (stem + '-execution.json'), result)
    return result


def build(directory, args):
    """Build only the independent source, public program header and numeric libraries."""
    register_limit = args.oracle_register_limit
    if type(register_limit) is not int or not 1 <= register_limit <= 1000000:
        raise ValueError('oracle_register_limit')
    directory = Path(directory).resolve()
    directory.mkdir(parents=True, exist_ok=False)
    snapshots = directory / 'inputs'
    snapshots.mkdir()
    selected = native_build.resolve(args)
    inputs = []
    for original in (HERE / 'reference.c', HEADER, HERE / 'independent_replay.py',
                     HERE / 'native_build.py', HERE / 'oracle-provenance.json'):
        target = snapshots / original.name
        before = identity(original)
        shutil.copyfile(original, target)
        snapshot = identity(target)
        if before != identity(original) or before['sha256'] != snapshot['sha256']:
            raise ValueError('concurrent_source_change')
        inputs.append({'original': before, 'snapshot': snapshot})
    compiler = selected['compiler']
    numeric = selected['numeric']
    libraries = [numeric[name]['archive'] for name in ('mpfr', 'gmp')]
    headers = [numeric[name]['header'] for name in ('mpfr', 'gmp')]
    build_inputs = ([identity(path) for path in libraries + headers] + selected['identities'] +
                    [item['snapshot'] for item in inputs])
    binary = directory / 'precision-reference'
    argv = [compiler, '-isysroot', selected['sdk'], '-std=c17', '-O1',
            '-Wall', '-Wextra', '-Werror', '-fno-fast-math', '-ffp-contract=off',
            '-DOSDI_REFERENCE_MAX_REGS=' + str(register_limit), '-I' + str(snapshots)]
    for name in ('mpfr', 'gmp'):
        argv += ['-I', numeric[name]['include']]
    argv += [snapshots / 'reference.c', *libraries, '-o', binary]
    execution = run(argv, directory, 'compile', timeout=60)
    native_build.unchanged(build_inputs)
    receipt = {'schema': 'osdi.independent-precision-build.v1', 'inputs': inputs,
               'compiler': native_build.identity(compiler), 'compilerVersion': native_build.output([compiler, '--version']),
               'buildSelection': selected['selection'], 'registerLimit': register_limit,
               'arithmeticLibraries': [identity(path) for path in libraries],
               'arithmeticHeaders': [identity(path) for path in headers], 'execution': execution,
               'status': 'failed'}
    if execution['exitCode'] == 0 and not execution['timedOut']:
        dependencies = native_build.output(['otool', '-L', binary])
        if any(not line.strip().split(' ')[0].startswith(('/usr/lib/', '/System/Library/'))
               for line in dependencies.splitlines()[1:]):
            raise RuntimeError('Independent static oracle has a non-system dynamic dependency')
        receipt.update(status='passed', binary=identity(binary), dynamicDependencies=dependencies)
    write_json(directory / 'receipt.json', receipt)
    if receipt['status'] != 'passed':
        raise RuntimeError('independent_compile_failed:' + str(directory / 'compile.stderr'))
    return binary


def points_bytes(vectors, contexts):
    if len(vectors) != 4 or len(contexts) != 2:
        raise ValueError('paired_shape')
    n = len(vectors[0])
    if n > 128 or any(len(x) != n for x in vectors):
        raise ValueError('coordinate_shape')
    if any(not math.isfinite(x) for lane in vectors for x in lane):
        raise ValueError('nonfinite_coordinate')
    for context in contexts:
        if context['mode'] not in (2, 3):
            raise ValueError('context_mode')
        if any(not math.isfinite(context[x]) for x in ('high', 'low')):
            raise ValueError('nonfinite_context')
    c, r = contexts
    return (struct.pack('<8sIIII4d', b'OSDIREF1', 1, n, c['mode'], r['mode'],
                        c['high'], c['low'], r['high'], r['low']) +
            struct.pack('<' + str(4*n) + 'd', *(x for lane in vectors for x in lane)))


def replay(binary, program, setup, vectors, contexts, directory, bits=512, work=100000000):
    directory = Path(directory).resolve()
    directory.mkdir(parents=True, exist_ok=False)
    if bits < 128 or bits > 4096 or work < 1 or work > 1000000000:
        raise ValueError('operating_profile')
    inputs = {key: identity(value) for key, value in
              (('binary', binary), ('program', program), ('setup', setup))}
    point = directory / 'points.bin'
    with point.open('xb') as stream:
        stream.write(points_bytes(vectors, contexts))
    inputs['points'] = identity(point)
    execution = run([binary, program, setup, point, bits, work], directory, 'point')
    changed = [key for key, value in inputs.items() if identity(value['path']) != value]
    receipt = {'schema': 'corespice.independent-precision-replay.v1', 'inputs': inputs,
               'precisionBits': bits, 'maximumWork': work, 'contexts': contexts,
               'vectors': vectors, 'execution': execution,
               'changedInputs': changed,
               'status': 'passed' if execution['exitCode'] == 0 and not execution['timedOut'] and not changed else 'failed'}
    write_json(directory / 'receipt.json', receipt)
    return receipt


NONE = 0xffffffff
STRIDES = [8, 16, 24, 24, 40, 24, 16, 24, 32, 16, 1]


def dyadic(value):
    value = value['significand']
    return -int(value[1:], 2) if value.startswith('-') else int(value, 2)


def endpoint(value):
    return Fraction(dyadic(value)) * Fraction(2) ** value['exponent']


def enclosure(value):
    return endpoint(value['lowerBinary']), endpoint(value['upperBinary'])


def contains(value, expected):
    lo, hi = enclosure(value)
    return lo <= expected <= hi


def pack_table(fmt, rows):
    return b''.join(struct.pack('<' + fmt, *row) for row in rows)


def fixture(kind):
    """Independent authored pure-CFG fixtures, with exact published layout."""
    regs = [1, 1, 1, 3, 1, 1, 1]
    constants = [(0, 1, 0), (1, 1, struct.unpack('<Q', struct.pack('<d', 1.0))[0])]
    instructions = [(47, 2, 1, 0, NONE, 0)]
    blocks = [(0, 0, 0, 0, 0, 0, NONE, NONE, NONE, 0),
              (1, 0, 0, 0, 0, 0, NONE, NONE, NONE, 0)]
    root = 1
    phis = []
    if kind in ('inactive-select', 'active-root'):
        constants.append((3, 3, 0))
        instructions.append((74, 4, 3, 2, 1, 0))
        blocks.append((2, 0, 2, 0, 0, 0, NONE, NONE, NONE, 0))
        root = 2 if kind == 'active-root' else 4
    else:
        instructions.append((54, 3, 2, 0, NONE, 0))
        blocks.append((2, 0, 2, 0, 0, 2, 3, 3, 4, 0))
        instructions.extend([(44, 4, 1, 1, NONE, 0), (45, 5, 1, 0, NONE, 0)])
        blocks.extend([(2, 2, 1, 0, 0, 1, NONE, 5, NONE, 0),
                       (2, 3, 1, 0, 0, 1, NONE, 5, NONE, 0),
                       (2, 4, 0, 0, 2, 0, NONE, NONE, NONE, 0)])
        phis = [(6, 3, 4, 0), (6, 4, 5, 0)]
        if kind == 'live-join':
            root = 6
        elif kind == 'effect':
            instructions[2] = (73, NONE, 1, NONE, NONE, 0)
        elif kind == 'loop':
            blocks[3] = (2, 2, 1, 0, 0, 1, NONE, 3, NONE, 0)
        elif kind == 'no-join':
            blocks[3] = (2, 2, 1, 0, 0, 0, NONE, NONE, NONE, 0)
            blocks[4] = (2, 3, 1, 0, 0, 0, NONE, NONE, NONE, 0)
    functions = [(1, 0, 0, 1, 0, 0), (2, 1, 1, 1, 0, 0),
                 (3, 2, 2, len(blocks)-2, 0, 0)]
    tables = [pack_table('II', [(x, 0) for x in regs]), pack_table('IIQ', constants), b'',
              pack_table('6I', functions), pack_table('10I', blocks),
              pack_table('6I', instructions), pack_table('4I', phis),
              pack_table('6I', [(2, 1, root, 0, NONE, 0), (2, 2, 0, 0, NONE, 0)]),
              b'', pack_table('4I', [(0, 0, 1, 0)]), b'p']
    header = struct.pack('<8sIIQIIII32s', b'OSDIPR1', 1, 248, 0, 1, 0, 1, 0, bytes(32))
    offset, sections, body = 248, [], b''
    for stride, table in zip(STRIDES, tables):
        padding = (-offset) % 8
        body += bytes(padding)
        offset += padding
        sections.append(struct.pack('<QII', offset, len(table)//stride, stride))
        body += table
        offset += len(table)
    result = bytearray(header + b''.join(sections) + body)
    struct.pack_into('<Q', result, 16, len(result))
    raw_map = struct.pack('<I', 0)
    setup = (struct.pack('<IIQQII', 1, 96, 100,
                        struct.unpack('<Q', struct.pack('<d', 300.15))[0], 1, 1) +
             struct.pack('<QII', 96, 0, 16) + struct.pack('<QII', 96, 0, 24) +
             struct.pack('<QII', 96, 1, 4) + struct.pack('<QII', 100, 0, 1) + raw_map)
    return result, setup


def selfchecks(binary, directory):
    directory.mkdir(parents=True, exist_ok=False)
    results = []
    kinds = ('inactive-select', 'active-root', 'inactive-region', 'live-join',
             'effect', 'loop', 'no-join', 'bad-opcode', 'bad-root', 'work-limit')
    for kind in kinds:
        case = directory / (kind + '-inputs')
        case.mkdir()
        program, setup = fixture('inactive-select' if kind.startswith('bad-') or kind == 'work-limit' else kind)
        if kind == 'bad-opcode':
            offset = struct.unpack_from('<Q', program, 72 + 5*16)[0]
            struct.pack_into('<I', program, offset, 999)
        if kind == 'bad-root':
            offset = struct.unpack_from('<Q', program, 72 + 7*16)[0]
            struct.pack_into('<I', program, offset + 12, 1)
        (case/'program').write_bytes(program)
        (case/'setup').write_bytes(setup)
        receipt = replay(binary, case/'program', case/'setup', [[0.0]]*4,
                                [{'mode': 2, 'high': 1.0, 'low': 0.0},
                                 {'mode': 2, 'high': 0.0, 'low': 0.0}], directory/kind,
                                work=1 if kind == 'work-limit' else 1000000)
        expected = kind in ('inactive-select', 'inactive-region')
        passed = (receipt['status'] == 'passed') == expected
        if expected and passed:
            output = json.loads((directory/kind/'point.stdout').read_text())
            passed = contains(output['rows'][0]['F'], Fraction(1))
        results.append({'case': kind, 'expectedSuccess': expected, 'passed': passed,
                        'receipt': identity(directory/kind/'receipt.json')})
    write_json(directory/'verification.json', {'schema': 'corespice.independent-precision-controls.v1',
                      'tool': identity(__file__), 'binary': identity(binary),
                      'cases': results, 'passed': all(x['passed'] for x in results)})
    if not all(x['passed'] for x in results):
        raise RuntimeError('control_failure: ' + ','.join(x['case'] for x in results if not x['passed']))
    return results


def compare_worker(binary, worker_report, directory):
    """Independently evaluate exact paired inputs, then compare exact dyadic sets."""
    directory.mkdir(parents=True, exist_ok=False)
    report = json.loads(Path(worker_report).read_text())
    if report.get('succeeded') is not True or not report.get('families'):
        raise AssertionError('worker_report_not_successful')
    results = []
    for name, data in report['families'].items():
        if not name or Path(name).name != name or name in ('.', '..'):
            raise ValueError('family_path')
        for key in ('program', 'setup'):
            if identity(data[key+'Path'])['sha256'] != data[key+'SHA256']:
                raise AssertionError(key+'_identity_mismatch')
        if data['status'] != 0:
            raise AssertionError('worker_not_successful:' + name)
        receipt = replay(binary, data['programPath'], data['setupPath'],
                                data['vectors'], data['contexts'], directory/name)
        if receipt['status'] != 'passed':
            raise AssertionError('independent_point_failure:' + name)
        output = json.loads((directory/name/'point.stdout').read_text())
        count = len(data['outputs'][0])
        if (output.get('status') != 'passed' or output.get('precisionBits') != 512 or
                len(output['rows']) != count or
                [row['index'] for row in output['rows']] != list(range(count)) or
                len(data['outputs']) != 8 or
                any(len(lane) != count for lane in data['outputs'][:6]) or
                any(not math.isfinite(value) for lane in data['outputs'] for value in lane)):
            raise AssertionError('independent_output_coverage:' + name)
        failures, channels = [], 0
        for row in output['rows']:
            i = row['index']
            for key, offset in (('F', 0), ('deltaQ', 3)):
                lo, hi = enclosure(row[key])
                center = Fraction(data['outputs'][offset][i]) + Fraction(data['outputs'][offset+1][i])
                radius = Fraction(data['outputs'][offset+2][i])
                if radius < 0 or lo > hi:
                    raise AssertionError('invalid_enclosure')
                channels += 1
                if not center-radius <= lo <= hi <= center+radius:
                    failures.append({'row': i, 'channel': key,
                                     'oracleLower': str(lo), 'oracleUpper': str(hi),
                                     'workerLower': str(center-radius), 'workerUpper': str(center+radius)})
        results.append({'family': name, 'passed': not failures, 'channels': channels,
                        'failures': failures,
                        'receipt': identity(directory/name/'receipt.json')})
    result = {'schema': 'corespice.independent-precision-containment.v1',
              'workerReport': identity(worker_report), 'tool': identity(__file__),
              'independentBinary': identity(binary), 'families': results,
              'passed': all(x['passed'] for x in results)}
    write_json(directory/'comparison.json', result)
    if not result['passed']:
        raise AssertionError('worker_enclosure_mismatch')
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='operation', required=True)
    command = commands.add_parser('build', help='Build the source-reproducible independent static oracle')
    command.add_argument('--output', required=True, type=Path)
    command.add_argument('--oracle-register-limit', default=200000, type=int)
    native_build.arguments(command)
    command = commands.add_parser('controls', help='Run generic success/refusal controls against the actual oracle')
    command.add_argument('--binary', required=True, type=Path)
    command.add_argument('--output', required=True, type=Path)
    command = commands.add_parser('compare', help='Compare explicit worker point enclosures using 512-bit oracle replay')
    command.add_argument('--binary', required=True, type=Path)
    command.add_argument('--worker-report', required=True, type=Path)
    command.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    if args.operation == 'build':
        print(build(args.output, args))
    else:
        results = (selfchecks(args.binary.resolve(strict=True), args.output.resolve())
                   if args.operation == 'controls' else
                   compare_worker(args.binary.resolve(strict=True), args.worker_report, args.output.resolve()))
        print(json.dumps({'passed': True, 'checks': len(results)}))


if __name__ == '__main__':
    main()

