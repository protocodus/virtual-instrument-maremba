#!/usr/bin/env python3
"""Generate/check the fixed clamped-supported-free tine modes, independently FEM-check.

Development-only dependencies: mpmath, numpy, scipy. No dependency enters DSP.
Run with --write to replace the generated array in Source/DSP/KalimbaModes.h and the
tine-only radiation cache; without it, check their agreement. --skip-fem only
skips the independent Hermite beam discretization, never the analytical solve.
"""
import argparse
import math
from pathlib import Path
import re
import numpy as np
import mpmath as mp
from scipy.sparse import csr_matrix
from scipy.sparse.linalg import eigsh

ROOT = Path(__file__).resolve().parents[1]
mp.mp.dps = 70
SPAN = mp.mpf('.30')


def determinant(k, s):
    """Scaled slope/moment matching determinant; free playing span=1."""
    b = k * s
    return ((mp.cos(b) - 1 / mp.cosh(b)) * (mp.sin(k) - mp.tanh(k) * mp.cos(k))
            - (mp.tanh(b) * mp.cos(b) - mp.sin(b)) * (1 / mp.cosh(k) + mp.cos(k)))


def solve():
    roots = []
    grid = np.arange(.10, 32, .025)
    for lo, hi in zip(grid[:-1], grid[1:]):
        if determinant(lo, SPAN) * determinant(hi, SPAN) < 0:
            root = mp.findroot(lambda k: determinant(k, SPAN), (lo, hi))
            roots.append(root)
        if len(roots) == 8:
            break
    assert len(roots) == 8
    rows = []
    for mode, k in enumerate(roots):
        C = mp.mpf(1)
        D = -(mp.cosh(k) + mp.cos(k)) / (mp.sinh(k) + mp.sin(k))
        b = k * SPAN
        BA = -(mp.cosh(b) - mp.cos(b)) / (mp.sinh(b) - mp.sin(b))
        A = -(C * (mp.sinh(k) - mp.sin(k)) + D * (mp.cosh(k) + mp.cos(k))) / (
            mp.sinh(b) + mp.sin(b) + BA * (mp.cosh(b) - mp.cos(b)))
        B = A * BA

        def rear(x):
            return A * (mp.cosh(k*x) - mp.cos(k*x)) + B * (mp.sinh(k*x) - mp.sin(k*x))

        def playing(u):
            return C * (mp.cosh(k*u) + mp.cos(k*u)) + D * (mp.sinh(k*u) + mp.sin(k*u))

        mass = mp.quad(lambda x: rear(x)**2, [0, SPAN]) + mp.quad(lambda u: playing(u)**2, [0, 1])
        normalization = (-1 if mode % 2 else 1) / mp.sqrt(mass)
        contact_scale = (1 / (playing(mp.mpf('.175')) * normalization)
                         if mode == 0 else mp.sqrt(1 + SPAN) / 2)
        rear_coefficients = [(A+B)*mp.exp(b)/2, (A-B)/2, -A, -B]
        playing_coefficients = [(C+D)*mp.exp(k)/2, (C-D)/2, C, D]
        rows.append((k, (k/roots[0])**2, contact_scale,
                     [x*normalization for x in rear_coefficients],
                     [x*normalization for x in playing_coefficients]))
    return rows


def fem_roots(elements_per_free_span):
    """Independent cubic-Hermite stiffness and consistent-mass eigenproblem."""
    n_free = elements_per_free_span
    n_rear = round(float(SPAN)*n_free)
    n = n_free+n_rear
    h = 1/n_free
    stiffness = np.array([[12, 6*h, -12, 6*h], [6*h, 4*h*h, -6*h, 2*h*h],
                          [-12, -6*h, 12, -6*h], [6*h, 2*h*h, -6*h, 4*h*h]]) / h**3
    mass = h/420*np.array([[156,22*h,54,-13*h], [22*h,4*h*h,13*h,-3*h*h],
                           [54,13*h,156,-22*h], [-13*h,-3*h*h,-22*h,4*h*h]])
    K, M = np.zeros((2*n+2,2*n+2)), np.zeros((2*n+2,2*n+2))
    for element in range(n):
        dofs = slice(2*element,2*element+4)
        K[dofs,dofs] += stiffness
        M[dofs,dofs] += mass
    keep = np.delete(np.arange(2*n+2), [0, 1, 2*n_rear])
    # Shift-invert targets the low modes without losing them to the much larger
    # highest eigenvalues of a finely discretized fourth-order beam operator.
    eigenvalues = eigsh(csr_matrix(K[np.ix_(keep,keep)]), k=8,
                        M=csr_matrix(M[np.ix_(keep,keep)]), sigma=0, which='LM',
                        v0=np.ones(len(keep)), tol=1.e-11, return_eigenvectors=False)
    eigenvalues.sort()
    assert np.all(eigenvalues > 0)
    return eigenvalues**.25


def check_radiation_cache(rows, write):
    """Reuse the existing quadrature abscissae; never modify any bar shape."""
    path = ROOT/'Source/DSP/ModalRadiation.h'
    text = path.read_text()
    begin = text.index('static constexpr ModalRadiationNode kModalRadiationNodes[32] = {')
    end = text.index('\n};',begin)
    lines = text[begin:end].splitlines()
    count = 0
    for index, line in enumerate(lines):
        if not line.startswith('    {'):
            continue
        x = float(line.split('{',1)[1].split(',')[0])
        expected = []
        for k, _, scale, _, segment in rows:
            k, scale = float(k), float(scale)
            ep, em, c, s = map(float, segment)
            u, z = 1-x, k*(1-x)
            hyperbolic = ep*math.exp(k*(u-1))+em*math.exp(-z)
            circular = c*math.cos(z)+s*math.sin(z)
            expected.append(scale*(hyperbolic+circular))
        if write:
            lines[index+2] = '     {'+', '.join(format(value,'.17g') for value in expected)+'}},'
        else:
            actual = [float(value) for value in re.search(r'\{(.*?)\}',lines[index+2]).group(1).split(',')]
            assert len(actual) == 8
            assert max(abs(a-b) for a,b in zip(actual,expected)) < 1.e-12, 'Radiation cache disagrees with derived tine shapes'
        count += 1
    assert count == 32
    if write:
        path.write_text(text[:begin]+'\n'.join(lines)+text[end:])
    print('Radiation tine cache: all32 quadrature points agree with derived geometry.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--write', action='store_true')
    parser.add_argument('--skip-fem', action='store_true')
    args = parser.parse_args()
    rows = solve()
    fmt = lambda value: format(float(value), '.17g')
    lines = ['static constexpr KalimbaBeamMode kKalimbaBeamModes[8] = {']
    for k, ratio, scale, rear, playing in rows:
        lines += ['    {'+', '.join(map(fmt, (k,ratio,scale)))+',',
                  '     {'+', '.join(map(fmt,rear))+'},',
                  '     {'+', '.join(map(fmt,playing))+'}},']
    lines.append('};')
    table = '\n'.join(lines)
    path = ROOT/'Source/DSP/KalimbaModes.h'
    original = path.read_text()
    begin = original.index('// BEGIN GENERATED KALIMBA MODES') + len('// BEGIN GENERATED KALIMBA MODES')
    end = original.index('// END GENERATED KALIMBA MODES')
    updated = original[:begin]+'\n'+table+'\n'+original[end:]
    if args.write:
        path.write_text(updated)
    else:
        assert original == updated, 'Generated table differs; investigate before using --write'
    check_radiation_cache(rows, args.write)
    expected = np.array([float(row[0]) for row in rows])
    print('analytic beta:', ', '.join(fmt(x) for x in expected))
    print('frequency ratios:', ', '.join(fmt(row[1]) for row in rows))
    if not args.skip_fem:
        for elements in (100,200):
            roots = fem_roots(elements)
            relative = np.max(np.abs(roots/expected-1))
            print(f'FEM free elements={elements}, beta:', ', '.join(fmt(x) for x in roots))
            print(f'FEM maximum relative beta error={relative:.12g}')
            assert relative < 3.e-6


if __name__ == '__main__':
    main()
