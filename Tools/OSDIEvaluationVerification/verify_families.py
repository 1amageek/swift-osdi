#!/usr/bin/env python3
"""Verify persisted ordinary CLI reports against the same exact-artifact direct ABI."""
from argparse import ArgumentParser
from pathlib import Path
import json
import math
import struct

from build import digest, run, save


FAMILIES = {'bsim', 'cmg', 'img', 'soi', 'soi101'}
CONTEXTS = [('dc', 0, 0, None), ('ac', 1, 0, None),
            ('transientInitialization', 3, 0, None), ('transient', 2, 1e-8, None),
            ('noise', 4, 0, 100), ('noise', 4, 0, 1e6)]


def require(condition, detail):
    if not condition:
        raise RuntimeError(detail)


def exact(actual, expected, label):
    require(len(actual) == len(expected), f'{label}: dimension mismatch')
    zero_canonicalizations = []
    for index, (a, b) in enumerate(zip(actual, expected)):
        # Ordinary schema 2 uses JSON numbers; its encoder canonicalizes signed zero.
        # The precision schema carries explicit bit patterns and has a stronger contract.
        same_bits = struct.pack('!d', a) == struct.pack('!d', b)
        require(math.isfinite(a) and math.isfinite(b) and
                (same_bits or (a == 0 and b == 0)),
                f'{label}[{index}]: {a!r} != {b!r}')
        if not same_bits:
            zero_canonicalizations.append({'field': label, 'index': index})
    return zero_canonicalizations


def verify(args):
    manifest = json.loads(args.manifest.read_bytes())
    libraries = {item['id']: item for item in manifest['libraries']}
    require(len(libraries) == len(manifest['libraries']), 'Manifest library IDs must be unique.')
    required_families = (set(args.library_ids) if args.library_ids is not None
                         else FAMILIES.intersection(libraries))
    require(required_families and required_families.issubset(libraries),
            'Selected library IDs must be explicit manifest entries.')
    if args.library_ids is not None:
        require(len(args.library_ids) == len(required_families), 'Selected library IDs must be unique.')
    else:
        require({'bsim', 'cmg', 'img', 'soi'}.issubset(required_families),
                'The default profile requires the four baseline families; select --library-ids for other entries.')
    points = [point for point in manifest['cases']
              if any(device['library'] in required_families for device in point['input']['devices'])]
    require({device['library'] for point in points for device in point['input']['devices']} == required_families,
            'Every advertised real-model library, including SOI 101 when present, requires actual cases.')
    args.output.mkdir(parents=True, exist_ok=False)
    receipts, checks = [], []
    for point in points:
        identifier = point['id']
        devices = point['input']['devices']
        require(len(devices) == 1, f'{identifier}: only one-device point seeds are supported')
        device = devices[0]
        fact = libraries[device['library']]
        library = Path(fact['path']).resolve(strict=True)
        require(digest(library) == fact['sha256'], f'{identifier}: artifact identity mismatch')
        require(all(value == 0 for value in device['previousState']),
                f'{identifier}: direct ABI observer requires zero accepted state')
        values = {unknown['id']: unknown['value'] for unknown in point['reference']['unknowns']}
        nodes = point['reference']['devices'][0]['rawNodes']
        command = [args.oracle, library, device['descriptorIndex'],
                   device['temperatureKelvin'], device['connectedTerminalCount']]
        for parameter in device['parameters']:
            scope = {'model': 'm', 'instance': 'i'}[parameter['scope']]
            value = parameter['value']
            kind = value['kind']
            tag = {'real': 'r', 'integer': 'i', 'string': 's'}[kind]
            command.append(f'p:{scope}:{tag}:{parameter["name"]}={value[kind]}')
        for node in nodes:
            value = values[node['unknownID']] if node.get('unknownID') else 0
            command.append(f'v:{node["name"]}={value!r}')
        size = library.stat().st_size
        for mode, mode_id, time, frequency in CONTEXTS:
            label = f'{identifier}-{mode}-{frequency or 0}'
            oracle_command = command + [f'e:mode={mode_id}', f'e:time={time}',
                                        f'e:frequency={frequency or 0}']
            raw = run(oracle_command, args.output, label + '-oracle', receipts)
            reference = json.loads(raw)
            rejected_noise = [{'index': index, 'value': value,
                              'source': reference['noiseSources'][index]}
                             for index, value in enumerate(reference['noiseDensities'])
                             if not isinstance(value, (float, int)) or not math.isfinite(value)]
            context = {'mode': mode, 'timeSeconds': time}
            if frequency is not None:
                context['noiseFrequencyHz'] = frequency
            request = {
                'schemaVersion': 2, 'operation': 'evaluate', 'profile': 'dynamic-point',
                'context': context, 'libraryPath': str(library), 'expectedSHA256': fact['sha256'],
                'expectedByteCount': size,
                'limits': {'maximumDescriptors': 64, 'maximumMetadataBytes': 16_777_216,
                           'maximumStorageBytes': 67_108_864},
                'readBudget': {'maximumPageByteCount': 65_536, 'maximumTotalByteCount': size,
                               'maximumPageCount': (size + 65_535)//65_536,
                               'maximumWorkUnitCount': 2*((size + 65_535)//65_536),
                               'maximumDurationNanoseconds': 10_000_000_000},
                'descriptorIndex': device['descriptorIndex'], 'parameters': device['parameters'],
                'simulationParameters': [{'name': 'gmin', 'value': {'kind': 'real', 'real': 0}}],
                'temperatureKelvin': device['temperatureKelvin'],
                'connectedTerminalCount': device['connectedTerminalCount'],
                'evaluationLimits': {'maximumWorkspaceBytes': 16_777_216, 'maximumDiagnosticCount': 128,
                                     'maximumDiagnosticBytes': 65_536, 'maximumStringBytes': 1_048_576},
                'localUnknowns': reference['localUnknowns'], 'previousState': device['previousState']}
            request_path = args.output/(label + '-request.json')
            report_path = args.output/(label + '-report.json')
            save(request_path, request)
            run([args.cli, '--request', request_path, '--output', report_path],
                args.output, label + '-cli', receipts, expected=1 if rejected_noise else 0)
            # Verify the saved bytes through a fresh read, including the exact request binding.
            report = json.loads(report_path.read_bytes())
            require(report['input'] == request, f'{label}: input round trip')
            require(report['requestBinding'] == {'sha256': digest(request_path),
                                                'byteCount': request_path.stat().st_size},
                    f'{label}: request binding')
            require(report['artifactBinding'] == {'sha256': fact['sha256'], 'byteCount': size},
                    f'{label}: artifact binding')
            if rejected_noise:
                failure = report.get('failure', {})
                require(not report['succeeded'] and not report.get('evaluation') and
                        failure.get('nativeStatus') == 19 and failure.get('field') == 'noise_density',
                        f'{label}: nonfinite raw-noise rejection contract must produce typed refusal, no evaluation')
                checks.append({'caseID': identifier, 'context': context, 'requestSHA256': digest(request_path),
                               'reportSHA256': digest(report_path), 'status': 'consumer-rejected-raw-noise',
                               'rejectedDirectABINoise': rejected_noise})
                continue
            require(report['succeeded'], f'{label}: failed report')
            topology = report['prepared']['topology']
            require([node['name'] for node in topology['nodes']] == reference['nodes'],
                    f'{label}: raw node order')
            require([node.get('activeUnknownIndex') for node in topology['nodes']] == reference['collapseMap'],
                    f'{label}: collapsed topology')
            require(topology['stateCount'] == reference['stateCount'] and
                    topology['activeUnknownCount'] == len(reference['localUnknowns']),
                    f'{label}: topology dimensions')
            resolved = {(item['parameter']['index'], item['scope']): item
                        for item in report['prepared']['resolvedParameters']}
            for parameter in reference['parameters']:
                for scope in ['model', 'instance']:
                    if scope + 'Value' not in parameter:
                        continue
                    actual = resolved[parameter['index'], scope]
                    require(actual['value'] == parameter[scope + 'Value'] and
                            actual['isGiven'] == parameter[scope + 'Given'] and
                            actual['parameter']['names'] == parameter['names'],
                            f'{label}: parameter {parameter["index"]} {scope}')
            evaluation = report['evaluation']
            zero_canonicalizations = []
            for field in ['f', 'q', 'g', 'c', 'nextState']:
                zero_canonicalizations.extend(exact(evaluation[field], reference[field], f'{label}.{field}'))
            exact(evaluation['noiseDensities'], [abs(value) for value in reference['noiseDensities']],
                  f'{label}.fixedBiasPSD')
            require(evaluation['modelFlags'] == reference['modelFlags'] and
                    evaluation.get('maximumTimestep') == reference.get('maximumTimestep'),
                    f'{label}: flags or timestep bound')
            require(len(report['prepared']['noiseSources']) == len(evaluation['noiseDensities'])
                    if mode == 'noise' else not evaluation['noiseDensities'], f'{label}: noise dimensions')
            signed_noise = [{'index': index, 'rawSignedValue': value, 'source': reference['noiseSources'][index]}
                            for index, value in enumerate(reference['noiseDensities']) if value < 0]
            checks.append({'caseID': identifier, 'context': context, 'requestSHA256': digest(request_path),
                           'reportSHA256': digest(report_path), 'status': 'passed',
                           'noiseTransform': 'fixed-bias-small-signal-abs', 'signedDirectABINoise': signed_noise,
                           'jsonSignedZeroCanonicalizations': zero_canonicalizations})
    summary = {'schema': 'osdi.ordinary-cli-qualification.v1', 'status': 'passed',
               'manifestSHA256': digest(args.manifest), 'cliSHA256': digest(args.cli),
               'oracleSHA256': digest(args.oracle), 'caseCount': len(points),
               'persistenceScope': 'Ordinary schema 2 JSON numbers retain finite nonzero binary64 bits; signed zero is numerically equal and encoder-canonicalized. API bits and precision explicit bit patterns are checked separately.',
               'requiredLibraryIDs': sorted(required_families),
               'excludedLibraryIDs': sorted(set(libraries) - required_families),
               'toolSHA256': digest(__file__),
               'evaluatedContextCount': sum(check['status'] == 'passed' for check in checks),
               'signedNoiseContextCount': sum(bool(check.get('signedDirectABINoise')) for check in checks),
               'rejectedRawNoiseCount': sum(check['status'] == 'consumer-rejected-raw-noise' for check in checks),
               'checks': checks, 'commands': receipts,
               'qualificationScope': 'Exact artifact ABI/CLI consumer regression using fixed-bias small-signal PSD abs(raw signed OSDI noise) with typed nonfinite refusal; no physical qualification claim'}
    summary['modelNoiseQualificationStatus'] = ('incomplete-nonfinite-noise' if summary['rejectedRawNoiseCount']
                                                 else 'passed-at-sampled-contexts')
    save(args.output/'verification.json', summary)
    print(json.dumps({'consumerStatus': summary['status'], 'cases': len(points), 'contexts': len(checks),
                      'modelNoiseQualificationStatus': summary['modelNoiseQualificationStatus'],
                      'rejectedRawNoiseCount': summary['rejectedRawNoiseCount'],
                      'rejectedContexts': [{'caseID': check['caseID'], 'context': check['context'],
                                            'rejectedDirectABINoise': check['rejectedDirectABINoise']}
                                           for check in checks if check['status'] == 'consumer-rejected-raw-noise'],
                      'verification': str(args.output/'verification.json')}))


if __name__ == '__main__':
    parser = ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--cli', type=Path, required=True)
    parser.add_argument('--oracle', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--library-ids', nargs='+',
                        help='Explicit real-model library IDs; defaults to the retained baseline matrix plus advertised SOI 101')
    verify(parser.parse_args())
