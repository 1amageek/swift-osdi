#!/usr/bin/env python3
"""Replay exact production snapshots through the worker at explicit raw points."""
import argparse
import hashlib
import json
import pathlib
import time
from verify_worker import Worker

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--worker', required=True, type=pathlib.Path)
    p.add_argument('--snapshots', required=True, type=pathlib.Path)
    p.add_argument('--output', required=True, type=pathlib.Path)
    p.add_argument('--allocation-probe', type=pathlib.Path)
    p.add_argument('--numeric-profile', type=pathlib.Path)
    a = p.parse_args()
    report = {'schema': 1, 'scope': 'Explicit raw point evaluation; not a circuit solution or physical qualification.',
              'workerSHA256': sha(a.worker), 'families': {}}
    if a.numeric_profile:
        profile = json.loads(a.numeric_profile.read_text())
        assert profile['worker']['sha256'] == report['workerSHA256']
        report['numericProfileSHA256'] = sha(a.numeric_profile)
    a.output.parent.mkdir(parents=True, exist_ok=True)
    for family in ['bulk', 'cmg', 'img', 'soi']:
        base = a.snapshots / family
        source_report = json.loads(base.with_name(family + '-report.json').read_text())
        topology = source_report['program']['topology']
        high = [0.0] * topology['activeUnknownCount']
        low = high.copy()
        polarity = -1 if family == 'bulk' else 1
        for node in topology['nodes']:
            index = node.get('activeUnknownIndex')
            if index is None or node['isFlow']:
                continue
            name = node['name'].lower()
            if name in ['d', 'di', 'di1', 'di2', 'ddbulk']:
                high[index] = polarity * .1
            elif name in ['g', 'fg', 'gi', 'ge', 'gm', 'gint', 'gints', 'gintd']:
                high[index] = polarity * .6
                low[index] = polarity * 2**-60
            elif name == 't':
                high[index] = 4.0
                low[index] = 2**-60
        vectors = [high, low, high.copy(), [0.0] * len(high)]
        assert all(h + l == h for h, l in zip(vectors[0], vectors[1]))
        assert all(h + l == h for h, l in zip(vectors[2], vectors[3]))
        contexts = [{'mode': 2, 'high': 1e-12, 'low': 0.0}, {'mode': 2, 'high': 0.0, 'low': 0.0}]
        entry = {'programSHA256': sha(base.with_suffix('.program')),
                 'semanticProfileSHA256': base.with_suffix('.program').read_bytes()[40:72].hex(), 'setupSHA256': sha(base.with_suffix('.setup')),
                 'programPath': str(base.with_suffix('.program').resolve()), 'setupPath': str(base.with_suffix('.setup').resolve()),
                 'vectors': vectors, 'contexts': contexts, 'precisionBits': 256, 'arenaBytes': 64 << 20, 'maximumWork': 100000000}
        report['families'][family] = entry
        w = Worker(a.worker, base.with_suffix('.program').read_bytes(), setup_bytes=base.with_suffix('.setup').read_bytes(),
                   arena=entry['arenaBytes'], work=entry['maximumWork'], precision=256, allocation_probe=a.allocation_probe)
        start = time.perf_counter()
        entry['readyStatus'] = w.start()
        entry['readySeconds'] = time.perf_counter() - start
        if entry['readyStatus'] == 0:
            for target, context in zip([w.control.candidate, w.control.reference], contexts):
                target.mode, target.high, target.low = context['mode'], context['high'], context['low']
            del target
            start = time.perf_counter()
            result = w.evaluate_vectors(vectors)
            entry['firstRequestSeconds'] = time.perf_counter() - start
            entry['status'] = w.control.status
            entry['detail'] = w.control.detail
            entry['work'] = w.control.work
            entry['outputs'] = result if isinstance(result, list) else None
            if isinstance(result, list):
                start = time.perf_counter()
                repeated = w.evaluate_vectors(vectors)
                entry['secondRequestSeconds'] = time.perf_counter() - start
                assert repeated == result
        entry['allocationSamples'] = w.allocationSamples
        entry['exit'] = w.close()
        a.output.write_text(json.dumps(report, sort_keys=True, indent=2) + '\n')
        print(family, entry.get('readyStatus'), entry.get('status'), entry.get('detail'), entry.get('firstRequestSeconds'), flush=True)
    report['succeeded'] = all(v.get('readyStatus') == 0 and v.get('status') == 0 and v.get('outputs') is not None and v['exit'] == 0 for v in report['families'].values())
    a.output.write_text(json.dumps(report, sort_keys=True, indent=2) + '\n')
    if not report['succeeded']:
        raise SystemExit(1)

if __name__ == '__main__':
    main()
