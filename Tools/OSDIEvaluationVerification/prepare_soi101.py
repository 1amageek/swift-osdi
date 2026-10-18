#!/usr/bin/env python3
"""Prepare explicit SOI 101.0.0 biased points using the public CLI topology."""
from argparse import ArgumentParser
from pathlib import Path
import json

from build import digest, run, save


SOURCE_SHA = 'cba0d2de068e86b026d9580c9d8ab78f592ccc3d9dc8efb98fb5d67182be0a87'
IMAGE_SHA = 'a8f1fb4e3913eb70a62c0b9972c92cec775ef4d5344919f3c13beb896d4afe3c'


def require(condition, detail):
    if not condition:
        raise RuntimeError(detail)


def parameter(scope, name, value):
    kind = 'integer' if isinstance(value, int) else 'real'
    return {'scope': scope, 'name': name, 'value': {'kind': kind, kind: value}}


def specifications():
    cases = []
    for sign, polarity in [(1, 'n'), (-1, 'p')]:
        for mode, body, reverse, thermal, series, label in [
                (0, 0, False, False, False, 'pd-forward'),
                (0, 0, True, False, False, 'pd-reverse'),
                (1, 0, False, False, False, 'dd-forward'),
                (1, 2, False, False, False, 'dd-contact'),
                (1, 2, False, True, True, 'dd-private-thermal-internal')]:
            values = {'d': sign * (-0.1 if reverse else 0.1), 'g': sign * 0.8,
                      's': 0.0, 'e': 0.0, 'b': 0.0, 't': 4.0 if thermal else 0.0,
                      'di': sign * 0.1, 'si': 0.0, 'gi': sign * 0.78,
                      'gm': sign * 0.79, 'bi': sign * 0.01,
                      'bi2': sign * 0.005, 'N1': 0.0, 'N2': 0.0}
            params = [parameter('instance', 'L', 1e-6), parameter('instance', 'W', 1e-5),
                      parameter('model', 'TYPE', sign), parameter('model', 'SOIMOD', mode),
                      parameter('model', 'BODYMOD', body), parameter('model', 'SHMOD', int(thermal)),
                      parameter('instance', 'TNODEOUT', 0),
                      parameter('model', 'RDSMOD', 2 if series else 0),
                      parameter('instance', 'RGATEMOD', 3 if series else 0)]
            if thermal:
                params += [parameter('model', 'RTH0', 0.01), parameter('model', 'CTH0', 1e-5)]
            cases.append({'id': f'soi101-{polarity}-{label}', 'parameters': params,
                          'temperatureKelvin': 373.15 if thermal else 300.15,
                          'requestedRawPotentials': values,
                          'coverage': {'TYPE': sign, 'SOIMOD': mode, 'BODYMOD': body,
                                       'direction': 'reverse' if reverse else 'forward',
                                       'privateThermal': thermal, 'internalResistance': series}})
    require(len(cases) == len({case['id'] for case in cases}) == 10, 'Matrix case identity')
    return cases


def prepare(args):
    source, library, cli = [value.resolve(strict=True) for value in (args.source, args.library, args.cli)]
    require(digest(source) == SOURCE_SHA, 'SOI 101.0.0 unchanged official source identity mismatch')
    require(digest(library) == IMAGE_SHA, 'SOI 101.0.0 exact generated image identity mismatch')
    baseline = args.manifest.resolve(strict=True)
    manifest = json.loads(baseline.read_bytes())
    require(not any(item['id'] == 'soi101' for item in manifest['libraries']), 'Latest SOI already present')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    receipts, points, checks = [], [], []
    size = library.stat().st_size
    for specification in specifications():
        identifier = specification['id']
        request = {
            'schemaVersion': 2, 'operation': 'prepare', 'profile': 'dynamic-point',
            'context': {'mode': 'dc', 'timeSeconds': 0.0},
            'libraryPath': str(library), 'expectedSHA256': IMAGE_SHA, 'expectedByteCount': size,
            'limits': {'maximumDescriptors': 64, 'maximumMetadataBytes': 16_777_216,
                       'maximumStorageBytes': 67_108_864},
            'readBudget': {'maximumPageByteCount': 65_536, 'maximumTotalByteCount': size,
                           'maximumPageCount': (size + 65_535)//65_536,
                           'maximumWorkUnitCount': 2*((size + 65_535)//65_536),
                           'maximumDurationNanoseconds': 10_000_000_000},
            'descriptorIndex': 0, 'parameters': specification['parameters'],
            'simulationParameters': [parameter('model', 'gmin', 0.0)],
            'temperatureKelvin': specification['temperatureKelvin'], 'connectedTerminalCount': 5,
            'evaluationLimits': {'maximumWorkspaceBytes': 16_777_216, 'maximumDiagnosticCount': 128,
                                 'maximumDiagnosticBytes': 65_536, 'maximumStringBytes': 1_048_576}}
        # Simulation parameters have no model/instance scope in the CLI contract.
        del request['simulationParameters'][0]['scope']
        request_path, report_path = output/(identifier+'-prepare-request.json'), output/(identifier+'-prepare-report.json')
        save(request_path, request)
        run([cli, '--request', request_path, '--output', report_path], output, identifier+'-prepare', receipts)
        report = json.loads(report_path.read_bytes())
        require(report['succeeded'] and report['input'] == request, identifier+': public prepare')
        require(report['requestBinding'] == {'sha256': digest(request_path), 'byteCount': request_path.stat().st_size},
                identifier+': request binding')
        require(report['artifactBinding'] == {'sha256': IMAGE_SHA, 'byteCount': size}, identifier+': image binding')
        require(report['descriptor']['terminalCount'] == 6 and report['descriptor']['nodeCount'] == 14,
                identifier+': latest six-terminal descriptor')
        topology = report['prepared']['topology']
        nodes = topology['nodes']
        raw_values = specification['requestedRawPotentials']
        require(set(raw_values) == {node['name'] for node in nodes}, identifier+': explicit raw point coverage')
        groups = {}
        for node in nodes:
            if 'activeUnknownIndex' in node:
                groups.setdefault(node['activeUnknownIndex'], []).append(node)
        require(set(groups) == set(range(topology['activeUnknownCount'])), identifier+': dense active topology')
        # The first descriptor node owns each alias group's fixture value. This gives
        # terminals authority over collapsed internal aliases without solving KCL.
        values = {index: raw_values[group[0]['name']] for index, group in groups.items()}
        unknowns = [{'id': f'explicit:{index}', 'value': values[index],
                     'unit': groups[index][0]['units']} for index in sorted(groups)]
        raw_nodes = [{'index': node['index'], 'name': node['name'],
                      'unknownID': f'explicit:{node["activeUnknownIndex"]}' if 'activeUnknownIndex' in node else None,
                      'unit': node['units'], 'residualUnit': node['residualUnits'], 'isFlow': node['isFlow']}
                     for node in nodes]
        actual_raw = {node['name']: values[node['activeUnknownIndex']] if 'activeUnknownIndex' in node else 0.0
                      for node in nodes}
        thermal = specification['coverage']['privateThermal']
        require(actual_raw['t'] == (4.0 if thermal else 0.0), identifier+': private thermal point')
        mapping = {node['name']: node.get('activeUnknownIndex') for node in nodes}
        contact = specification['coverage']['BODYMOD'] == 2
        require((mapping['b'] != mapping['bi'] and mapping['bi'] != mapping['bi2']) if contact
                else mapping['b'] == mapping['bi'] == mapping['bi2'], identifier+': body topology')
        require((mapping['g'] != mapping['gm'] and mapping['gm'] != mapping['gi']) if thermal
                else mapping['g'] == mapping['gm'] == mapping['gi'], identifier+': gate topology')
        require(mapping['d'] == mapping['di'] and mapping['s'] == mapping['si'],
                identifier+': collapsed external series terminals')
        device = {'id': 'M1', 'library': 'soi101', 'descriptorIndex': 0,
                  'temperatureKelvin': specification['temperatureKelvin'], 'connectedTerminalCount': 5,
                  'parameters': specification['parameters'], 'previousState': [0.0]*topology['stateCount']}
        points.append({'id': identifier, 'input': {'devices': [device]},
                       'reference': {'schema': 'osdi.explicit-biased-point.v1', 'status': 'explicit-biased-point',
                                     'qualificationScope': 'Point evaluation only; no circuit solution or trajectory',
                                     'unknowns': unknowns, 'devices': [{'id': 'M1', 'rawNodes': raw_nodes}]},
                       'coverage': specification['coverage'], 'requestedRawPotentials': raw_values,
                       'rawPotentials': actual_raw, 'collapseMap': [node.get('activeUnknownIndex') for node in nodes]})
        checks.append({'caseID': identifier, 'prepareRequestSHA256': digest(request_path),
                       'prepareReportSHA256': digest(report_path), 'activeUnknownCount': len(groups),
                       'stateCount': topology['stateCount'], 'diagnostics': report['prepared']['diagnostics']})
    manifest['libraries'].append({'id': 'soi101', 'path': str(library), 'sha256': IMAGE_SHA,
                                 'descriptorIndex': 0, 'descriptorName': 'bsimsoi'})
    manifest['cases'].extend(points)
    manifest['status'] = 'prepared-not-evaluation-qualified'
    manifest['qualificationComplete'] = False
    manifest['soi101PointPreparation'] = {
        'schema': 'osdi.soi101-explicit-points.v1', 'status': 'prepared-not-evaluation-qualified',
        'baselineManifest': {'path': str(baseline), 'sha256': digest(baseline)},
        'source': {'path': str(source), 'sha256': SOURCE_SHA, 'release': '101.0.0', 'releaseDate': '2026-09-15'},
        'artifact': {'path': str(library), 'sha256': IMAGE_SHA, 'byteCount': size},
        'cli': {'path': str(cli), 'sha256': digest(cli)}, 'toolSHA256': digest(__file__),
        'caseCount': len(points), 'checks': checks, 'commands': receipts,
        'qualificationScope': 'Explicit biased points with public prepared topology; no KCL solve, transient trajectory, physical model qualification or foundry qualification'}
    save(output/'manifest.json', manifest)
    save(output/'matrix.json', {'points': points, 'preparation': manifest['soi101PointPreparation']})
    print(json.dumps({'manifest': str(output/'manifest.json'), 'matrix': str(output/'matrix.json'),
                      'newCaseCount': len(points), 'retainedBaselineCaseCount': len(manifest['cases'])-len(points),
                      'status': 'prepared-not-evaluation-qualified'}))


if __name__ == '__main__':
    parser = ArgumentParser(description=__doc__)
    for argument in ('source', 'library', 'cli', 'manifest', 'output'):
        parser.add_argument('--'+argument, type=Path, required=True)
    prepare(parser.parse_args())
