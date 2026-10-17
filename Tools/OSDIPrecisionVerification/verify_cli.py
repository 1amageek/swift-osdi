#!/usr/bin/env python3
"""Exercise the actual precision CLI, persistence, cold snapshots and refusal paths."""
import argparse
import copy
import decimal
import hashlib
import json
import math
import os
from pathlib import Path
import signal
import struct
import subprocess
import time


def load_json(data):
    # JSON permits -0. Python's integer parser otherwise discards its IEEE sign.
    return json.loads(data, parse_int=lambda text: -0.0 if text == '-0' else int(text))


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def save(path, value):
    Path(path).write_text(json.dumps(value, sort_keys=True, allow_nan=False) + '\n')


def bits(value):
    return struct.unpack('>Q', struct.pack('>d', value))[0]


def vector(values):
    return {'values': values, 'bitPatterns': [bits(v) for v in values]}


def reference(path):
    path = Path(path)
    digest = bytes.fromhex(sha(path))
    # CircuiteFoundation ArtifactID canonical v1, big-endian lengths and byte count.
    identity = b'CircuiteArtifactContent\0' + struct.pack('>H', 1)
    identity += struct.pack('>H', 6) + b'sha256' + struct.pack('>H', len(digest)) + digest
    identity += struct.pack('>Q', path.stat().st_size)
    return {'id': identity.hex(), 'descriptor': {'role': 'input', 'kind': 'tool', 'format': 'executable'}}


def budget(path):
    return {'maximumPageByteCount': 65536, 'maximumTotalByteCount': max(65536, Path(path).stat().st_size),
            'maximumPageCount': 65536, 'maximumWorkUnitCount': 1000000, 'maximumDurationNanoseconds': 10000000000}


def ordinary(path):
    return {'schemaVersion': 1, 'operation': 'prepare', 'libraryPath': str(Path(path).resolve()),
            'expectedSHA256': sha(path), 'expectedByteCount': Path(path).stat().st_size,
            'limits': {'maximumDescriptors': 16, 'maximumMetadataBytes': 1048576, 'maximumStorageBytes': 4194304},
            'readBudget': budget(path), 'descriptorIndex': 0, 'profile': 'dc-static-point',
            'parameters': [], 'simulationParameters': [], 'temperatureKelvin': 300.15, 'connectedTerminalCount': 1,
            'evaluationLimits': {'maximumWorkspaceBytes': 8388608, 'maximumDiagnosticCount': 256,
                                 'maximumDiagnosticBytes': 65536, 'maximumStringBytes': 65536}}


def request(model, worker, semantic, numeric, staging, operation):
    result = {'schemaVersion': 1, 'profile': 'stateless-precision-v1', 'operation': operation, 'model': model,
              'worker': {'path': str(Path(worker).resolve()), 'reference': reference(worker), 'readBudget': budget(worker)},
              'stagingParentDirectory': str(staging.resolve()), 'semanticProfileSHA256': semantic,
              'numericProfileSHA256': numeric, 'precisionBits': 256, 'maximumReportBytes': 268435456,
              'options': {'maximumProgramBytes': 16777216, 'maximumMappingBytes': 16777216,
                          'arenaBytes': 16777216, 'maximumWorkUnits': 100000000,
                          'minimumPrecisionBits': 53, 'maximumPrecisionBits': 256,
                          'startupTimeoutNanoseconds': 2000000000, 'evaluationTimeoutNanoseconds': 2000000000,
                          'cleanupTimeoutNanoseconds': 1000000000, 'pollNanoseconds': 1000000}}
    if operation == 'evaluate':
        result.update(candidateHigh=vector([1.0]), candidateLow=vector([1e-20]),
                      referenceHigh=vector([1.0]), referenceLow=vector([-0.0]),
                      candidateContext={'mode': 'transient', 'timeHigh': 1.0, 'timeLow': 2 ** -60,
                                        'timeHighBits': bits(1.0), 'timeLowBits': bits(2 ** -60)},
                      referenceContext={'mode': 'transientInitialization', 'timeHigh': 0.0, 'timeLow': -0.0,
                                        'timeHighBits': bits(0.0), 'timeLowBits': bits(-0.0)})
    return result


def run(executable, request_path, output, flag='--precision-request'):
    command = [str(executable), flag, str(request_path), '--output', str(output)]
    env = {k: v for k, v in os.environ.items() if k in ['HOME', 'PATH', 'TMPDIR', 'SDKROOT', 'TOOLCHAINS', 'MallocNanoZone']}
    start = time.monotonic()
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env, start_new_session=True)
    try:
        stdout, stderr = process.communicate(timeout=20)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        process.communicate(timeout=5)
        raise AssertionError('Bounded CLI invocation timed out')
    record = {'command': command, 'exitCode': process.returncode, 'seconds': time.monotonic() - start,
              'stdout': stdout.decode(), 'stderr': stderr.decode()}
    report = load_json(output.read_bytes())
    assert report['requestBinding'] == {'sha256': sha(request_path), 'byteCount': request_path.stat().st_size}
    # A second, independent JSON persistence boundary must retain every binary64 pair.
    restored = load_json(json.dumps(report, sort_keys=True, allow_nan=False))
    assert report == restored
    check_bits(restored)
    return record, restored


def check_bits(value):
    if isinstance(value, dict):
        if 'values' in value and 'bitPatterns' in value:
            assert len(value['values']) == len(value['bitPatterns'])
            assert [bits(v) for v in value['values']] == value['bitPatterns']
        if 'timeHighBits' in value:
            assert bits(value['timeHigh']) == value['timeHighBits']
            assert bits(value['timeLow']) == value['timeLowBits']
        for child in value.values():
            check_bits(child)
    elif isinstance(value, list):
        for child in value:
            check_bits(child)


def validate_report(report, expected):
    assert report['succeeded'] and report['input'] == expected
    program = report['program']
    assert hashlib.sha256(bytes(program['programBytes'])).hexdigest() == program['programSHA256']
    assert hashlib.sha256(bytes(program['setupBytes'])).hexdigest() == program['setupSHA256']
    assert program['semanticProfileSHA256'] == expected['semanticProfileSHA256']
    assert report['workerReference'] == expected['worker']['reference']
    assert report['numericProfileSHA256'] == expected['numericProfileSHA256']
    if expected['operation'] == 'snapshot':
        assert report['stage'] == 'osdi-precision-snapshot' and report.get('evaluation') is None
        return
    assert report['stage'] == 'osdi-precision-evaluation'
    evaluation = report['evaluation']
    assert evaluation['usedPrecisionBits'] == 256 and evaluation['requestSequence'] == 1
    D = decimal.Decimal.from_float
    with decimal.localcontext() as context:
        context.prec = 180
        expected_f = decimal.Decimal(1) + (D(1.0) + D(1e-20)) ** 2
        center_f = D(evaluation['fHigh']['values'][0]) + D(evaluation['fLow']['values'][0])
        assert abs(expected_f - center_f) <= D(evaluation['fRadius']['values'][0])
        expected_q = D(1e-9) * D(1e-20)
        center_q = D(evaluation['deltaQHigh']['values'][0]) + D(evaluation['deltaQLow']['values'][0])
        assert abs(expected_q - center_q) <= D(evaluation['deltaQRadius']['values'][0])
    assert evaluation['fLow']['values'][0] != 0 and evaluation['deltaQHigh']['values'][0] != 0
    assert evaluation['g']['values'] == [2.0] and evaluation['c']['values'] == [1e-9]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--model', type=Path, required=True)
    parser.add_argument('--worker', type=Path, required=True)
    parser.add_argument('--semantic-profile', type=Path, required=True)
    numeric = parser.add_mutually_exclusive_group(required=True)
    numeric.add_argument('--numeric-profile-sha256')
    numeric.add_argument('--numeric-profile', type=Path)
    parser.add_argument('--ordinary-without-capability', type=Path)
    parser.add_argument('--family-requests', type=Path)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    profile_verified = False
    if args.numeric_profile:
        profile_bytes = args.numeric_profile.read_bytes()
        assert len(profile_bytes) <= 1048576, 'Numeric profile exceeds explicit one MiB test boundary'
        profile = load_json(profile_bytes)
        assert profile['schema'] == 1
        assert profile['worker']['sha256'] == sha(args.worker), 'Qualified numeric profile does not bind the actual worker image'
        args.numeric_profile_sha256 = hashlib.sha256(profile_bytes).hexdigest()
        profile_verified = True
    args.output_dir.mkdir(parents=True, exist_ok=False)
    if args.numeric_profile:
        (args.output_dir / 'numeric-profile.json').write_bytes(profile_bytes)
    staging = args.output_dir / 'staging'; staging.mkdir()
    semantic = sha(args.semantic_profile)
    original = request(ordinary(args.model), args.worker, semantic, args.numeric_profile_sha256, staging, 'evaluate')
    commands = []
    for name in ['evaluate', 'fresh-repeat', 'snapshot']:
        value = copy.deepcopy(original)
        if name == 'snapshot':
            value = request(value['model'], args.worker, semantic, args.numeric_profile_sha256, staging, 'snapshot')
        path = args.output_dir / (name + '-request.json'); save(path, value)
        command, report = run(args.executable, path, args.output_dir / (name + '-report.json'))
        commands.append(command); assert command['exitCode'] == 0, command
        validate_report(report, value)
        if name == 'evaluate': first = report
        if name == 'fresh-repeat': assert report['program'] == first['program'] and report['evaluation'] == first['evaluation']
        assert not list(staging.iterdir()), 'A completed command retained a staged image or worker'
    for mutation in ['missing-context', 'bit-pattern', 'overlap', 'unknown-operation', 'program-limit', 'arena-limit', 'work-limit', 'report-limit', 'worker-identity', 'semantic-identity', 'worker-failure'] + (['missing-capability'] if args.ordinary_without_capability else []):
        value = copy.deepcopy(original)
        if mutation == 'missing-context': del value['referenceContext']
        elif mutation == 'bit-pattern': value['candidateLow']['bitPatterns'][0] ^= 1
        elif mutation == 'overlap': value['candidateLow'] = vector([0.5])
        elif mutation == 'unknown-operation': value['operation'] = 'ordinary-fallback'
        elif mutation == 'program-limit': value['options']['maximumProgramBytes'] = 1
        elif mutation == 'arena-limit': value['options']['arenaBytes'] = 128
        elif mutation == 'work-limit': value['options']['maximumWorkUnits'] = 1
        elif mutation == 'report-limit': value['maximumReportBytes'] = 1
        elif mutation == 'worker-identity': value['worker']['reference']['id'] = value['worker']['reference']['id'][:-2] + f"{int(value['worker']['reference']['id'][-2:], 16) ^ 1:02x}"
        elif mutation == 'semantic-identity': value['semanticProfileSHA256'] = '00' * 32
        elif mutation == 'worker-failure':
            failed = args.output_dir / 'failing-worker'; failed.write_text('#!/bin/sh\nexit 37\n'); failed.chmod(0o700)
            value['worker'] = {'path': str(failed.resolve()), 'reference': reference(failed), 'readBudget': budget(failed)}
        elif mutation == 'missing-capability': value['model'] = ordinary(args.ordinary_without_capability)
        path = args.output_dir / (mutation + '-request.json'); save(path, value)
        command, report = run(args.executable, path, args.output_dir / (mutation + '-report.json'))
        commands.append(command); assert command['exitCode'] != 0, (mutation, command)
        assert report['succeeded'] is False and 'failure' in report
        assert report.get('program') is None and report.get('evaluation') is None
        assert not list(staging.iterdir()), (mutation, list(staging.iterdir()))
    # Preserve the unchanged ordinary schema/profile on the same model and executable.
    value = ordinary(args.model); path = args.output_dir / 'ordinary-request.json'; save(path, value)
    command, report = run(args.executable, path, args.output_dir / 'ordinary-report.json', '--request')
    commands.append(command); assert command['exitCode'] == 0 and report['schemaVersion'] == 1
    assert report['stage'] == 'osdi-preparation' and report.get('evaluation') is None
    if args.family_requests:
        for family in ['bulk', 'cmg', 'img', 'soi']:
            model = load_json((args.family_requests / (family + '-prepare.json')).read_bytes())
            value = request(model, args.worker, semantic, args.numeric_profile_sha256, staging, 'snapshot')
            path = args.output_dir / (family + '-snapshot-request.json'); save(path, value)
            command, report = run(args.executable, path, args.output_dir / (family + '-snapshot-report.json'))
            commands.append(command); assert command['exitCode'] == 0, (family, command)
            validate_report(report, value)
            (args.output_dir / (family + '.program')).write_bytes(bytes(report['program']['programBytes']))
            (args.output_dir / (family + '.setup')).write_bytes(bytes(report['program']['setupBytes']))
            assert not list(staging.iterdir())
    save(args.output_dir / 'receipt.json', {'status': 'passed', 'executableSHA256': sha(args.executable),
         'modelSHA256': sha(args.model), 'workerSHA256': sha(args.worker), 'semanticProfileSHA256': semantic,
         'numericProfileSHA256': args.numeric_profile_sha256, 'qualifiedProfileBindingVerified': profile_verified, 'commands': commands})
    print('Precision CLI actual execution, exact persistence, refusal and cleanup passed')


if __name__ == '__main__':
    main()
