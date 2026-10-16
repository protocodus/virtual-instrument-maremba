#!/usr/bin/env python3
"""Generate mounted-tine shapes from the frequency/loss model's Hermite FEM.

Development-only numpy/scipy; no eigensolver enters the realtime device. The
four bounded beam basis functions compress each FEM playing-span eigenvector.
Frequencies and bridge losses are NOT refitted or rewritten by this script.
"""
import argparse
from pathlib import Path
import re
import numpy as np
from scipy.sparse import csr_matrix
from scipy.sparse.linalg import eigsh

from generate_kalimba_mount import (GRID, REFERENCE_A, REFERENCE_HZ, REAR_B,
                                   KAPPA_AT_REFERENCE, mount)

ROOT = Path(__file__).resolve().parents[1]


def fem(frequency, n_free):
    a = REFERENCE_A * np.sqrt(REFERENCE_HZ / frequency)
    rear = REAR_B / a
    kappa = KAPPA_AT_REFERENCE * (a / REFERENCE_A)**3
    n_rear = max(2, round(rear * n_free))
    n = n_rear + n_free
    K, M = np.zeros((2*n+2, 2*n+2)), np.zeros((2*n+2, 2*n+2))
    for element in range(n):
        h = rear / n_rear if element < n_rear else 1 / n_free
        k = np.array([[12, 6*h, -12, 6*h], [6*h, 4*h*h, -6*h, 2*h*h],
                      [-12, -6*h, 12, -6*h], [6*h, 2*h*h, -6*h, 4*h*h]]) / h**3
        mass = h/420*np.array([[156,22*h,54,-13*h], [22*h,4*h*h,13*h,-3*h*h],
                              [54,13*h,156,-22*h], [-13*h,-3*h*h,-22*h,4*h*h]])
        dofs = slice(2*element, 2*element+4)
        K[dofs, dofs] += k
        M[dofs, dofs] += mass
    bridge = 2*n_rear
    K[bridge, bridge] += kappa
    keep = np.arange(1, 2*n+2)  # pressure bar pins w but allows rotation
    values, vectors = eigsh(csr_matrix(K[np.ix_(keep, keep)]), k=8,
                            M=csr_matrix(M[np.ix_(keep, keep)]), sigma=0,
                            which='LM', v0=np.ones(len(keep)), tol=1.e-12)
    order = np.argsort(values)
    values, vectors = values[order], vectors[:, order]
    full = np.zeros((2*n+2, 8))
    full[keep, :] = vectors
    for mode in range(8):
        full[:, mode] *= (-1 if mode % 2 else 1) * np.sign(full[-2, mode])
    assert np.max(np.abs(full.T @ M @ full - np.eye(8))) < 1.e-10
    residual = K @ full - (M @ full) * values
    relative = np.linalg.norm(residual[keep], axis=0) / np.maximum(
        np.linalg.norm((M @ full)[keep] * values, axis=0), 1)
    assert max(relative) < 1.e-4, f'FEM residual {max(relative)}'
    return values**.25, full, n_rear, rear, kappa


def basis(beta, x, derivative=0):
    x = np.asarray(x)
    z = beta*x
    phase = derivative*np.pi/2
    return beta**derivative * np.stack((np.exp(beta*(x-1)),
        (-1)**derivative*np.exp(-z), np.cos(z+phase), np.sin(z+phase)), axis=-1)


def average(beta, coefficients, position, width, derivative=0):
    lo, hi = max(0, position-width/2), min(1, position+width/2)
    if hi-lo < 1.e-10:
        return basis(beta, (hi+lo)/2, derivative) @ coefficients
    # Analytic antiderivative via basis derivative=-1, multiplied by the
    # appropriate signs for the exponential and circular basis.
    antiderivative = lambda x: basis(beta, x, derivative-1) @ coefficients
    return (antiderivative(hi)-antiderivative(lo))/(hi-lo)


def fitted(frequency, n_free):
    beta, full, n_rear, rear, kappa = fem(frequency, n_free)
    x = np.linspace(0, 1, n_free+1)
    rows = []
    fit_error = 0
    for mode in range(8):
        displacement = full[2*n_rear::2, mode]
        coefficients = np.linalg.lstsq(basis(beta[mode], x), displacement, rcond=None)[0]
        fit_error = max(fit_error, max(abs(basis(beta[mode], x) @ coefficients - displacement)))
        scale = 1/(basis(beta[mode], .825) @ coefficients) if mode == 0 else np.sqrt(1+rear)/2
        rows.append((beta[mode], scale, coefficients))
    assert fit_error < 5.e-6, f'compressed shape fit {fit_error}'
    # The exact physical spring/pin/free boundaries are independently checked
    # on fitted rear/playing analytical solutions, not on the fit objective.
    rear_x = np.linspace(0, rear, n_rear+1)
    worst_boundary = 0
    for mode, (b, _, c) in enumerate(rows):
        # Rear coordinate normalized by rear span; its wavenumber is b*rear.
        rc = np.linalg.lstsq(basis(b*rear, rear_x/rear), full[:2*n_rear+1:2,mode], rcond=None)[0]
        p = lambda x,d: basis(b,x,d) @ c
        r = lambda x,d: (basis(b*rear,x/rear,d) @ rc)/rear**d
        # Scale derivatives by powers of beta so all errors have modal units.
        checks = [r(0,0), r(0,2)/b**2, p(1,2)/b**2, p(1,3)/b**3,
                  r(rear,0)-p(0,0), (r(rear,1)-p(0,1))/b,
                  (r(rear,2)-p(0,2))/b**2,
                  (p(0,3)-r(rear,3)+kappa*p(0,0))/b**3]
        worst_boundary = max(worst_boundary, max(abs(np.array(checks))))
    assert worst_boundary < 3.e-5, f'physical boundary error {worst_boundary}'
    return rows, fit_error, worst_boundary


def table():
    data = []
    worst = dict(fit=0., boundary=0., convergence=0., curvature=0.)
    for frequency in GRID:
        rows, fit, boundary = fitted(frequency, 240)
        fine, _, _ = fitted(frequency, 480)
        ratios, _ = mount(frequency, 240)
        assert np.max(abs(np.array([r[0]**2/rows[0][0]**2 for r in rows])-ratios)) < 1.e-8
        for (b,s,c), (fb,fs,fc) in zip(rows, fine):
            x = np.linspace(0,1,1001)
            worst['convergence'] = max(worst['convergence'], max(abs(s*basis(b,x) @ c-fs*basis(fb,x) @ fc)))
            old = s*average(b,c,.06,.075,2)
            new = fs*average(fb,fc,.06,.075,2)
            worst['curvature'] = max(worst['curvature'], abs(old-new)/max(1,abs(new)))
        worst['fit'], worst['boundary'] = max(worst['fit'],fit), max(worst['boundary'],boundary)
        data.append((frequency, rows))
    assert worst['convergence'] < 2.e-5 and worst['curvature'] < 2.e-5, worst
    print('Mounted shape validation:', worst)
    return data


def render(data):
    fmt = lambda x: format(float(x), '.10g')
    lines = ['// BEGIN GENERATED MOUNT SHAPES',
             'static constexpr KalimbaMountShapePoint kKalimbaMountShapes[12] = {']
    for frequency, rows in data:
        # Same pickup fundamental gain as the legacy cantilever geometry;
        # the one positive scale applies to EVERY mode, preserving curvature.
        b0,s0,c0 = rows[0]
        reference = .9174214950230173  # independently checked in C++ against legacy pickup
        strain_scale = reference/(s0*average(b0,c0,.06,.075,2))
        lines.append('    {'+format(float(frequency),'.17g')+', '+fmt(strain_scale)+', {')
        for b,s,c in rows:
            lines.append('        {'+', '.join(map(fmt,(b,s,*c)))+'},')
        lines.append('    }},')
    lines += ['};', '// END GENERATED MOUNT SHAPES']
    return '\n'.join(lines)


def render_radiation(data):
    x = (np.polynomial.legendre.leggauss(32)[0]+1)/2
    lines = ['// BEGIN GENERATED MOUNT RADIATION',
             'static constexpr float kKalimbaMountRadiation[12][8][32] = {']
    worst = 0
    for _, rows in data:
        lines.append('    {')
        for b,s,c in rows:
            values = s*basis(b,x) @ c
            reduced = values.astype(np.float32)
            worst = max(worst, max(abs(values-reduced)))
            lines.append('        {'+', '.join(format(v,'.9g')+'f' for v in reduced)+'},')
        lines.append('    },')
    lines += ['};', '// END GENERATED MOUNT RADIATION']
    assert worst < 2.e-7, worst
    print('Radiation float32 maximum error:',worst)
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--write', action='store_true')
    args = parser.parse_args()
    data = table()
    path = ROOT/'Source/DSP/KalimbaMountShapes.h'
    source = path.read_text()
    for name, block in [('SHAPES',render(data)), ('RADIATION',render_radiation(data))]:
        pattern = re.compile(r'// BEGIN GENERATED MOUNT '+name+r'.*?// END GENERATED MOUNT '+name, re.S)
        if args.write:
            source = pattern.sub(lambda _: block, source)
        else:
            current = pattern.search(source)
            assert current and current.group(0) == block, 'Mounted '+name+' table is stale'
    if args.write: path.write_text(source)
    print('wrote' if args.write else 'table matches',path)


if __name__ == '__main__':
    main()
