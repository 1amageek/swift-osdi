#!/usr/bin/env python3
"""Replay exact production snapshots through the worker at explicit raw points."""
import argparse
import hashlib
import json
import pathlib
import math
import struct
import sys
import time
from verify_worker import Worker

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def legacy_entries(directory):
    entries = {}
    for family in ['bulk', 'cmg', 'img', 'soi']:
        base = directory / family
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
        entries[family] = entry
    return entries

def load_entries(args):
    if args.manifest:
        manifest = json.loads(args.manifest.read_bytes())
        entries = manifest['families']
        if not isinstance(entries, dict) or not entries:
            raise ValueError('Snapshot manifest requires a nonempty families mapping')
        entries = {name: dict(value) for name, value in entries.items()}
    else:
        entries = legacy_entries(args.snapshots)
    selected = list(entries) if args.families is None else args.families
    if not selected or len(selected) != len(set(selected)) or any(name not in entries for name in selected):
        raise ValueError('Selected families must be unique entries in the supplied snapshot profile')
    for name in selected:
        if not name or pathlib.Path(name).name != name or name in ('.', '..'):
            raise ValueError('Family identity must be a plain path component')
        if args.manifest:
            for key in ('programPath', 'setupPath'):
                path = pathlib.Path(entries[name][key])
                entries[name][key] = str((args.manifest.parent / path).resolve(strict=True) if not path.is_absolute()
                                        else path.resolve(strict=True))
    return entries, selected


def validate(entry):
    for key, minimum, maximum in (('precisionBits', 128, (1 << 32) - 1),
                                  ('arenaBytes', 1, sys.maxsize), ('maximumWork', 1, (1 << 64) - 1)):
        if type(entry[key]) is not int or not minimum <= entry[key] <= maximum:
            raise ValueError('Snapshot control range: ' + key)
    vectors, contexts = entry['vectors'], entry['contexts']
    if len(vectors) != 4 or len(contexts) != 2 or len(vectors[0]) > 128:
        raise ValueError('Paired input shape')
    if any(len(vector) != len(vectors[0]) for vector in vectors):
        raise ValueError('Coordinate dimensions differ')
    if any(type(value) not in (int, float) or not math.isfinite(value) for vector in vectors for value in vector):
        raise ValueError('Coordinates must be finite numbers')
    for high, low in ((vectors[0], vectors[1]), (vectors[2], vectors[3])):
        if any(h + l != h for h, l in zip(high, low)):
            raise ValueError('Unnormalized twofold coordinates')
    for context in contexts:
        if type(context['mode']) is not int or context['mode'] not in (2, 3):
            raise ValueError('Unsupported precision context')
        if any(type(context[key]) not in (int, float) or not math.isfinite(context[key]) for key in ('high', 'low')):
            raise ValueError('Context time must be finite')
    for key in ('program', 'setup'):
        if sha(pathlib.Path(entry[key + 'Path'])) != entry[key + 'SHA256']:
            raise ValueError(key + ' identity mismatch')
    if pathlib.Path(entry['programPath']).read_bytes()[40:72].hex() != entry['semanticProfileSHA256']:
        raise ValueError('Program semantic profile identity mismatch')


def output_bits(rows):
    return b''.join(struct.pack('<' + 'd' * len(row), *row) for row in rows)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--worker', required=True, type=pathlib.Path)
    source = p.add_mutually_exclusive_group(required=True)
    source.add_argument('--snapshots', type=pathlib.Path, help='Legacy four-family directory, preserving SOI 4.7.0 regression')
    source.add_argument('--manifest', type=pathlib.Path, help='Explicit families mapping with exact paths/digests, vectors, contexts and limits')
    p.add_argument('--families', nargs='+', help='Explicit subset of entries; defaults to all manifest entries')
    p.add_argument('--output', required=True, type=pathlib.Path)
    p.add_argument('--allocation-probe', type=pathlib.Path)
    p.add_argument('--numeric-profile', type=pathlib.Path)
    a = p.parse_args()
    entries, selected = load_entries(a)
    for family in selected:
        validate(entries[family])
    report = {'schema': 1, 'scope': 'Explicit raw point evaluation; not a circuit solution or physical qualification.',
              'workerSHA256': sha(a.worker), 'families': {}, 'succeeded': False,
              'selectedFamilies': selected, 'excludedFamilies': [name for name in entries if name not in selected],
              'toolSHA256': sha(pathlib.Path(__file__))}
    if a.manifest:
        report['snapshotManifestSHA256'] = sha(a.manifest)
    if a.numeric_profile:
        profile = json.loads(a.numeric_profile.read_text())
        if profile['worker']['sha256'] != report['workerSHA256']:
            raise ValueError('Worker numeric-profile binding mismatch')
        report['numericProfileSHA256'] = sha(a.numeric_profile)
    a.output.parent.mkdir(parents=True, exist_ok=True)
    with a.output.open('x'):
        pass
    for family in selected:
        source_entry = entries[family]
        entry = {key: source_entry[key] for key in ('programPath', 'programSHA256', 'setupPath', 'setupSHA256',
                 'semanticProfileSHA256', 'vectors', 'contexts', 'precisionBits', 'arenaBytes', 'maximumWork')}
        report['families'][family] = entry
        program = pathlib.Path(entry['programPath']).read_bytes()
        setup = pathlib.Path(entry['setupPath']).read_bytes()
        if (hashlib.sha256(program).hexdigest() != entry['programSHA256'] or
                hashlib.sha256(setup).hexdigest() != entry['setupSHA256']):
            raise ValueError('Snapshot changed before worker admission')
        w = Worker(a.worker, program, setup_bytes=setup,
                   arena=entry['arenaBytes'], work=entry['maximumWork'], precision=entry['precisionBits'],
                   allocation_probe=a.allocation_probe)
        try:
            start = time.perf_counter()
            entry['readyStatus'] = w.start()
            entry['readySeconds'] = time.perf_counter() - start
            if entry['readyStatus'] == 0:
                for target, context in zip([w.control.candidate, w.control.reference], entry['contexts']):
                    target.mode, target.high, target.low = context['mode'], context['high'], context['low']
                del target
                start = time.perf_counter()
                result = w.evaluate_vectors(entry['vectors'])
                entry['firstRequestSeconds'] = time.perf_counter() - start
                entry['status'], entry['detail'] = w.control.status, w.control.detail
                entry['work'], entry['usedPrecisionBits'] = w.control.work, w.control.usedBits
                entry['outputs'] = result if isinstance(result, list) else None
                if isinstance(result, list) and entry['usedPrecisionBits'] != entry['precisionBits']:
                    raise AssertionError('Worker used a different precision than the explicit snapshot')
                if isinstance(result, list):
                    start = time.perf_counter()
                    repeated = w.evaluate_vectors(entry['vectors'])
                    entry['secondRequestSeconds'] = time.perf_counter() - start
                    if not isinstance(repeated, list) or output_bits(repeated) != output_bits(result):
                        raise AssertionError('Repeated point output bits changed')
                    if w.control.usedBits != entry['usedPrecisionBits'] or w.control.work != entry['work']:
                        raise AssertionError('Repeated point work or precision changed')
                if 'work' in source_entry and entry['work'] != source_entry['work']:
                    raise AssertionError('Completed work differs from the explicit snapshot')
        finally:
            entry['allocationSamples'] = w.allocationSamples
            entry['exit'] = w.close()
            a.output.write_text(json.dumps(report, sort_keys=True, indent=2, allow_nan=False) + '\n')
        print(family, entry.get('readyStatus'), entry.get('status'), entry.get('detail'), entry.get('firstRequestSeconds'), flush=True)
    if sha(a.worker) != report['workerSHA256']:
        raise AssertionError('Worker changed during replay')
    report['succeeded'] = all(v.get('readyStatus') == 0 and v.get('status') == 0 and
                              v.get('outputs') is not None and v['exit'] == 0 for v in report['families'].values())
    a.output.write_text(json.dumps(report, sort_keys=True, indent=2, allow_nan=False) + '\n')
    if not report['succeeded']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
