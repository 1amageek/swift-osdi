#!/usr/bin/env python3
"""Exercise the real fixed-slab worker and compare nonlinear values with Decimal."""
import argparse
import ctypes as C
import decimal
import fcntl
import json
import mmap
import os
import pathlib
import select
import struct
import tempfile
import time

NONE = 0xffffffff

class Region(C.Structure):
    _fields_ = [('offset', C.c_uint64), ('count', C.c_uint64)]
class Context(C.Structure):
    _fields_ = [('mode', C.c_uint32), ('reserved', C.c_uint32), ('high', C.c_double), ('low', C.c_double)]
class Control(C.Structure):
    _fields_ = [('magic', C.c_uint64), ('version', C.c_uint32), ('headerBytes', C.c_uint32),
        ('mappingBytes', C.c_uint64), ('byteOrder', C.c_uint32), ('descriptor', C.c_uint32),
        ('active', C.c_uint64), ('raw', C.c_uint64), ('arena', C.c_uint64), ('maxBits', C.c_uint32), ('minBits', C.c_uint32),
        ('programSha', C.c_ubyte * 32), ('workerSha', C.c_ubyte * 32), ('regions', Region * 14),
        ('request', C.c_uint64), ('response', C.c_uint64), ('candidate', Context), ('reference', Context),
        ('bits', C.c_uint32), ('status', C.c_uint32), ('maxWork', C.c_uint64), ('work', C.c_uint64),
        ('usedBits', C.c_uint32), ('flags', C.c_uint32), ('hasBound', C.c_uint32), ('detail', C.c_uint32), ('bound', C.c_double)]
assert C.sizeof(Control) == 464

def records(fmt, rows):
    return b''.join(struct.pack('<' + fmt, *row) for row in rows)

def blob(header_bytes, sections):
    data = bytearray(header_bytes)
    descriptors = []
    for stride, payload in sections:
        while len(data) % 8:
            data.append(0)
        descriptors.append((len(data), len(payload) // stride, stride))
        data.extend(payload)
    return data, descriptors

def real(value):
    return struct.unpack('<Q', struct.pack('<d', value))[0]

def program(opcode=14, ambiguous=False, select_value=False, shifted=False):
    # exp(1e16 * (x - 1)) exposes the nonlinear effect of a sub-ULP coordinate.
    instructions = [(45, 3, 2, 0, NONE, 0), (46, 4, 3, 1, NONE, 0),
        (opcode, 5, 4, NONE, NONE, 0), (46, 6, 5, 1, NONE, 0)]
    register_types = [1] * 7
    root_value = 5
    if ambiguous:
        register_types += [1, 3]
        if shifted:
            register_types += [1, 1, 1, 1]
            instructions[2:2] = [(44, 12, 4, 11, NONE, 0)]
            instructions[3] = (opcode, 5, 12, NONE, NONE, 0)
            instructions += [(44, 10, 4, 9, NONE, 0)]
        instructions += [(opcode, 7, 10 if shifted else 4, NONE, NONE, 0), (58, 8, 5, 7, NONE, 0)]
        result_register = len(register_types)
        register_types += [1]
        instructions += [(11, result_register, 8, NONE, NONE, 0)]
        root_value = result_register
    if select_value:
        register_types += [3, 1]
        instructions += [(58, 7, 0, 0, NONE, 0), (74, 8, 7, 5, 0, 0)]
        root_value = 8
    roots = [(2, 1, root_value, 0, NONE, 0), (2, 2, root_value, 0, NONE, 0),
        (2, 3, 6, 0, 0, 0), (2, 4, 6, 0, 0, 0)]
    blocks = [(0, 0, 0, 0, 0, 0, NONE, NONE, NONE, 0), (1, 0, 0, 0, 0, 0, NONE, NONE, NONE, 0),
        (2, 0, len(instructions), 0, 0, 0, NONE, NONE, NONE, 0)]
    sections = [(8, records('II', [(t, 0) for t in register_types])),
        (16, records('IIQ', [(0, 1, real(1.0)), (1, 1, real(1e16))] + ([(9, 1, real(2**-250)), (11, 1, real(2**-251))] if shifted else []))),
        (24, records('IIIIII', [(2, 2, 10, 0, NONE, 0)])),
        (24, records('IIIIII', [(i + 1, i, i, 1, 0, 0) for i in range(3)])),
        (40, records('IIIIIIIIII', blocks)), (24, records('IIIIII', instructions)), (16, b''),
        (24, records('IIIIII', roots)), (32, b''), (16, records('IIII', [(0, 0, 1, 0)])), (1, b'x')]
    data, descriptors = blob(248, sections)
    struct.pack_into('<8sIIQIIII32s', data, 0, b'OSDIPR1\0', 1, 248, len(data), 1, 0, 1, 0, bytes(32))
    for i, item in enumerate(descriptors):
        struct.pack_into('<QII', data, 72 + 16 * i, *item)
    return bytes(data)

def phi_program():
    types = [2] * 8 + [3] + [1] * 5
    instructions = [(34, 7, 6, 0, NONE, 0), (49, 8, 7, 1, NONE, 0),
        (7, 9, 4, NONE, NONE, 0), (7, 10, 5, NONE, NONE, 0), (7, 11, 3, NONE, NONE, 0),
        (46, 12, 9, 11, NONE, 0), (44, 13, 12, 10, NONE, 0)]
    blocks = [(0, 0, 0, 0, 0, 0, NONE, NONE, NONE, 0), (1, 0, 0, 0, 0, 0, NONE, NONE, NONE, 0),
        (2, 0, 0, 0, 0, 1, NONE, 3, NONE, 0), (2, 0, 2, 0, 6, 2, 8, 3, 4, 0),
        (2, 2, 5, 0, 0, 0, NONE, NONE, NONE, 0)]
    phi = [(4, 2, 0, 0), (5, 2, 1, 0), (6, 2, 2, 0), (4, 3, 5, 0), (5, 3, 4, 0), (6, 3, 7, 0)]
    sections = [(8, records('II', [(t, 0) for t in types])),
        (16, records('IIQ', [(i, 2, value) for i, value in enumerate([1, 2, 0, 10])])), (24, b''),
        (24, records('IIIIII', [(1, 0, 0, 1, 0, 0), (2, 1, 1, 1, 0, 0), (3, 2, 2, 3, 0, 0)])),
        (40, records('IIIIIIIIII', blocks)), (24, records('IIIIII', instructions)), (16, records('IIII', phi)),
        (24, records('IIIIII', [(2, 1, 13, 0, NONE, 0)])), (32, b''), (16, records('IIII', [(0, 0, 1, 0)])), (1, b'x')]
    data, descriptors = blob(248, sections)
    struct.pack_into('<8sIIQIIII32s', data, 0, b'OSDIPR1\0', 1, 248, len(data), 1, 0, 1, 0, bytes(32))
    for i, item in enumerate(descriptors):
        struct.pack_into('<QII', data, 72 + 16 * i, *item)
    return bytes(data)

def boolean_or_program(a, b, collapse=False):
    sections = [(8, records('II', [(3, 0)] * 4 + [(1, 0)])),
        (16, records('IIQ', [(0, 3, a), (1, 3, b), (2, 3, 1)])), (24, b''),
        (24, records('IIIIII', [(i + 1, i, i, 1, 0, 0) for i in range(3)])),
        (40, records('IIIIIIIIII', [(0, 0, 0, 0, 0, 0, NONE, NONE, NONE, 0),
            (1, 0, 0, 0, 0, 0, NONE, NONE, NONE, 0), (2, 0, 2, 0, 0, 0, NONE, NONE, NONE, 0)])),
        (24, records('IIIIII', [(74, 3, 0, 2, 1, 0), (11, 4, 3, NONE, NONE, 0)])), (16, b''),
        (24, records('IIIIII', [(2, 1, 4, 0, NONE, 0)] + ([(2, 6, 3, 0, NONE, 0)] if collapse else []))), (32, b''),
        (16, records('IIII', [(0, 0, 1, 0)])), (1, b'x')]
    data, descriptors = blob(248, sections)
    struct.pack_into('<8sIIQIIII32s', data, 0, b'OSDIPR1\0', 1, 248, len(data), 1, 0, 1, 0, bytes(32))
    for i, item in enumerate(descriptors):
        struct.pack_into('<QII', data, 72 + 16 * i, *item)
    return bytes(data)

def dependency_program():
    # Two separately serialized divisions have the same exact leaf authority.
    types = [1] * 9 + [3, 1, 1]
    instructions = [(47, 4, 0, 1, NONE, 0), (47, 5, 0, 2, NONE, 0),
        (67, 6, 4, 3, NONE, 0), (67, 7, 5, 3, NONE, 0),
        (45, 8, 6, 7, NONE, 0), (54, 9, 8, 11, NONE, 0), (11, 10, 9, NONE, NONE, 0)]
    sections = [(8, records('II', [(t, 0) for t in types])),
        (16, records('IIQ', [(0, 1, real(1.0)), (1, 1, real(3.0)),
                            (2, 1, real(3.0)), (3, 1, real(.5)), (11, 1, real(0.0))])), (24, b''),
        (24, records('IIIIII', [(i + 1, i, i, 1, 0, 0) for i in range(3)])),
        (40, records('IIIIIIIIII', [(0, 0, 0, 0, 0, 0, NONE, NONE, NONE, 0),
            (1, 0, 0, 0, 0, 0, NONE, NONE, NONE, 0), (2, 0, len(instructions), 0, 0, 0, NONE, NONE, NONE, 0)])),
        (24, records('IIIIII', instructions)), (16, b''),
        (24, records('IIIIII', [(2, 1, 10, 0, NONE, 0)])), (32, b''),
        (16, records('IIII', [(0, 0, 1, 0)])), (1, b'x')]
    data, descriptors = blob(248, sections)
    struct.pack_into('<8sIIQIIII32s', data, 0, b'OSDIPR1\0', 1, 248, len(data), 1, 0, 1, 0, bytes(32))
    for i, item in enumerate(descriptors):
        struct.pack_into('<QII', data, 72 + 16 * i, *item)
    return bytes(data)

def inactive_program(enabled=False, variant='pure'):
    types = [1, 1, 3, 1, 3, 1, 1, 1, 1]
    instructions = [(47, 3, 0, 1, NONE, 0), (54, 4, 3, 1, NONE, 0),
        (14, 5, 3, NONE, NONE, 0), (3, 6, 3, NONE, NONE, 0), (74, 8, 2, 7, 1, 0)]
    blocks = [(0, 0, 0, 0, 0, 0, NONE, NONE, NONE, 0),
        (1, 0, 0, 0, 0, 0, NONE, NONE, NONE, 0),
        (2, 0, 2, 0, 0, 2, 4, 3, 4, 0),
        (2, 2, 1, 0, 0, 1, NONE, 5, NONE, 0),
        (2, 3, 1, 0, 0, 1, NONE, 5, NONE, 0),
        (2, 4, 1, 0, 2, 0, NONE, NONE, NONE, 0)]
    if variant in ['effect', 'immediate']:
        instructions.insert(2, (73, NONE, 8, NONE, NONE, 0))
        blocks[3] = (2, 2, 2, 0, 0, 1, NONE, 5, NONE, 0)
        blocks[4] = (2, 4, 1, 0, 0, 1, NONE, 5, NONE, 0)
        blocks[5] = (2, 5, 1, 0, 2, 0, NONE, NONE, NONE, 0)
        if variant == 'immediate':
            instructions[1] = (58, 4, 0, 0, NONE, 0)
    elif variant == 'definite':
        instructions[1] = (58, 4, 0, 0, NONE, 0)
    elif variant == 'loop':
        blocks[3] = (2, 2, 1, 0, 0, 1, NONE, 3, NONE, 0)
    elif variant == 'nojoin':
        blocks[3] = (2, 2, 1, 0, 0, 0, NONE, NONE, NONE, 0)
    elif variant == 'multientry':
        blocks += [(2, 5, 0, 2, 0, 1, NONE, 3, NONE, 0)]
    elif variant == 'livepredicate':
        # No common join can bypass a demanded control decision.
        blocks[3] = (2, 2, 1, 0, 0, 0, NONE, NONE, NONE, 0)
        blocks[4] = (2, 3, 1, 0, 0, 0, NONE, NONE, NONE, 0)
    sections = [(8, records('II', [(t, 0) for t in types])),
        (16, records('IIQ', [(0, 1, real(1.0)), (1, 1, real(0.0)), (2, 3, int(enabled))])), (24, b''),
        (24, records('IIIIII', [(1, 0, 0, 1, 0, 0), (2, 1, 1, 1, 0, 0), (3, 2, 2, len(blocks)-2, 0, 0)])),
        (40, records('IIIIIIIIII', blocks)), (24, records('IIIIII', instructions)),
        (16, records('IIII', [(7, 3, 5, 0), (7, 4, 6, 0)])),
        (24, records('IIIIII', [(2, 1, 8, 0, NONE, 0)])), (32, b''),
        (16, records('IIII', [(0, 0, 1, 0)])), (1, b'x')]
    data, descriptors = blob(248, sections)
    struct.pack_into('<8sIIQIIII32s', data, 0, b'OSDIPR1\0', 1, 248, len(data), 1, 0, 1, 0, bytes(32))
    for i, item in enumerate(descriptors):
        struct.pack_into('<QII', data, 72 + 16 * i, *item)
    return bytes(data)

def setup(raw=1, facts=(), mapping=None):
    mapping = list(range(raw)) if mapping is None else mapping
    active = len(set(i for i in mapping if i != NONE))
    data, descriptors = blob(96, [(16, records('IIQ', facts)), (24, b''), (4, records('I', [(i,) for i in mapping])), (1, b'')])
    struct.pack_into('<IIQQII', data, 0, 1, 96, len(data), real(300.0), raw, active)
    for i, item in enumerate(descriptors):
        struct.pack_into('<QII', data, 32 + 16 * i, *item)
    return bytes(data)

class Worker:
    def __init__(self, path, tape, arena=1 << 20, work=100000, facts=(), allocation_probe=None, mapping=None, setup_bytes=None, precision=192):
        self.file = tempfile.TemporaryFile()
        self.inRead, self.inWrite = os.pipe()
        self.outRead, self.outWrite = os.pipe()
        self.raw = struct.unpack_from('<I', tape, 32)[0]
        setup_bytes = setup(self.raw, facts, mapping) if setup_bytes is None else setup_bytes
        self.active = struct.unpack_from('<I', setup_bytes, 28)[0]
        payload = [tape, setup_bytes] + [bytes(8 * self.active)] * 4 + [bytes(8 * self.raw)] * 6 + [bytes(8 * self.raw * self.raw)] * 2
        cursor = C.sizeof(Control)
        geometry = []
        for item in payload:
            cursor = (cursor + 7) // 8 * 8
            geometry.append((cursor, len(item) if len(geometry) < 2 else len(item) // 8))
            cursor += len(item)
        self.file.truncate(cursor)
        self.map = mmap.mmap(self.file.fileno(), cursor)
        self.control = Control.from_buffer(self.map)
        c = self.control
        c.magic = 0x313056504944534f
        c.version = 1
        c.headerBytes = 464
        c.mappingBytes = cursor
        c.byteOrder = 0x01020304
        c.active = self.active
        c.raw = self.raw
        c.arena = arena
        c.minBits = 128
        c.maxBits = c.bits = precision
        c.maxWork = work
        c.response = 0xffffffffffffffff
        c.candidate.mode = c.reference.mode = 2
        for i, ((offset, count), item) in enumerate(zip(geometry, payload)):
            c.regions[i] = Region(offset, count)
            self.map[offset:offset + len(item)] = item
        # All dup sources exceed child slots so fd action ordering cannot clobber a source.
        copies = [fcntl.fcntl(fd, fcntl.F_DUPFD_CLOEXEC, 10) for fd in [self.file.fileno(), self.inRead, self.outWrite]]
        slots = [3, 4, 5]
        environment = {'PATH': '/usr/bin:/bin'}
        self.statsRead = None
        self.allocationSamples = []
        if allocation_probe:
            self.statsRead, statsWrite = os.pipe()
            copies.append(fcntl.fcntl(statsWrite, fcntl.F_DUPFD_CLOEXEC, 10))
            slots.append(6)
            environment['DYLD_INSERT_LIBRARIES'] = str(allocation_probe.resolve())
        actions = [(os.POSIX_SPAWN_DUP2, fd, slot) for fd, slot in zip(copies, slots)]
        self.pid = os.posix_spawn(str(path), [str(path)], environment, file_actions=actions)
        if allocation_probe:
            os.close(statsWrite)
        for fd in copies:
            os.close(fd)
        os.close(self.inRead)
        os.close(self.outWrite)
        self.exit = None

    def wake(self):
        ready, _, _ = select.select([self.outRead], [], [], 5)
        if not ready:
            raise AssertionError('Worker notification timed out')
        value = os.read(self.outRead, 1)
        if value and self.statsRead is not None:
            ready, _, _ = select.select([self.statsRead], [], [], 5)
            assert ready
            data = os.read(self.statsRead, 40)
            assert len(data) == 40
            self.allocationSamples.append(struct.unpack('<QQQQQ', data))
        return value

    def start(self):
        value = self.wake()
        return self.control.status if value else None

    def evaluate(self, low=2 ** -54, high=1.0):
        vectors = [[value] + [0.0] * (self.active - 1) if self.active else []
                   for value in [high, low, high, 0.0]]
        result = self.evaluate_vectors(vectors)
        return [row[0] for row in result] if isinstance(result, list) else result

    def evaluate_vectors(self, vectors):
        assert len(vectors) == 4 and all(len(v) == self.active for v in vectors)
        for index, values in enumerate(vectors, 2):
            struct.pack_into('<' + 'd' * self.active, self.map, self.control.regions[index].offset, *values)
        self.control.request += 1
        os.write(self.inWrite, b'1')
        if not self.wake():
            return None
        assert self.control.response == self.control.request
        if self.control.status:
            return self.control.status
        return [list(struct.unpack_from('<' + 'd' * self.control.regions[i].count, self.map, self.control.regions[i].offset)) for i in range(6, 14)]

    def close(self):
        os.close(self.inWrite)
        deadline = time.monotonic() + 5
        while self.exit is None:
            pid, status = os.waitpid(self.pid, os.WNOHANG)
            if pid:
                self.exit = os.waitstatus_to_exitcode(status)
                break
            if time.monotonic() > deadline:
                os.kill(self.pid, 9)
                os.waitpid(self.pid, 0)
                raise AssertionError('Worker did not terminate')
            time.sleep(0.001)
        os.close(self.outRead)
        if self.statsRead is not None:
            os.close(self.statsRead)
        del self.control
        self.map.close()
        self.file.close()
        return self.exit

def contains(parts, exact):
    d = decimal.Decimal
    center = d.from_float(parts[0]) + d.from_float(parts[1])
    radius = d.from_float(parts[2])
    assert center - radius <= exact <= center + radius, (center, radius, exact)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--worker', required=True, type=pathlib.Path)
    parser.add_argument('--actual-compound', type=pathlib.Path)
    parser.add_argument('--allocation-probe', type=pathlib.Path)
    args = parser.parse_args()
    decimal.getcontext().prec = 200
    d = decimal.Decimal
    expected = (d.from_float(1e16) * d.from_float(2 ** -54)).exp()
    checks = []
    w = Worker(args.worker, program())
    assert w.start() == 0
    result = w.evaluate()
    assert isinstance(result, list), result
    contains(result[:3], expected)
    contains(result[3:6], expected - 1)
    assert abs(expected - (1 + d.from_float(1e16) * d.from_float(2 ** -54))) > d('0.1')
    assert result[3] != 0 and result[2] < 1e-30 and result[5] < 1e-30
    for _ in range(100):
        assert isinstance(w.evaluate(), list)
    assert w.close() == 0
    checks.append('nonlinear sub-ULP full-F/DeltaQ containment and 101 persistent requests')
    for a in [0, 1]:
        for b in [0, 1]:
            w = Worker(args.worker, boolean_or_program(a, b))
            assert w.start() == 0 and w.evaluate()[0] == (a or b) and w.close() == 0
    checks.append('Boolean SELECT74 OR preserves all four collapse-guard outcomes')
    w = Worker(args.worker, boolean_or_program(1, 0, collapse=True), mapping=[NONE])
    assert w.start() == 0 and w.evaluate()[0] == 1 and w.close() == 0
    w = Worker(args.worker, boolean_or_program(1, 0, collapse=True))
    assert w.start() == 0 and w.evaluate() == 1 and w.close() == 0
    checks.append('eval-serialized preparation collapse guards are checked after the complete pipeline')
    w = Worker(args.worker, phi_program())
    assert w.start() == 0
    result = w.evaluate()
    assert isinstance(result, list) and result[0] == 21 and result[1] == result[2] == 0
    assert w.close() == 0
    checks.append('simultaneous loop phi transfers preserve swap dependencies')
    for offset, encoded in [(72 + 16 * 5, struct.pack('<Q', 0xfffffffffffffff0)), (72 + 16 * 8 + 12, struct.pack('<I', 24))]:
        bad = bytearray(program())
        bad[offset:offset + len(encoded)] = encoded
        w = Worker(args.worker, bytes(bad))
        assert w.start() == 2 and w.close() == 0
    bad = bytearray(program())
    function_offset = struct.unpack_from('<Q', bad, 72 + 16 * 3)[0]
    struct.pack_into('<I', bad, function_offset + 8, 1)
    w = Worker(args.worker, bytes(bad))
    assert w.start() == 2 and w.close() == 0
    checks.append('malformed table offset/stride and unowned CFG block admission')
    w = Worker(args.worker, program(select_value=True))
    assert w.start() == 0
    contains(w.evaluate()[:3], expected)
    assert w.close() == 0
    checks.append('SELECT74 definite condition')
    w = Worker(args.worker, program())
    assert w.start() == 0 and isinstance(w.evaluate(), list) and w.control.work > 0
    w.control.bits = 193
    assert w.evaluate() == 1 and w.control.work == 0 and w.control.usedBits == 0
    assert w.close() == 0
    checks.append('invalid subsequent request clears work/precision diagnostics')
    w = Worker(args.worker, program(ambiguous=True))
    assert w.start() == 0 and isinstance(w.evaluate(), list) and w.close() == 0
    w = Worker(args.worker, program(ambiguous=True, shifted=True))
    assert w.start() == 0 and w.evaluate() == 5 and w.close() == 0
    checks.append('proved expression identity resolves equality; overlapping unequal expression rejects publication')
    w = Worker(args.worker, dependency_program())
    assert w.start() == 0 and w.evaluate()[0] == 0 and w.close() == 0
    checks.append('BULK-shaped duplicate division/power/subtraction comparison resolves exactly')
    w = Worker(args.worker, inactive_program())
    assert w.start() == 0 and w.evaluate()[0] == 0 and w.close() == 0
    w = Worker(args.worker, inactive_program(enabled=True))
    assert w.start() == 0 and w.evaluate() == 4 and w.close() == 0
    for variant in ['effect', 'loop', 'nojoin', 'multientry', 'livepredicate']:
        w = Worker(args.worker, inactive_program(variant=variant))
        assert w.start() == 0 and w.evaluate() == 4 and w.close() == 0, variant
    checks.append('inactive pure division/control defers; demanded/effect/loop/nojoin/multientry poison refuses')
    w = Worker(args.worker, inactive_program(variant='definite'))
    assert w.start() == 0 and w.evaluate()[0] == 0 and w.close() == 0
    w = Worker(args.worker, inactive_program(variant='immediate'))
    assert w.start() == 0 and w.evaluate() == 4 and w.control.flags == 8 and w.close() == 0
    checks.append('definite phi/SELECT discard only inactive values; executed MODEL_STATUS remains immediate')
    unused = bytearray(program(ambiguous=True, shifted=True))
    roots = struct.unpack_from('<Q', unused, 72 + 16 * 7)[0]
    struct.pack_into('<I', unused, roots + 8, 5)
    struct.pack_into('<I', unused, roots + 24 + 8, 5)
    w = Worker(args.worker, bytes(unused))
    assert w.start() == 0 and isinstance(w.evaluate(), list) and w.close() == 0
    checks.append('unused indeterminate comparison does not poison independent F root')
    w = Worker(args.worker, program())
    assert w.start() == 0 and w.evaluate(low=2**-53, high=0.0) == 1 and w.close() == 0
    checks.append('direct worker rejects nonnormalized coordinate pairs')
    w = Worker(args.worker, program(opcode=25))
    assert w.start() == 3 and w.close() == 0
    checks.append('unsupported opcode admission')
    w = Worker(args.worker, program(), work=2)
    assert w.start() == 0 and w.evaluate() == 7 and w.close() == 0
    loop = bytearray(phi_program())
    blocks = struct.unpack_from('<Q', loop, 72 + 16 * 4)[0]
    struct.pack_into('<I', loop, blocks + 3 * 40 + 20, 1)
    struct.pack_into('<I', loop, blocks + 3 * 40 + 24, NONE)
    w = Worker(args.worker, bytes(loop), work=1000)
    assert w.start() == 0 and w.evaluate() == 7 and w.close() == 0
    checks.append('work exhaustion in setup and an actual nonterminating CFG loop')
    w = Worker(args.worker, program(), arena=128)
    assert w.start() is None and w.close() == 80
    checks.append('cold arena exhaustion')
    lower, upper = 128, 65536
    while upper - lower > 16:
        midpoint = (lower + upper) // 32 * 16
        w = Worker(args.worker, program(), arena=midpoint)
        ready = w.start()
        exit_code = w.close()
        if ready == 0:
            upper = midpoint
        else:
            assert exit_code == 80
            lower = midpoint
    w = Worker(args.worker, program(), arena=upper)
    assert w.start() == 0
    assert w.evaluate() is None and w.close() == 80
    checks.append('MPFR internal scratch exhaustion after successful ready')
    if args.actual_compound:
        tape = args.actual_compound.read_bytes()
        offset, count, stride = struct.unpack_from('<QII', tape, 72 + 16 * 8)
        assert stride == 32
        strings_offset, strings_count, _ = struct.unpack_from('<QII', tape, 72 + 16 * 10)
        params = {}
        for i in range(count):
            osdi, scope, typ, name_offset, name_bytes, flags, default = struct.unpack_from('<IIIIIIQ', tape, offset + i * stride)
            name = tape[strings_offset + name_offset:strings_offset + name_offset + name_bytes].decode()
            params[(name, scope)] = i
        for model, instance, scale in [(None, None, d(1)), (0.5, None, d('0.5')), (0.5, 0.25, d('0.25'))]:
            facts = []
            for scope, value in [(1, model), (2, instance)]:
                if value is not None:
                    facts.append((params[('$mfactor', scope)], 1, real(value)))
            w = Worker(args.worker, tape, facts=facts)
            assert w.start() == 0, (w.control.status, w.control.detail)
            result = w.evaluate()
            assert isinstance(result, list), (result, w.control.detail)
            x = d(1) + d.from_float(2 ** -54)
            contains(result[:3], scale * (1 + x * x))
            contains(result[3:6], scale * d.from_float(1e-9) * d.from_float(2 ** -54))
            assert result[6] == float(scale * 2 * x)
            assert result[7] == float(scale * d.from_float(1e-9))
            assert w.close() == 0
        checks.append('actual compiler program full source compound default and model/instance primitive-default precedence')
    measurement = None
    if args.allocation_probe:
        w = Worker(args.worker, program(), allocation_probe=args.allocation_probe)
        assert w.start() == 0
        started = time.monotonic_ns()
        for _ in range(1000):
            assert isinstance(w.evaluate(), list)
        elapsed = time.monotonic_ns() - started
        samples = w.allocationSamples
        assert w.close() == 0
        measurement = {'requests': 1000, 'totalNanoseconds': elapsed,
            'nanosecondsPerRequest': elapsed / 1000, 'coldHeapCounts': samples[0],
            'firstRequestHeapCounts': samples[1],
            'remainingRequestHeapCounts': [sum(row[i] for row in samples[2:]) for i in range(5)],
            'heapCounterColumns': ['malloc', 'calloc', 'realloc', 'free', 'requestedBytes'],
            'scope': 'Instrumented actual IPC loop, same audit/input/synchronization on all 1000 requests; Python request preparation, notification and result read included.'}
    print(json.dumps({'checks': checks, 'minimumReadyArenaBytes': upper, 'measurement': measurement, 'result': 'passed'}))

if __name__ == '__main__':
    main()
