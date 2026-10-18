#!/usr/bin/env python3
"""Verify real-worker smooth-box bounds and source regularity with closed forms."""
import argparse
import copy
import ctypes as C
import decimal
import fcntl
import hashlib
import json
import math
import mmap
import os
from pathlib import Path
import struct
import tempfile

import verify_cli as CLI

from verify_worker import Control, Context, NONE, Region, Worker, blob, real, records, setup


def make_program(variant='polynomial', time_input=True):
    types, constants, inputs, instructions = [], [], [], []
    def register(kind=1):
        index = len(types);types.append(kind);return index
    def constant(value):
        index = register();constants.append((index, 1, real(value)));return index
    def operation(op, a, b=NONE, aux=NONE, kind=1):
        index = register(kind);instructions.append((op, index, a, b, aux, 0));return index
    zero, one, two, three = [constant(value) for value in (0., 1., 2., 3.)]
    x = register();inputs.append((2, x, 10, 0, 1 if variant == 'aliased_raw_abs' else (0 if variant == 'same_raw_abs' else NONE), 0))
    independent = variant in ('min', 'max', 'independent_equal')
    if independent:
        y = register();inputs.append((2, y, 10, 1, NONE, 0))
    if time_input:
        t = register();inputs.append((2, t, 4, 0, 0, 0))
    else:
        t = zero
    square = operation(46, x, x)
    cube = operation(46, square, x)
    f = operation(44, square, t)
    q = operation(44, cube, operation(46, two, t))
    g, c = operation(46, two, x), operation(46, three, square)
    if variant in ('floor', 'ceil', 'sqrt', 'log'):
        f = operation({'floor': 20, 'ceil': 21, 'sqrt': 13, 'log': 16}[variant], x)
        q = g = c = zero
    elif variant in ('cast', 'trunc'):
        integer = operation(5 if variant == 'cast' else 6, x, kind=2)
        f = operation(7, integer);q = g = c = zero
    elif variant == 'divide':
        f = operation(47, one, x);q = g = c = zero
    elif variant in ('switch', 'abs', 'min', 'max', 'independent_equal', 'aliased_raw_abs', 'same_raw_abs'):
        peer = y if independent else (one if variant == 'switch' else zero)
        comparison = operation(58 if variant == 'independent_equal' else 53, x, peer, kind=3)
        left, right = (square, operation(46, three, x)) if variant == 'switch' else (x, peer)
        if variant in ('abs', 'aliased_raw_abs', 'same_raw_abs'):left, right = operation(3, x), x
        if variant == 'max':left, right = peer, x
        f = operation(74, comparison, left, right);q = g = c = zero
    elif variant == 'inactive_floor':
        operation(20, x)
    raw = 2 if independent or variant == 'aliased_raw_abs' else 1
    roots = [(2, kind, value, 0, NONE if kind < 3 else 0, 0)
             for kind, value in enumerate((f, q, g, c), 1)]
    blocks = [(0, 0, 0, 0, 0, 0, NONE, NONE, NONE, 0),
              (1, 0, 0, 0, 0, 0, NONE, NONE, NONE, 0),
              (2, 0, len(instructions), 0, 0, 0, NONE, NONE, NONE, 0)]
    sections = [(8, records('II', [(kind, 0) for kind in types])),
                (16, records('IIQ', constants)), (24, records('IIIIII', inputs)),
                (24, records('IIIIII', [(i+1, i, i, 1, 0, 0) for i in range(3)])),
                (40, records('IIIIIIIIII', blocks)), (24, records('IIIIII', instructions)),
                (16, b''), (24, records('IIIIII', roots)), (32, b''),
                (16, records('IIII', [(i, i, 1, 0) for i in range(raw)])), (1, b'xy'[:raw])]
    data, descriptors = blob(248, sections)
    struct.pack_into('<8sIIQIIII32s', data, 0, b'OSDIPR1\0', 1, 248, len(data), 1, 0, raw, 0, bytes(32))
    for index, section in enumerate(descriptors):struct.pack_into('<QII', data, 72+16*index, *section)
    return bytes(data)


class RangeWorker(Worker):
    def __init__(self, path, tape, arena=1 << 20, work=100000, mapping=None, setup_bytes=None, precision=192, overlap=False):
        self.file = tempfile.TemporaryFile()
        self.inRead, self.inWrite = os.pipe();self.outRead, self.outWrite = os.pipe()
        self.raw = struct.unpack_from('<I', tape, 32)[0]
        setup_bytes = setup(self.raw, mapping=mapping) if setup_bytes is None else setup_bytes
        self.active = struct.unpack_from('<I', setup_bytes, 28)[0]
        payload = ([tape, setup_bytes] + [bytes(8*self.active)]*4 +
                   [bytes(8*self.raw)]*4 + [bytes(8*self.raw*self.raw)]*4)
        cursor, geometry = C.sizeof(Control), []
        for index, data in enumerate(payload):
            cursor = (cursor+7)//8*8;geometry.append((cursor, len(data) if index < 2 else len(data)//8));cursor += len(data)
        self.file.truncate(cursor);self.map = mmap.mmap(self.file.fileno(), cursor)
        self.control = Control.from_buffer(self.map);control = self.control
        control.magic = 0x313056504944534f;control.version = 2;control.headerBytes = 464
        control.mappingBytes = cursor;control.byteOrder = 0x01020304
        control.active = self.active;control.raw = self.raw;control.arena = arena
        control.minBits = 128;control.maxBits = control.bits = precision;control.maxWork = work
        control.response = 0xffffffffffffffff;control.candidate.mode = control.reference.mode = 2
        for index, ((offset, count), data) in enumerate(zip(geometry, payload)):
            control.regions[index] = Region(offset, count);self.map[offset:offset+len(data)] = data
        if overlap:
            control.regions[3] = Region(control.regions[2].offset, control.regions[2].count)
        copies = [fcntl.fcntl(fd, fcntl.F_DUPFD_CLOEXEC, 10) for fd in (self.file.fileno(), self.inRead, self.outWrite)]
        actions = [(os.POSIX_SPAWN_DUP2, fd, slot) for fd, slot in zip(copies, (3, 4, 5))]
        environment = {'PATH': '/usr/bin:/bin'}
        environment.update({key: os.environ[key] for key in ('ASAN_OPTIONS', 'UBSAN_OPTIONS') if key in os.environ})
        self.pid = os.posix_spawn(str(path), [str(path)], environment, file_actions=actions)
        for fd in copies:os.close(fd)
        os.close(self.inRead);os.close(self.outWrite)
        self.exit = None;self.statsRead = None;self.allocationSamples = []

    def box(self, lower, upper, lower_low=None, upper_low=None, times=(0., 0.)):
        self.control.candidate.high, self.control.reference.high = times
        self.control.candidate.low = self.control.reference.low = 0
        return self.evaluate_vectors([lower, lower_low or [0.]*self.active,
                                      upper, upper_low or [0.]*self.active])


def sha(data):return hashlib.sha256(data).hexdigest()


def emit_fixtures(directory):
    directory.mkdir(parents=True, exist_ok=False);fixtures = []
    for variant in ('polynomial', 'static_polynomial', 'floor', 'ceil', 'cast', 'trunc', 'switch',
                    'abs', 'min', 'max', 'sqrt', 'log', 'divide', 'independent_equal', 'inactive_floor'):
        tape = make_program('polynomial' if variant == 'static_polynomial' else variant,
                            time_input=variant == 'polynomial')
        raw = struct.unpack_from('<I', tape, 32)[0];state = setup(raw)
        program_path, setup_path = directory/(variant+'.program'), directory/(variant+'.setup')
        program_path.write_bytes(tape);setup_path.write_bytes(state)
        fixtures.append({'id': variant, 'programPath': str(program_path.resolve()), 'setupPath': str(setup_path.resolve()),
                         'programSHA256': sha(tape), 'setupSHA256': sha(state), 'rawNodeCount': raw,
                         'activeUnknownCount': raw, 'programHasTimeInput': variant == 'polynomial'})
    manifest = {'schema': 'osdi.range-fixtures.v1', 'source': 'F=x*x+t; Q=x*x*x+2*t; G=2*x; C=3*x*x',
                'fixtures': fixtures}
    (directory/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    return manifest


def verify(worker):
    decimal.getcontext().prec = 160;D = decimal.Decimal;checks = []
    def execute(variant, lower, upper, expected=0, mapping=None, **arguments):
        child = RangeWorker(worker, make_program(variant, variant == 'polynomial'), mapping=mapping)
        try:
            assert child.start() == 0, (variant, 'startup', child.control.status)
            result = child.box(lower, upper, **arguments)
            if expected:
                assert result == expected, (variant, result, expected)
            else:
                assert isinstance(result, list), (variant, result)
            checks.append({'id': variant, 'lower': lower, 'upper': upper, 'expectedStatus': expected,
                           'completedWork': child.control.work, 'usedBits': child.control.usedBits})
            return result
        finally:
            child.close()
    for lower, upper, tl, tu in [(.25, 2., .125, .75), (.75, 1.5, .5, 1.), (-2., -.5, 0., .25), (-1., 2., .125, .75)]:
        result = execute('polynomial', [lower], [upper], times=(tl, tu))
        x0, x1, t0, t1 = map(D.from_float, (lower, upper, tl, tu))
        square_min = min(x0*x0, x1*x1) if not x0 <= 0 <= x1 else D(0)
        square_max = max(x0*x0, x1*x1)
        bounds = [(square_min+t0, square_max+t1), (x0*x0*x0+2*t0, x1*x1*x1+2*t1),
                  (2*x0, 2*x1), (3*square_min, 3*square_max)]
        for index, (lo, hi) in enumerate(bounds):
            assert D.from_float(result[2*index][0]) <= lo <= hi <= D.from_float(result[2*index+1][0]), (index, result, bounds)
    result = execute('polynomial', [1.], [1.], lower_low=[2**-55], upper_low=[2**-54])
    x0, x1 = D(1)+D.from_float(2**-55), D(1)+D.from_float(2**-54)
    for index, (lo, hi) in enumerate(((x0*x0, x1*x1), (x0*x0*x0, x1*x1*x1), (2*x0, 2*x1), (3*x0*x0, 3*x1*x1))):
        assert D.from_float(result[2*index][0]) <= lo <= hi <= D.from_float(result[2*index+1][0])
    execute('aliased_raw_abs', [1.], [1.], 10, mapping=[0, 0])
    execute('same_raw_abs', [1.], [2.])
    execute('inactive_floor', [.5], [1.5])
    execute('floor', [.25], [.75])
    for variant, lower, upper in [('floor', [.5], [1.5]), ('ceil', [1.], [1.5]),
        ('cast', [.25], [.5]), ('trunc', [.5], [1.5]), ('switch', [.5], [1.]),
        ('abs', [0.], [1.]), ('min', [1., 1.], [1., 1.]), ('max', [1., 1.], [1., 1.]),
        ('independent_equal', [1., 1.], [1., 1.]), ('sqrt', [0.], [1.]),
        ('log', [0.], [1.]), ('divide', [0.], [1.])]:
        execute(variant, lower, upper, 10)
    execute('polynomial', [2.], [1.], 1)
    execute('polynomial', [1.], [2.], 1, times=(1., .5))
    execute('polynomial', [1.], [2.], 1, lower_low=[.5])
    child = RangeWorker(worker, make_program(), overlap=True)
    try:
        assert child.start() in (None, 1)
        checks.append({'id': 'overlapping-wire-inputs', 'expectedStatus': 'startup-refused'})
    finally:child.close()
    return checks


def proof_program(variant):
    """Build source instructions, including dynamic phi overwrites, not output fixtures."""
    types, constants, inputs, instructions, phis = [], [], [], [], []
    def register(kind=1):
        index = len(types);types.append(kind);return index
    def constant(value, kind=1):
        index = register(kind)
        constants.append((index, kind, real(value) if kind == 1 else int(value)))
        return index
    def operation(op, a, b=NONE, aux=NONE, kind=1):
        index = register(kind);instructions.append((op, index, a, b, aux, 0));return index
    zero, one = constant(0.), constant(1.)
    false, true = constant(False, 3), constant(True, 3)
    x = register();inputs.append((2, x, 10, 0, NONE, 0))
    independent = variant in ('active_zero', 'independent_box', 'independent_equalpoint', 'active_c', 'loop_stale')
    raw = 2 if independent or variant == 'alias_zero' else 1
    if independent:
        y = register();inputs.append((2, y, 10, 1, NONE, 0))
    blocks = [(i, 0, 0, 0, 0, 0, NONE, NONE, NONE, 0) for i in range(2)]
    g = zero
    if variant in ('zero', 'zero_invalid', 'zero_nonfinite', 'active_zero', 'alias_zero'):
        factor, operand = zero, x
        if variant == 'zero_invalid':operand = operation(16, constant(-1.))
        if variant == 'zero_nonfinite':operand = operation(14, constant(1e308))
        if variant == 'active_zero':factor = operation(45, x, y)
        if variant == 'alias_zero':
            factor = register();inputs.append((2, factor, 10, 0, 1, 0))
        product = operation(46, factor, operand)
        predicate = operation(58, product, zero, kind=3)
        f = operation(11, predicate)
        blocks.append((2, 0, len(instructions), 0, 0, 0, NONE, NONE, NONE, 0))
    else:
        source, final_x, c = x, x, one
        if variant == 'copy':source = operation(12, x)
        if variant == 'definite_select':source = operation(74, true, x, zero)
        if variant in ('independent_box', 'independent_equalpoint'):final_x = y
        if variant == 'active_c':c = y
        if variant == 'zero_c':c = zero
        if variant == 'negative_c':c = constant(-.5)
        if variant == 'phi':
            source = register();phis.append((source, 2, x, 0))
        if variant == 'loop_stale':
            source, iteration, retained = register(), register(3), register()
            final_x = source
        square = operation(46, source, source)
        shifted = operation(44, square, c)
        root = operation(13, shifted)
        final_root = retained if variant == 'loop_stale' else root
        sum_start = len(instructions)
        total = operation(44, final_x, final_root)
        predicate = operation(54, total, zero, kind=3)
        f = operation(74, predicate, total, zero)
        # The successful lane returns the actual spatial derivative, independently bounded below.
        g = operation(44, one, operation(47, final_x, final_root))
        if variant == 'phi':
            blocks += [(2, 0, 0, 0, 0, 1, NONE, 3, NONE, 0),
                       (2, 0, len(instructions), 0, 1, 0, NONE, NONE, NONE, 0)]
        elif variant == 'loop_stale':
            phis = [(source, 2, x, 0), (source, 4, y, 0),
                    (iteration, 2, false, 0), (iteration, 4, true, 0),
                    (retained, 2, zero, 0), (retained, 4, root, 0)]
            blocks += [(2, 0, 0, 0, 0, 1, NONE, 3, NONE, 0),
                       (2, 0, 0, 0, len(phis), 2, iteration, 5, 4, 0),
                       (2, 0, sum_start, 0, 0, 1, NONE, 3, NONE, 0),
                       (2, sum_start, len(instructions)-sum_start, 0, 0, 0, NONE, NONE, NONE, 0)]
        else:blocks.append((2, 0, len(instructions), 0, 0, 0, NONE, NONE, NONE, 0))
    roots = [(2, kind, value, 0, NONE if kind < 3 else 0, 0)
             for kind, value in enumerate((f, zero, g, zero), 1)]
    sections = [(8, records('II', [(kind, 0) for kind in types])),
                (16, records('IIQ', constants)), (24, records('IIIIII', inputs)),
                (24, records('IIIIII', [(1, 0, 0, 1, 0, 0), (2, 1, 1, 1, 0, 0),
                                       (3, 2, 2, len(blocks)-2, 0, 0)])),
                (40, records('IIIIIIIIII', blocks)), (24, records('IIIIII', instructions)),
                (16, records('IIII', phis)), (24, records('IIIIII', roots)), (32, b''),
                (16, records('IIII', [(i, i, 1, 0) for i in range(raw)])), (1, b'xy'[:raw])]
    data, descriptors = blob(248, sections)
    struct.pack_into('<8sIIQIIII32s', data, 0, b'OSDIPR1\0', 1, 248, len(data), 1, 0, raw, 0, bytes(32))
    for index, section in enumerate(descriptors):struct.pack_into('<QII', data, 72+16*index, *section)
    return bytes(data)


def verify_proofs(worker, selected=None, baseline_worker=None):
    """Retain observed failures in the receipt so a frozen-worker red is reviewable."""
    cases = [('zero', [-1.], [2.], 0), ('positive', [-2.], [-1.], 0),
             ('copy', [-2.], [-1.], 0), ('definite_select', [-2.], [-1.], 0),
             ('phi', [-2.], [-1.], 0), ('positive_mixed', [-.5], [1.], 0),
             ('zero_invalid', [-1.], [2.], 4), ('zero_nonfinite', [-1.], [2.], 8),
             ('active_zero', [1., 1.], [1., 1.], 10), ('alias_zero', [1.], [1.], 10),
             ('independent_box', [-2., -2.], [-1., -1.], 10),
             ('independent_equalpoint', [-2.**400, -2.**400], [-2.**400, -2.**400], 10),
             ('active_c', [-2., 1.], [-1., 1.], 10), ('zero_c', [-2.], [-1.], 10),
             ('negative_c', [-2.], [-1.], 10), ('loop_stale', [-2., -2.], [-1., -1.], 10),
             ('proof_budget', [-2.], [-1.], 7)]
    if selected:
        assert set(selected) <= {case[0] for case in cases}, selected
        cases = [case for case in cases if case[0] in selected]
    checks = []
    for name, lower, upper, expected in cases:
        variant = 'positive' if name in ('positive_mixed', 'proof_budget') else name
        tape = proof_program(variant);mapping = [0, 0] if name == 'alias_zero' else None
        raw = struct.unpack_from('<I', tape, 32)[0];state = setup(raw, mapping=mapping)
        check = {'id': name, 'expectedStatus': expected, 'lowerHigh': lower, 'upperHigh': upper,
                 'lowerLow': [0.]*len(lower), 'upperLow': [0.]*len(upper),
                 'programSHA256': sha(tape), 'setupSHA256': sha(state),
                 'programHex': tape.hex(), 'setupHex': state.hex(), 'passed': False}
        child = RangeWorker(worker, tape, setup_bytes=state, work=1000 if name == 'proof_budget' else 100000)
        try:
            startup = child.start();check['startupStatus'] = startup
            result = child.box(lower, upper) if startup == 0 else startup
            actual = 0 if isinstance(result, list) else result
            check.update(observedStatus=actual, actualBounds=result if isinstance(result, list) else None,
                         completedWork=child.control.work, usedBits=child.control.usedBits,
                         detailInstruction=child.control.detail, maximumWork=child.control.maxWork)
            assert actual == expected, (name, 'status', actual, expected)
            if name == 'proof_budget':
                # The bounded prefix probe must reach the final shared-sum FADD (instruction 3).
                # Retry exactly once with all but one of its 256 refinement units available.
                # An omitted charge advances to later instructions and fails these assertions.
                prefix = child.control.work
                assert startup == 0 and child.control.detail == 3, check
                check['prefixProbe'] = {'maximumWork': 1000, 'completedWork': prefix, 'detailInstruction': 3}
                bounded = RangeWorker(worker, tape, setup_bytes=state, work=prefix+255)
                try:
                    assert bounded.start() == 0
                    refusal = bounded.box(lower, upper)
                    check['proofChargeProbe'] = {'maximumWork': prefix+255, 'observedStatus': refusal,
                        'completedWork': bounded.control.work, 'detailInstruction': bounded.control.detail}
                    assert refusal == 7 and bounded.control.detail == 3 and bounded.control.work == prefix, check
                finally:bounded.close()
            if expected == 0:
                if name == 'zero':
                    assert result[0][0] == result[1][0] == 1., result
                    assert all(value == 0 for plane in result[2:] for value in plane), result
                else:
                    with decimal.localcontext() as context:
                        context.prec = 400;D = decimal.Decimal;one = D(1)
                        def exact(x):
                            x = D.from_float(x);r = (x*x+one).sqrt()
                            return (one/(r-x), one/(r*(r-x))) if x < 0 else (x+r, one+x/r)
                        endpoints = [exact(lower[0]), exact(upper[0])]
                        bounds = [(endpoints[0][0], endpoints[1][0]), (endpoints[0][1], endpoints[1][1])]
                        check['independentDecimalBounds'] = [[str(lo), str(hi)] for lo, hi in bounds]
                        for plane, (lo, hi) in zip((0, 4), bounds):
                            assert D.from_float(result[plane][0]) <= lo <= hi <= D.from_float(result[plane+1][0]), (name, plane, result, bounds)
                        assert result[0][0] > 0, (name, result)
                        # Tightening is observable through the exact directed binary64 F endpoints.
                        directed = []
                        for index, value in enumerate(bounds[0]):
                            rounded = float(value)
                            if (index == 0 and D.from_float(rounded) > value) or (index == 1 and D.from_float(rounded) < value):
                                rounded = math.nextafter(rounded, -math.inf if index == 0 else math.inf)
                            directed.append(rounded)
                        assert [result[0][0], result[1][0]] == directed, (name, result[:2], directed)
                        assert all(value == 0 for plane in (2, 3, 6, 7) for value in result[plane]), result
            check['passed'] = True
        except AssertionError as error:
            check['failure'] = str(error)
        finally:
            check['childExitCode'] = child.close()
        checks.append(check)
    if baseline_worker:
        # v1 must retain the existing point output bits and charged work exactly.
        for name in ('zero', 'positive'):
            observations = []
            for image in (baseline_worker, worker):
                child = Worker(image, proof_program(name))
                try:
                    assert child.start() == 0
                    result = child.evaluate_vectors([[-2.], [0.], [-2.], [0.]])
                    observations.append({'status': 0 if isinstance(result, list) else result,
                        'outputBits': [[CLI.bits(value) for value in plane] for plane in result] if isinstance(result, list) else None,
                        'completedWork': child.control.work, 'usedBits': child.control.usedBits})
                finally:child.close()
            checks.append({'id': 'v1-'+name, 'baseline': observations[0], 'current': observations[1],
                           'passed': observations[0] == observations[1]})
    return checks


def verify_cli_range(arguments):
    for value in (arguments.worker, arguments.model, arguments.semantic_profile, arguments.numeric_profile, arguments.cli_output):
        assert value is not None, 'Range CLI verification requires exact model/worker/profiles and fresh output.'
    numeric_bytes = arguments.numeric_profile.read_bytes()
    numeric = CLI.load_json(numeric_bytes)
    assert numeric['worker']['sha256'] == CLI.sha(arguments.worker)
    assert numeric['wireVersion'] == 1 and numeric['rangeWireVersion'] == 2
    assert numeric['rangeSemantics'] == 'source-program-smooth-box-v2'
    output = arguments.cli_output.resolve();output.mkdir(parents=True, exist_ok=False)
    staging = output/'staging';staging.mkdir()
    (output/'numeric-profile.json').write_bytes(numeric_bytes)
    (output/'semantic-profile.json').write_bytes(arguments.semantic_profile.read_bytes())
    original = CLI.request(CLI.ordinary(arguments.model), arguments.worker, CLI.sha(arguments.semantic_profile),
                           sha(numeric_bytes), staging, 'enclose')
    original.update(schemaVersion=2, profile='stateless-smooth-box-v2')
    def context(high, low=0.):
        return {'mode': 'transient', 'timeHigh': high, 'timeLow': low,
                'timeHighBits': CLI.bits(high), 'timeLowBits': CLI.bits(low)}
    original.update(lowerHigh=CLI.vector([.25]), lowerLow=CLI.vector([-0.0]),
                    upperHigh=CLI.vector([2.]), upperLow=CLI.vector([0.]),
                    lowerContext=context(.25, 2**-60), upperContext=context(.75, 2**-60))
    commands, checks = [], []
    D = decimal.Decimal.from_float
    for name, lo, lo_low, hi, hi_low in [('positive', .25, -0., 2., 0.),
        ('straddle-zero', -1., 0., 2., 0.), ('sub-ulp', 1., 2**-55, 1., 2**-54)]:
        request = copy.deepcopy(original)
        request.update(lowerHigh=CLI.vector([lo]), lowerLow=CLI.vector([lo_low]),
                       upperHigh=CLI.vector([hi]), upperLow=CLI.vector([hi_low]))
        request_path, report_path = output/(name+'-request.json'), output/(name+'-report.json')
        CLI.save(request_path, request)
        command, report = CLI.run(arguments.executable, request_path, report_path);commands.append(command)
        assert command['exitCode'] == 0 and report['succeeded'], (name, command, report)
        assert report['schemaVersion'] == 2 and report['stage'] == 'osdi-smooth-range-enclosure'
        assert report['input'] == request and report.get('evaluation') is None
        program = report['program']
        assert sha(bytes(program['programBytes'])) == program['programSHA256']
        assert sha(bytes(program['setupBytes'])) == program['setupSHA256']
        assert program['modelReference']['id'] == CLI.reference(arguments.model)['id']
        assert program['semanticProfileSHA256'] == request['semanticProfileSHA256']
        assert report['workerReference'] == request['worker']['reference']
        assert report['numericProfileSHA256'] == request['numericProfileSHA256']
        result = report['rangeEvaluation']
        assert result['programHasTimeInput'] is False
        assert result['usedPrecisionBits'] == 256 and result['requestSequence'] == 1
        assert 0 < result['completedWorkUnits'] <= request['options']['maximumWorkUnits']
        with decimal.localcontext() as ctx:
            ctx.prec = 180
            a, b = D(lo)+D(lo_low), D(hi)+D(hi_low)
            square_min = min(a*a, b*b) if not a <= 0 <= b else decimal.Decimal(0)
            square_max = max(a*a, b*b)
            bounds = [('f', 1+square_min, 1+square_max), ('q', D(1e-9)*a, D(1e-9)*b),
                      ('g', 2*a, 2*b), ('c', D(1e-9), D(1e-9))]
            for field, expected_low, expected_high in bounds:
                lower, upper = result[field+'Lower']['values'], result[field+'Upper']['values']
                assert len(lower) == len(upper) == 1
                assert D(lower[0]) <= expected_low <= expected_high <= D(upper[0]), (name, field, lower, upper, bounds)
        assert not list(staging.iterdir()), (name, list(staging.iterdir()))
        checks.append({'id': name, 'status': 'passed', 'requestSHA256': CLI.sha(request_path),
                       'reportSHA256': CLI.sha(report_path), 'programSHA256': program['programSHA256'],
                       'setupSHA256': program['setupSHA256']})
    for mutation in ('missing-bound', 'point-field-mix', 'wrong-operation', 'reversed', 'time-reversed',
                     'context-mode', 'count', 'unnormalized', 'bits', 'time-bits',
                     'mapping-budget', 'work-budget', 'report-budget'):
        request = copy.deepcopy(original)
        if mutation == 'missing-bound':del request['upperContext']
        elif mutation == 'point-field-mix':request['candidateHigh'] = CLI.vector([1.])
        elif mutation == 'wrong-operation':request['operation'] = 'evaluate'
        elif mutation == 'reversed':request['lowerHigh'] = CLI.vector([3.])
        elif mutation == 'time-reversed':request['lowerContext'] = context(1.)
        elif mutation == 'context-mode':
            request['upperContext'] = context(0.);request['upperContext']['mode'] = 'transientInitialization'
        elif mutation == 'count':request['upperHigh'] = CLI.vector([])
        elif mutation == 'unnormalized':request['lowerLow'] = CLI.vector([.5])
        elif mutation == 'bits':request['lowerLow']['bitPatterns'][0] ^= 1
        elif mutation == 'time-bits':request['lowerContext']['timeLowBits'] ^= 1
        elif mutation == 'mapping-budget':request['options']['maximumMappingBytes'] = 1
        elif mutation == 'work-budget':request['options']['maximumWorkUnits'] = 1
        elif mutation == 'report-budget':request['maximumReportBytes'] = 1
        request_path, report_path = output/(mutation+'-request.json'), output/(mutation+'-report.json')
        CLI.save(request_path, request)
        command, report = CLI.run(arguments.executable, request_path, report_path);commands.append(command)
        assert command['exitCode'] != 0 and report['succeeded'] is False and report.get('failure'), (mutation, command)
        assert all(report.get(field) is None for field in ('program', 'prepared', 'evaluation', 'rangeEvaluation'))
        assert not list(staging.iterdir()), (mutation, list(staging.iterdir()))
        checks.append({'id': mutation, 'status': 'refused', 'requestSHA256': CLI.sha(request_path),
                       'reportSHA256': CLI.sha(report_path)})
    CLI.save(output/'receipt.json', {'schema': 'osdi.range-cli-verification.v1', 'status': 'passed',
        'executableSHA256': CLI.sha(arguments.executable), 'modelSHA256': CLI.sha(arguments.model),
        'workerSHA256': CLI.sha(arguments.worker), 'semanticProfileSHA256': CLI.sha(arguments.semantic_profile),
        'numericProfileSHA256': sha(numeric_bytes), 'toolSHA256': CLI.sha(__file__), 'checks': checks, 'commands': commands})
    print(json.dumps({'status': 'passed', 'rangeCLIPositiveCases': 3, 'rangeCLIRefusals': 13,
                      'receipt': str(output/'receipt.json')}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--emit-fixtures', type=Path)
    parser.add_argument('--worker', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--executable', type=Path)
    parser.add_argument('--model', type=Path)
    parser.add_argument('--semantic-profile', type=Path)
    parser.add_argument('--numeric-profile', type=Path)
    parser.add_argument('--cli-output', type=Path)
    parser.add_argument('--proof-only', action='store_true', help='Run only the independent R2.1 structural proof cases.')
    parser.add_argument('--proof-cases', help='Comma-separated proof case IDs; zero,positive is the minimal red.')
    parser.add_argument('--baseline-worker', type=Path, help='Frozen worker for exact v1 output/work comparison.')
    arguments = parser.parse_args()
    if arguments.emit_fixtures:
        emit_fixtures(arguments.emit_fixtures.resolve())
        print(json.dumps({'fixtures': str(arguments.emit_fixtures.resolve()/'manifest.json')}))
    if arguments.worker and (arguments.output is not None or arguments.executable is None):
        assert arguments.output is not None
        assert not arguments.proof_cases or arguments.proof_only
        checks = (verify_proofs(arguments.worker.resolve(strict=True),
                               arguments.proof_cases.split(',') if arguments.proof_cases else None,
                               arguments.baseline_worker.resolve(strict=True) if arguments.baseline_worker else None)
                  if arguments.proof_only else verify(arguments.worker.resolve(strict=True)))
        passed = all(check.get('passed', True) for check in checks)
        receipt = {'schema': 'osdi.range-verification.v1', 'workerSHA256': sha(arguments.worker.read_bytes()),
                   'toolSHA256': sha(Path(__file__).read_bytes()), 'checks': checks, 'status': 'passed' if passed else 'failed'}
        receipt['sanitizerControls'] = {key: os.environ[key] for key in ('ASAN_OPTIONS', 'UBSAN_OPTIONS') if key in os.environ}
        if arguments.baseline_worker:receipt['baselineWorkerSHA256'] = sha(arguments.baseline_worker.read_bytes())
        with arguments.output.open('x') as stream:json.dump(receipt, stream, indent=2);stream.write('\n')
        print(json.dumps({'status': receipt['status'], 'cases': len(checks), 'receipt': str(arguments.output)}))
        if not passed:raise SystemExit(1)

    if arguments.executable:
        verify_cli_range(arguments)
