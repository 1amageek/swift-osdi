#!/usr/bin/env python3
"""Emit explicit source-program fixtures for the internal chart checker."""
import argparse
from pathlib import Path
import struct
from verify_worker import blob, records, real, setup, phi_program, inactive_program, NONE


def program(variant):
    types, constants, inputs, instructions = [], [], [], []
    def register(kind=1):
        index = len(types); types.append(kind); return index
    def constant(value):
        index = register(); constants.append((index, 1, real(value))); return index
    def operation(opcode, a, b=NONE, aux=NONE, kind=1):
        index = register(kind); instructions.append((opcode, index, a, b, aux, 0)); return index
    zero, one, two, three, four, half, delta = [constant(x) for x in (0., 1., 2., 3., 4., .5, .125)]
    x = register(); inputs.append((2, x, 10, 0, NONE, 0))
    square = operation(46, x, x)
    cube = operation(46, square, x)
    base = operation(44, x, cube)
    derivative = operation(44, one, operation(46, three, square))
    if variant in ('nonlinear', 'jump'):
        predicate = operation(54, x, half, kind=3)
        if variant == 'nonlinear':
            offset = operation(45, x, half)
            offset_square = operation(46, offset, offset)
            extra = operation(46, offset_square, offset)
            f = operation(44, operation(46, four, base), operation(74, predicate, extra, zero))
            q = base
            g = operation(44, operation(46, four, derivative),
                          operation(74, predicate, operation(46, three, offset_square), zero))
        else:
            f = operation(74, predicate, two, one)
            q = operation(44, base, operation(74, predicate, delta, zero))
            g = zero
        c = derivative
    elif variant == 'sqrt':
        f = operation(13, x); q = x; g = operation(47, half, f); c = one
    elif variant == 'removable':
        f = operation(47, x, x); q = x; g = zero; c = one
    else:
        raise ValueError(variant)
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
                (16, records('IIII', [(0, 0, 1, 0)])), (1, b'x')]
    data, descriptors = blob(248, sections)
    struct.pack_into('<8sIIQIIII32s', data, 0, b'OSDIPR1\0', 1, 248, len(data), 1, 0, 1, 0, bytes(32))
    for index, section in enumerate(descriptors):
        struct.pack_into('<QII', data, 72 + 16*index, *section)
    return bytes(data)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    programs = {name: program(name) for name in ('nonlinear', 'jump', 'sqrt', 'removable')}
    programs.update(loop=phi_program(), inactive=inactive_program(False), live_invalid=inactive_program(True))
    for name, data in programs.items():
        (args.output / (name + '.program')).write_bytes(data)
        (args.output / (name + '.setup')).write_bytes(setup())
    # Independent compiler input for optional same-formula producer verification.
    (args.output / 'nonlinear.va').write_text('''`include "disciplines.vams"
module guarded_nonlinear(inout x);
electrical x;
real q, i, z;
analog begin
    q = V(x) + V(x)*V(x)*V(x);
    z = V(x)-0.5;
    if (V(x)>0.5) i = 4.0*q + z*z*z;
    else i = 4.0*q;
    I(x) <+ i + ddt(q);
end
endmodule
''')


if __name__ == '__main__':
    main()
