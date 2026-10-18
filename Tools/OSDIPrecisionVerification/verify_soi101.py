#!/usr/bin/env python3
"""Bounded public paired-point qualification of the authentic SOI 101 image."""
if not __debug__:
    raise RuntimeError('SOI qualification requires assertion-enabled Python; optimized execution is unsupported')

import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def save(path, value):
    Path(path).write_text(json.dumps(value, indent=2, allow_nan=False) + '\n')


SOI_CASE_IDS = {'soi101-n-pd', 'soi101-n-dd', 'soi101-p-pd', 'soi101-p-dd'}
PAIRED_FIELDS = ['candidateHigh', 'candidateLow', 'referenceHigh', 'referenceLow', 'candidateContext', 'referenceContext']


def validate_cold_refusal_pairs(pairs):
    if len(pairs) != 4 or {x['id'] for x in pairs} != SOI_CASE_IDS:
        raise ValueError('cold_refusal_case_coverage')
    for pair in pairs:
        cold = json.loads(pair['requestBytes'])
        report = json.loads(pair['reportBytes'])
        success = json.loads(pair['successBytes'])
        binding = {'sha256': hashlib.sha256(pair['requestBytes']).hexdigest(), 'byteCount': len(pair['requestBytes'])}
        precision = report.get('failure', {}).get('precision', {})
        if (report.get('requestBinding') != binding or report.get('succeeded') is not False or
                report.get('failure', {}).get('reason') != 'precision_resourceLimit' or
                precision.get('code') != 2 or precision.get('exitCode') != 80 or
                report.get('evaluation') is not None or report.get('program') is not None):
            raise ValueError('cold_refusal_report_binding')
        if (cold.get('operation') != 'snapshot' or cold['options']['arenaBytes'] != 64 << 20 or
                success.get('operation') != 'evaluate' or success['options']['arenaBytes'] != 256 << 20 or
                any(key in cold for key in PAIRED_FIELDS)):
            raise ValueError('cold_refusal_operating_profile')
        expected = copy.deepcopy(success)
        expected['operation'] = 'snapshot'
        expected['options']['arenaBytes'] = 64 << 20
        for key in PAIRED_FIELDS:
            expected.pop(key, None)
        expected.pop('stagingParentDirectory', None)
        cold.pop('stagingParentDirectory', None)
        if cold != expected:
            raise ValueError('cold_refusal_success_identity')


def cold_refusal_controls(pairs):
    validate_cold_refusal_pairs(pairs)
    altered = copy.deepcopy(pairs)
    request = json.loads(altered[0]['requestBytes'])
    parameter = next(p for p in request['model']['parameters'] if p['name'] == 'TYPE')
    parameter['value']['integer'] *= -1
    altered[0]['requestBytes'] = json.dumps(request).encode()
    report = json.loads(altered[0]['reportBytes'])
    report['requestBinding'] = {'sha256': hashlib.sha256(altered[0]['requestBytes']).hexdigest(),
                                'byteCount': len(altered[0]['requestBytes'])}
    altered[0]['reportBytes'] = json.dumps(report).encode()
    substituted = copy.deepcopy(pairs)
    substituted[0]['requestBytes'] = pairs[1]['requestBytes']
    substituted[0]['reportBytes'] = pairs[1]['reportBytes']
    results = []
    for name, values in [('missing-case', pairs[:-1]), ('altered-type-and-rebound-report', altered),
                         ('substituted-bound-request-report', substituted)]:
        try:
            validate_cold_refusal_pairs(values)
        except ValueError as error:
            results.append({'case': name, 'passed': True, 'reason': str(error)})
        else:
            raise AssertionError('cold_refusal_negative_control_accepted:' + name)
    return results


def finalize(driver, point, admission_path, comparison_path, cold_refusals):
    original = json.loads((point / 'receipt.json').read_text())
    comparison = json.loads(Path(comparison_path).read_text())
    model = json.loads((point / 'model-verification.json').read_text())
    assert model['succeeded'] and comparison['passed']
    assert comparison['workerReport']['sha256'] == sha(point / 'model-verification.json')
    admission = json.loads(Path(admission_path).read_text())
    assert admission['status'] == 'passed' and admission['binary']['sha256'] == comparison['independentBinary']['sha256']
    controls_path = point / 'independent-controls/verification.json'
    independent_controls = json.loads(controls_path.read_text())
    assert independent_controls['passed'] and independent_controls['binary']['sha256'] == comparison['independentBinary']['sha256']
    compiler_receipt = json.loads(Path(original['inputs']['compile_receipt']['path']).read_text())
    import native_build
    compiler = native_build.identity(compiler_receipt['command'][0])
    assert compiler['sha256'] == compiler_receipt['compilerSHA256']
    pairs, cold_records = [], []
    for report_path in cold_refusals:
        report_path = Path(report_path)
        suffix = '-snapshot-report.json'
        if not report_path.name.endswith(suffix):
            raise ValueError('cold_refusal_report_name')
        name = report_path.name[:-len(suffix)]
        request_path = report_path.with_name(name + '-snapshot-request.json')
        pairs.append({'id': name, 'requestBytes': request_path.read_bytes(), 'reportBytes': report_path.read_bytes(),
                      'successBytes': (point / (name + '-optimized-request.json')).read_bytes()})
        cold_records.append({'id': name, 'request': driver.identity(request_path), 'report': driver.identity(report_path)})
    controls = cold_refusal_controls(pairs)
    value = {'schema': 'osdi.soi101.precision-qualified.v2', 'passed': True,
             'source': original['inputs']['source'], 'model': original['inputs']['model'],
             'compileReceipt': original['inputs']['compile_receipt'], 'compiler': compiler,
             'acquisition': original['inputs']['acquisition'], 'semanticProfile': original['inputs']['semantic_profile'],
             'selectedWorker': original['workers']['optimized']['image'],
             'selectedNumericProfile': original['workers']['optimized']['profile'],
             'modelVerification': driver.identity(point / 'model-verification.json'),
             'independentAdmission': driver.identity(admission_path),
             'independentControls': driver.identity(controls_path),
             'independentComparison': driver.identity(comparison_path),
             'coldResourceRefusals': cold_records, 'coldRefusalValidationControls': controls,
             'cases': [{'id': name, 'request': driver.identity(point / (name + '-optimized-request.json')),
                        'report': driver.identity(point / (name + '-optimized-report.json'))}
                       for name in model['families']]}
    save(point / 'latest-qualification.json', value)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['cli', 'model', 'preparation', 'compile-receipt', 'source', 'acquisition',
                 'baseline-worker', 'baseline-profile', 'optimized-worker', 'optimized-profile',
                 'semantic-profile', 'output']:
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--arena-bytes', type=int, required=True)
    parser.add_argument('--oracle-register-limit', type=int, required=True)
    parser.add_argument('--cold-resource-refusal', type=Path, action='append', required=True)
    import native_build
    native_build.arguments(parser)
    args = parser.parse_args()
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import verify_cli
    import independent_replay as driver
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    staging = out / 'staging'
    staging.mkdir()
    compile_receipt = json.loads(args.compile_receipt.read_text())
    acquisition = json.loads(args.acquisition.read_text())
    assert sha(args.model) == compile_receipt['imageSHA256']
    assert sha(args.source) == compile_receipt['sourceSHA256']
    assert any(x['sha256'] == sha(args.source) for x in acquisition['files'])
    semantic = sha(args.semantic_profile)
    assert semantic == compile_receipt['semanticProfileSHA256']
    workers = {}
    for name in ['baseline', 'optimized']:
        worker, profile = getattr(args, name + '_worker'), getattr(args, name + '_profile')
        numeric = json.loads(profile.read_text())
        assert numeric['worker']['sha256'] == sha(worker)
        workers[name] = {'path': str(worker.resolve()), 'numeric': sha(profile),
                         'image': driver.identity(worker), 'profile': driver.identity(profile)}
    template = json.loads(args.preparation.read_text())
    assert template['expectedSHA256'] == sha(args.model)
    commands, cases = [], []
    worker_report = {'schema': 1, 'succeeded': False, 'families': {},
                     'scope': 'SOI 101 n/p PD/DD explicit paired raw points; no full circuit or physical qualification.',
                     'workerSHA256': workers['optimized']['image']['sha256'],
                     'numericProfileSHA256': workers['optimized']['numeric']}
    inputs = {key: driver.identity(getattr(args, key)) for key in
              ['cli', 'model', 'preparation', 'source', 'acquisition', 'compile_receipt',
               'semantic_profile']}

    def run(name, request):
        request_path, report_path = out / (name + '-request.json'), out / (name + '-report.json')
        save(request_path, request)
        argv = [str(args.cli.resolve()), '--precision-request', str(request_path), '--output', str(report_path)]
        started = time.monotonic()
        with (out / (name + '.stdout')).open('xb') as stdout, (out / (name + '.stderr')).open('xb') as stderr:
            child = subprocess.Popen(argv, stdout=stdout, stderr=stderr, start_new_session=True)
            try:
                code = child.wait(timeout=30)
            except BaseException as error:
                try:
                    os.killpg(child.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                child.wait(timeout=5)
                if not isinstance(error, subprocess.TimeoutExpired):
                    raise
                code = 124
        command = {'case': name, 'argv': argv, 'exitCode': code,
                   'seconds': time.monotonic() - started, 'timeoutSeconds': 30,
                   'request': driver.identity(request_path)}
        if report_path.exists():
            command['report'] = driver.identity(report_path)
        commands.append(command)
        save(out / 'commands.json', commands)
        if code != 0:
            raise RuntimeError('public_cli_refusal:' + name + ':' + str(code))
        report = verify_cli.load_json(report_path.read_bytes())
        assert report['succeeded'] and report['input'] == request
        assert report['requestBinding'] == {'sha256': sha(request_path), 'byteCount': request_path.stat().st_size}
        verify_cli.check_bits(report)
        assert not list(staging.iterdir())
        return report

    for polarity, label in [(1, 'n'), (-1, 'p')]:
        for mode, mode_label in [(0, 'pd'), (1, 'dd')]:
            case_id = 'soi101-' + label + '-' + mode_label
            case = {'id': case_id, 'status': 'failed'}
            cases.append(case)
            try:
                model = copy.deepcopy(template)
                model['libraryPath'] = str(args.model.resolve())
                model['connectedTerminalCount'] = 5
                model['temperatureKelvin'] = 330.0
                model['parameters'] = []
                parameters = [('L', 'instance', 1e-6), ('W', 'instance', 1e-5),
                              ('TYPE', 'model', polarity), ('SOIMOD', 'model', mode),
                              ('BODYMOD', 'model', 2), ('SHMOD', 'model', 1),
                              ('RTH0', 'model', 1000.0), ('CTH0', 'model', 1e-9),
                              ('RDSMOD', 'model', 2), ('RGATEMOD', 'instance', 3),
                              ('TNODEOUT', 'instance', 0)]
                for name, scope, value in parameters:
                    kind = 'integer' if isinstance(value, int) else 'real'
                    model['parameters'].append({'name': name, 'scope': scope, 'value': {'kind': kind, kind: value}})
                model['simulationParameters'] = [{'name': 'gmin', 'value': {'kind': 'real', 'real': 0.0}}]
                model['limits'].update(maximumMetadataBytes=16777216, maximumStorageBytes=67108864)
                model['evaluationLimits'].update(maximumWorkspaceBytes=67108864, maximumDiagnosticCount=1024,
                                                 maximumStringBytes=1048576)
                optimized = workers['optimized']
                request = verify_cli.request(model, optimized['path'], semantic, optimized['numeric'], staging, 'snapshot')
                request['options']['arenaBytes'] = args.arena_bytes
                snapshot = run(case_id + '-snapshot', request)
                program = snapshot['program']
                topology = program['topology']
                assert topology['stateCount'] == 0
                case['topology'] = topology
                # Exact names originate in the inspected six-terminal model declaration and body.
                values = {'d': polarity*.1, 'di': polarity*.1, 'g': polarity*.6,
                          'gm': polarity*.6, 'gi': polarity*.6, 's': 0.0, 'si': 0.0,
                          'e': 0.0, 'b': 0.0, 'bi': 0.0, 'bi2': 0.0, 't': 4.0,
                          'N1': 0.0, 'N2': 0.0}
                assert {n['name'] for n in topology['nodes']} == set(values)
                high, low = [None] * topology['activeUnknownCount'], [None] * topology['activeUnknownCount']
                for node in topology['nodes']:
                    assert not node['isFlow']
                    index = node.get('activeUnknownIndex')
                    if index is None:
                        continue
                    h = values[node['name']]
                    l = (polarity*2**-60 if node['name'] in ['g', 'gm', 'gi'] else
                         2**-60 if node['name'] == 't' else 0.0)
                    assert high[index] is None or (high[index], low[index]) == (h, l)
                    high[index], low[index] = h, l
                assert all(x is not None for x in high + low)
                assert all(h + l == h for h, l in zip(high, low))
                assert any(n['name'] == 't' and n.get('activeUnknownIndex') is not None for n in topology['nodes'])
                vectors = [high, low, high.copy(), [0.0] * len(high)]
                contexts = [{'mode': 2, 'high': 1e-12, 'low': 0.0}, {'mode': 2, 'high': 0.0, 'low': 0.0}]
                for key in ['program', 'setup']:
                    image = bytes(program[key + 'Bytes'])
                    assert hashlib.sha256(image).hexdigest() == program[key + 'SHA256']
                    (out / (case_id + '.' + key)).write_bytes(image)
                case['programSHA256'], case['setupSHA256'] = program['programSHA256'], program['setupSHA256']
                case['vectors'], case['contexts'] = vectors, contexts
                evaluations = {}
                keys = ['fHigh', 'fLow', 'fRadius', 'deltaQHigh', 'deltaQLow', 'deltaQRadius', 'g', 'c']
                for variant, worker in workers.items():
                    value = copy.deepcopy(request)
                    value.update(operation='evaluate', worker={'path': worker['path'],
                                 'reference': verify_cli.reference(worker['path']), 'readBudget': verify_cli.budget(worker['path'])},
                                 numericProfileSHA256=worker['numeric'])
                    for key, vector in zip(['candidateHigh', 'candidateLow', 'referenceHigh', 'referenceLow'], vectors):
                        value[key] = verify_cli.vector(vector)
                    for key, context in zip(['candidateContext', 'referenceContext'], contexts):
                        value[key] = {'mode': 'transient', 'timeHigh': context['high'], 'timeLow': context['low'],
                                      'timeHighBits': verify_cli.bits(context['high']), 'timeLowBits': verify_cli.bits(context['low'])}
                    report = run(case_id + '-' + variant, value)
                    assert report['program'] == program
                    assert report['workerReference'] == value['worker']['reference']
                    assert report['numericProfileSHA256'] == worker['numeric']
                    evaluations[variant] = report['evaluation']
                assert all(evaluations['baseline'][k]['bitPatterns'] == evaluations['optimized'][k]['bitPatterns'] for k in keys)
                evaluation = evaluations['optimized']
                worker_report['families'][case_id] = {'status': 0, 'exit': 0, 'precisionBits': 256,
                    'arenaBytes': args.arena_bytes, 'maximumWork': request['options']['maximumWorkUnits'],
                    'programPath': str(out / (case_id + '.program')), 'programSHA256': program['programSHA256'],
                    'setupPath': str(out / (case_id + '.setup')), 'setupSHA256': program['setupSHA256'],
                    'semanticProfileSHA256': semantic, 'vectors': vectors, 'contexts': contexts,
                    'outputs': [evaluation[k]['values'] for k in keys],
                    'work': evaluation['completedWorkUnits']}
                case['allEightOutputVectorsBitIdentical'] = True
                case['work'] = evaluation['completedWorkUnits']
                case['status'] = 'passed'
            except Exception as error:
                case['failure'] = type(error).__name__ + ': ' + str(error)
            save(out / 'case-results.json', cases)
    worker_report['succeeded'] = all(case['status'] == 'passed' for case in cases)
    save(out / 'model-verification.json', worker_report)
    independent_ok = False
    if worker_report['succeeded']:
        try:
            oracle = driver.build(out / 'independent-oracle', args)
            driver.selfchecks(oracle, out / 'independent-controls')
            driver.compare_worker(oracle, out / 'model-verification.json', out / 'independent')
            independent_ok = True
        except Exception as error:
            save(out / 'independent-failure.json', {'reason': type(error).__name__ + ': ' + str(error)})
    assert sha(args.cli) == inputs['cli']['sha256'], 'CLI changed during experiment'
    result = {'schema': 'osdi.soi101.precision-point-qualification.v1',
              'status': 'pending-finalization' if worker_report['succeeded'] and independent_ok else 'failed',
              'scope': worker_report['scope'], 'inputs': inputs, 'workers': workers,
              'cases': cases, 'independent512FDeltaQ': independent_ok,
              'commands': driver.identity(out / 'commands.json'),
              'modelVerification': driver.identity(out / 'model-verification.json')}
    if (out / 'independent/comparison.json').exists():
        result['independentComparison'] = driver.identity(out / 'independent/comparison.json')
    save(out / 'receipt.json', result)
    if result['status'] == 'pending-finalization':
        try:
            finalize(driver, out, out / 'independent-oracle/receipt.json', out / 'independent/comparison.json', args.cold_resource_refusal)
        except Exception as error:
            result['status'] = 'failed'
            result['finalizationFailure'] = type(error).__name__ + ': ' + str(error)
        else:
            result['status'] = 'passed'
        save(out / 'receipt.json', result)
    print(json.dumps({'status': result['status'], 'cases': [(c['id'], c['status'], c.get('failure')) for c in cases],
                      'independent512FDeltaQ': independent_ok, 'receipt': str(out / 'receipt.json')}))
    raise SystemExit(0 if result['status'] == 'passed' else 2)


if __name__ == '__main__':
    main()
