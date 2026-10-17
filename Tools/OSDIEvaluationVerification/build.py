#!/usr/bin/env python3
"""Build and capture independent direct-ABI S2 point references, without COSDI."""
from argparse import ArgumentParser
from hashlib import sha256
from pathlib import Path
import json
import math
import os
import platform
import shutil
import signal
import subprocess
import time

HEADER_SHA = '0f3eeb9ef3c9f0bea9b3c59f34c632555e2367f6e28722e219d8c3f1d655a16f'
PRUNED_BSIM_SHA = '6eced9dd6bc02b0e78905b67302938e5bdaa2707e9b2dc0deb18f4f2142cbdfa'


def digest(path):
    return sha256(Path(path).read_bytes()).hexdigest()


def save(path, value):
    path.write_text(json.dumps(value, indent=2, allow_nan=False)+'\n')


def run(arguments, output, label, receipts, expected=0):
    started = time.monotonic()
    process = subprocess.Popen([str(item) for item in arguments], stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, text=True, start_new_session=True)
    try:
        stdout, stderr = process.communicate(timeout=30)
    except BaseException:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.communicate(timeout=5)
        raise
    (output/f'{label}.stdout').write_text(stdout)
    (output/f'{label}.stderr').write_text(stderr)
    receipt = {'argv': [str(item) for item in arguments], 'exitCode': process.returncode,
               'seconds': time.monotonic()-started, 'stdoutSHA256': digest(output/f'{label}.stdout'),
               'stderrSHA256': digest(output/f'{label}.stderr')}
    receipts.append(receipt)
    if process.returncode != expected:
        raise RuntimeError(f'{label}: expected exit {expected}, got {process.returncode}: {stderr}')
    return stdout


def command(oracle, library, request, potentials=None):
    arguments = [oracle, library, str(request['descriptorIndex']),
                 repr(request['temperatureKelvin']), str(request['connectedTerminalCount'])]
    for parameter in request['parameters']:
        value = parameter['value']
        kind = value['kind']
        scope = 'm' if parameter['scope'] == 'model' else 'i'
        tag = {'real': 'r', 'integer': 'i', 'string': 's'}[kind]
        arguments.append(f'p:{scope}:{tag}:{parameter["name"]}={value[kind]}')
    arguments += [f'r:{name}' for name in request['readParameters']]
    arguments += [f'v:{name}={value!r}' for name, value in (potentials or request['rawPotentials']).items()]
    return arguments


def contracted(values, mapping):
    count = max((index for index in mapping if index is not None), default=-1)+1
    result = [0.]*count
    for value, index in zip(values, mapping):
        if index is not None:
            result[index] += value
    return result


def finite_differences(oracle, libraries, cases, output, receipts):
    checks = []
    # No-collapse full matrices plus one collapsed topology, in independent C order.
    for identifier in ('rc-linear', 'bsim-n-hv-all', 'flow-unit', 'collapse-on',
                       'bsim-n-hv-all-nonzero-aux', 'auxiliary-coupling'):
        case = next(case for case in cases if case['id'] == identifier)
        reference = case['reference']
        request = case['input']
        mapping = reference['collapseMap']
        active_count = len(reference['localUnknowns'])
        columns = range(active_count) if identifier != 'collapse-on' else range(1)
        n = len(mapping)
        for column in columns:
            center = reference['localUnknowns'][column]
            step = math.ldexp(max(1., abs(center)), -16)
            observations = []
            for divisor in (1, 2):
                h = step/divisor
                pair = []
                for sign in (-1, 1):
                    potentials = dict(zip(reference['nodes'], reference['rawPotentials']))
                    for node, index in zip(reference['nodes'], mapping):
                        if index == column:
                            potentials[node] += sign*h
                    label = f'fd-{identifier}-{column}-{divisor}-{sign}'
                    raw = run(command(oracle, libraries[request['library']], request, potentials), output, label, receipts)
                    pair.append(json.loads(raw))
                observations.append((h, pair))
            for field, matrix in [('f', 'g'), ('q', 'c')]:
                target = [0.]*active_count
                for row in range(n):
                    destination = mapping[row]
                    if destination is None:
                        continue
                    for raw_column in range(n):
                        if mapping[raw_column] == column:
                            target[destination] += reference[matrix][row*n+raw_column]
                derivatives = []
                magnitudes = []
                for h, pair in observations:
                    lo = contracted(pair[0][field], mapping)
                    hi = contracted(pair[1][field], mapping)
                    derivatives.append([(b-a)/(2*h) for a, b in zip(lo, hi)])
                    magnitudes.append([abs(a)+abs(b) for a, b in zip(lo, hi)])
                for row, expected in enumerate(target):
                    coarse, fine = derivatives[0][row], derivatives[1][row]
                    # A measured ordinary-point error budget, not certified inclusion.
                    roundoff = 32*math.ulp(1.)*magnitudes[1][row]/observations[1][0]
                    truncation = 4*abs(fine-coarse)/3
                    bound = roundoff+truncation+64*math.ulp(expected)
                    error = abs(fine-expected)
                    checks.append({'caseID': identifier, 'matrix': matrix, 'row': row,
                                   'column': column, 'target': expected, 'difference': fine,
                                   'absoluteError': error, 'bound': bound, 'passed': error <= bound})
    return checks


def build(args):
    started = time.monotonic()
    if platform.system() != 'Darwin' or platform.machine() != 'arm64':
        raise RuntimeError('S2 native qualification requires macOS arm64.')
    here = Path(__file__).resolve().parent
    fixtures = here.parents[1]/'Tests'/'OSDIEvaluationTests'/'Fixtures'
    header = args.header.resolve(strict=True)
    compiler = args.compiler.resolve(strict=True)
    clang = Path(shutil.which(str(args.clang)) or str(args.clang)).resolve(strict=True)
    bsim = args.bsim.resolve(strict=True)
    if digest(header) != HEADER_SHA or digest(here/'osdi_0_4.h') != HEADER_SHA:
        raise RuntimeError('Pinned header identity mismatch.')
    bsim_sha = args.bsim_sha256.lower()
    if len(bsim_sha) != 64 or any(c not in '0123456789abcdef' for c in bsim_sha):
        raise RuntimeError('Expected BSIM SHA-256 must contain 64 hexadecimal digits.')
    if bsim_sha == PRUNED_BSIM_SHA or digest(bsim) != bsim_sha:
        raise RuntimeError('BSIM identity must match the selected new full-residuals artifact, not the pruned baseline.')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if (output/'manifest.json').exists():
        raise RuntimeError('Use a fresh S2 output directory.')
    snapshots = output/'input-snapshots'
    snapshots.mkdir(exist_ok=True)
    for source in [here/'oracle.c', here/'build.py', here/'osdi_0_4.h', *fixtures.iterdir()]:
        if source.is_file() and source.suffix in ('.c', '.py', '.h', '.va', '.json'):
            shutil.copyfile(source, snapshots/source.name)
    receipts = []
    compiler_version = run([compiler, '--version'], output, 'compiler-version', receipts)
    compiler_help = run([compiler, '--help'], output, 'compiler-help', receipts)
    if '--full-residuals' not in compiler_help:
        raise RuntimeError('Selected compiler does not advertise the required --full-residuals profile.')
    clang_version = run([clang, '--version'], output, 'clang-version', receipts)
    oracle = output/'oracle'
    run([clang, '-std=c11', '-Wall', '-Wextra', '-Werror', here/'oracle.c', '-o', oracle], output, 'oracle-build', receipts)
    libraries = {'bsim': bsim}
    for source in sorted(fixtures.glob('*.va')):
        destination = output/(source.stem+'.osdi')
        run([compiler, '--full-residuals', '-O', '1', '--target', 'aarch64-apple-darwin', source, '-o', destination], output, 'compile-'+source.stem, receipts)
        libraries[source.stem] = destination
    protocol_libraries = {}
    protocol_names = ['state', 'fatal', 'finish', 'stop', 'diagnostics', 'diagnostic_overflow',
                      'logged_error', 'limiting', 'unknown_flags', 'nonfinite_output', 'negative_noise', 'nonfinite_noise',
                      'time_state_charge', 'no_charge']
    for mode, name in enumerate(protocol_names):
        destination = output/('protocol-'+name+'.dylib')
        run([clang, '-std=c11', '-Wall', '-Wextra', '-Werror', '-dynamiclib', '-I', here,
             f'-DPROTOCOL_MODE={mode}', fixtures/'protocol.c', '-o', destination],
            output, 'compile-protocol-'+name, receipts)
        protocol_libraries[name] = {'path': str(destination), 'sha256': digest(destination), 'mode': mode}
    requests = json.loads((fixtures/'requests.json').read_text())['cases']
    cases = []
    for request in requests:
        raw = run(command(oracle, libraries[request['library']], request), output, request['id'], receipts)
        reference = json.loads(raw)
        n = len(reference['nodes'])
        assert len(reference['f']) == len(reference['q']) == n
        assert len(reference['g']) == len(reference['c']) == n*n
        assert reference['evaluationFlags'] == 34863 and reference['modelFlags'] == 0
        case = {'id': request['id'], 'library': str(libraries[request['library']]),
                'artifactSHA256': digest(libraries[request['library']]),
                'input': request, 'reference': reference}
        cases.append(case)
    auxiliary = next(case for case in cases if case['id'] == 'auxiliary-coupling')
    reference = auxiliary['reference']
    n = len(reference['nodes'])
    p, q, a = [reference['nodes'].index(name) for name in ('p', 'n', 'aux')]
    voltage = reference['rawPotentials'][p]-reference['rawPotentials'][q]
    auxiliary_bias = reference['rawPotentials'][a]
    physical_f = .001*voltage+(.1+.2*voltage)*auxiliary_bias
    physical_q = 2e-12*voltage+3e-9*auxiliary_bias
    expected_f = {p: physical_f, q: -physical_f, a: auxiliary_bias}
    expected_q = {p: physical_q, q: -physical_q, a: (2e-9+.3e-9*voltage)*auxiliary_bias}
    physical_slope = .001+.2*auxiliary_bias
    auxiliary_slope = .1+.2*voltage
    expected_g = {(p,p): physical_slope, (p,q): -physical_slope, (p,a): auxiliary_slope,
                  (q,p): -physical_slope, (q,q): physical_slope, (q,a): -auxiliary_slope,
                  (a,p): 0., (a,q): 0., (a,a): 1.}
    expected_c = {(p,p): 2e-12, (p,q): -2e-12, (p,a): 3e-9,
                  (q,p): -2e-12, (q,q): 2e-12, (q,a): -3e-9,
                  (a,p): .3e-9*auxiliary_bias, (a,q): -.3e-9*auxiliary_bias, (a,a): 2e-9+.3e-9*voltage}
    for field, targets in [('f', expected_f), ('q', expected_q), ('g', expected_g), ('c', expected_c)]:
        for coordinate, expected in targets.items():
            index = coordinate[0]*n+coordinate[1] if isinstance(coordinate, tuple) else coordinate
            actual = reference[field][index]
            assert math.isclose(actual, expected, rel_tol=4e-15, abs_tol=8*math.ulp(expected)), (field, coordinate, actual, expected)
    default_library = output/'auxiliary_coupling.default.osdi'
    run([compiler, '-O', '1', '--target', 'aarch64-apple-darwin',
         fixtures/'auxiliary_coupling.va', '-o', default_library], output, 'compile-auxiliary-default', receipts)
    default_raw = run(command(oracle, default_library, auxiliary['input']), output, 'auxiliary-default', receipts)
    default_reference = json.loads(default_raw)
    default_aux = default_reference['nodes'].index('aux')
    reproduced = (default_reference['f'][default_aux] == 0 and default_reference['q'][default_aux] == 0
                  and reference['f'][a] != 0 and reference['q'][a] != 0)
    assert reproduced, 'Default profile no longer reproduces the intended pruning control.'
    discriminator = {'defaultArtifact': str(default_library), 'defaultArtifactSHA256': digest(default_library),
                     'sourceSHA256': digest(fixtures/'auxiliary_coupling.va'),
                     'defaultProfile': 'default', 'qualifiedProfile': 'full-residuals',
                     'reproduced': reproduced, 'defaultReference': default_reference,
                     'qualifiedCaseID': 'auxiliary-coupling'}
    # Analytic authority supplements the direct-ABI comparison.
    for identifier, conductance, capacitance in [('rc-linear', .001, 2e-12), ('rc-first', .001, 2e-12), ('rc-second', .003, 4e-12)]:
        reference = next(case['reference'] for case in cases if case['id'] == identifier)
        assert reference['f'] == [conductance, -conductance]
        assert reference['q'] == [capacitance, -capacitance]
        assert reference['g'] == [conductance, -conductance, -conductance, conductance]
        assert reference['c'] == [capacitance, -capacitance, -capacitance, capacitance]
    fd = finite_differences(oracle, libraries, cases, output, receipts)
    failures = [check for check in fd if not check['passed']]
    save(output/'finite-differences.json', {'checks': fd, 'count': len(fd),
        'failedCount': len(failures), 'failures': failures,
        'status': 'failed' if failures else 'passed'})
    result = {'schema': 'osdi.point-qualification.v1', 'cases': cases,
              'finiteDifferenceChecks': len(fd), 'finiteDifferenceStatus': 'failed' if failures else 'passed',
              'finiteDifferenceFailures': failures, 'seconds': time.monotonic()-started,
              'headerSHA256': HEADER_SHA, 'bsimSHA256': bsim_sha,
              'compilerProfile': 'full-residuals', 'defaultProfileDiscriminator': discriminator,
              'compiler': {'path': str(compiler), 'sha256': digest(compiler), 'version': compiler_version.strip()},
              'clang': {'path': str(clang), 'sha256': digest(clang), 'version': clang_version.strip()},
              'oracle': {'sourceSHA256': digest(here/'oracle.c'), 'binarySHA256': digest(oracle)},
              'protocolLibraries': protocol_libraries,
              'sources': {path.name: digest(path) for path in sorted(fixtures.iterdir()) if path.suffix in ('.va', '.json', '.c')},
              'builderSHA256': digest(__file__), 'commands': receipts}
    if args.compiler_profile_receipt:
        receipt_path = args.compiler_profile_receipt.resolve(strict=True)
        shutil.copyfile(receipt_path, output/'compiler-profile-receipt.json')
        result['compilerProfileReceipt'] = {'path': str(receipt_path), 'sha256': digest(receipt_path)}
    save(output/'manifest.json', result)
    print(json.dumps({'manifest': str(output/'manifest.json'), 'cases': len(cases),
                      'finiteDifferenceChecks': len(fd), 'finiteDifferenceStatus': result['finiteDifferenceStatus'],
                      'failedDifferenceChecks': len(failures), 'seconds': result['seconds']}))


if __name__ == '__main__':
    parser = ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--clang', type=Path, required=True)
    parser.add_argument('--header', type=Path, required=True)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--bsim', type=Path, required=True)
    parser.add_argument('--bsim-sha256', required=True)
    parser.add_argument('--compiler-profile-receipt', type=Path)
    args = parser.parse_args()
    def interrupted(signum, frame):
        raise KeyboardInterrupt(f'S2 builder interrupted by signal {signum}')
    signal.signal(signal.SIGTERM, interrupted)
    build(args)
