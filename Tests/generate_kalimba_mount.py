#!/usr/bin/env python3
"""Generate/check the kalimba mount table: overtone ratios and bridge losses.

Development-only dependencies: numpy, scipy. No dependency enters DSP.

A tine is free at the playing end, rests on the bridge and is held down by a
pressure bar behind it. Chapman (Canadian Acoustics 34(3), 2006) measured
f2/f1 = 5.86, 5.63, 5.76, 5.61, 5.30 on five tines whose free length a runs
5.4 ... 3.7 cm while the bridge-to-bar span b stays ~1.3-1.4 cm. Those ratios
lie BELOW the ideal cantilever's 6.267, which a rigid bridge with a clamped
bar cannot produce (it gives 6.4+). A bar that pins the tine (a line contact,
free to rotate) and a bridge that yields under the tine's reaction reproduce
them, including the fall toward the treble, with ONE fitted constant: the
bridge stiffness k is the same for every tine, so its normalized value
k*a^3/EI grows with a^3. This script fits nothing at run time; it checks that
the stored constant still reproduces Chapman's table and writes the table.

Per note (a = free length at uniform thickness, a ~ 1/sqrt(f)), a cubic
Hermite beam with consistent mass is solved at two resolutions, which must
agree. Stored per mode: the frequency ratio and the bridge loss relative to
the fundamental's, Gamma_n / Gamma_1 = (w_n / w_1) * (rho_n / rho_1), where
rho is the fraction of modal energy stored in the bridge spring. A bridge
with a frequency-independent loss factor dissipates Gamma = eta * w * rho / 2.
Run with --write to replace the generated block in Source/DSP/KalimbaModes.h.
"""
import argparse
from pathlib import Path
import re
import numpy as np
from scipy.sparse import csr_matrix
from scipy.sparse.linalg import eigsh

ROOT = Path(__file__).resolve().parents[1]
MODES = 8
# Chapman's small kalimba: note, measured f1 (Hz), a (cm), b (cm), f2/f1.
CHAPMAN = [('D3', 304, 5.4, 1.4, 5.86), ('E3', 336, 5.0, 1.3, 5.63),
           ('G#3', 415, 4.6, 1.4, 5.76), ('A#3', 462, 4.2, 1.3, 5.61),
           ('D4', 581, 3.7, 1.3, 5.30)]
REFERENCE_HZ, REFERENCE_A, REAR_B = 304.0, 5.4, 1.35
KAPPA_AT_REFERENCE = 350.0     # k * a^3 / EI for the 5.4 cm tine (fitted)
# Held constant outside the region the fitted mount law is trusted over.
GRID = np.geomspace(130.8127826502993, 880.0, 12)


def solve(kappa, rear, n_free, modes=MODES):
    """Pin at x=-rear, translational spring kappa at the bridge x=0, free end x=1."""
    n_rear = max(2, round(rear * n_free))
    h, h_rear = 1.0 / n_free, rear / n_rear
    def stiffness(h):
        return np.array([[12, 6*h, -12, 6*h], [6*h, 4*h*h, -6*h, 2*h*h],
                         [-12, -6*h, 12, -6*h], [6*h, 2*h*h, -6*h, 4*h*h]]) / h**3
    def mass(h):
        return h / 420 * np.array([[156, 22*h, 54, -13*h], [22*h, 4*h*h, 13*h, -3*h*h],
                                   [54, 13*h, 156, -22*h], [-13*h, -3*h*h, -22*h, 4*h*h]])
    n = n_rear + n_free
    K, M = np.zeros((2*n+2, 2*n+2)), np.zeros((2*n+2, 2*n+2))
    for element in range(n):
        size = h_rear if element < n_rear else h
        dofs = slice(2*element, 2*element+4)
        K[dofs, dofs] += stiffness(size)
        M[dofs, dofs] += mass(size)
    bridge = 2 * n_rear
    K[bridge, bridge] += kappa
    keep = np.arange(1, 2*n+2)                  # pinned bar: displacement only
    # Shift-invert keeps the low modes accurate next to the huge top
    # eigenvalues of a finely discretized fourth-order operator.
    w2, vectors = eigsh(csr_matrix(K[np.ix_(keep, keep)]), k=modes,
                        M=csr_matrix(M[np.ix_(keep, keep)]), sigma=0, which='LM',
                        v0=np.ones(len(keep)), tol=1.e-12)
    order = np.argsort(w2)
    w2, vectors = w2[order], vectors[:, order]
    rho = kappa * vectors[bridge-1, :]**2 / w2  # eigsh returns M-normalized vectors
    return np.sqrt(w2), rho


def mount(frequency, n_free):
    a = REFERENCE_A * np.sqrt(REFERENCE_HZ / frequency)
    omega, rho = solve(KAPPA_AT_REFERENCE * (a / REFERENCE_A)**3, REAR_B / a, n_free)
    ratio = omega / omega[0]
    return ratio, ratio * rho / rho[0]


def table():
    rows = []
    for frequency in GRID:
        ratio, loss = mount(frequency, 240)
        fine_ratio, fine_loss = mount(frequency, 480)
        assert np.max(np.abs(fine_ratio / ratio - 1)) < 2e-4, 'mount ratios not converged'
        assert np.max(np.abs(fine_loss[:6] - loss[:6]) / np.maximum(loss[:6], 1)) < 2e-3, \
            'bridge losses not converged'
        assert np.all(np.diff(ratio) > 0) and np.all(loss >= 0)
        rows.append((frequency, ratio, loss))
    return rows


def check_chapman():
    errors = []
    for name, frequency, a, b, measured in CHAPMAN:
        omega, _ = solve(KAPPA_AT_REFERENCE * (a / REFERENCE_A)**3, b / a, 240)
        errors.append(omega[1] / omega[0] - measured)
        print(f'{name}: model {omega[1] / omega[0]:.3f} measured {measured:.2f}')
    rms = float(np.sqrt(np.mean(np.square(errors))))
    print(f'Chapman RMS ratio error {rms:.3f}')
    assert rms < 0.12, 'mount no longer reproduces the measured overtone ratios'


def render(rows):
    lines = ['// BEGIN GENERATED KALIMBA MOUNT',
             f'static constexpr int kKalimbaMountPoints = {len(rows)};',
             'static constexpr KalimbaMountPoint kKalimbaMount[kKalimbaMountPoints] = {']
    for frequency, ratio, loss in rows:
        lines.append('    {' + format(float(frequency), '.17g') + ',')
        lines.append('     {' + ', '.join(format(x, '.9g') for x in ratio) + '},')
        lines.append('     {' + ', '.join(format(x, '.9g') for x in loss) + '}},')
    lines += ['};', '// END GENERATED KALIMBA MOUNT']
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--write', action='store_true')
    args = parser.parse_args()
    check_chapman()
    block = render(table())
    path = ROOT / 'Source/DSP/KalimbaModes.h'
    text = path.read_text()
    pattern = re.compile(r'// BEGIN GENERATED KALIMBA MOUNT.*?// END GENERATED KALIMBA MOUNT', re.S)
    if args.write:
        path.write_text(pattern.sub(lambda _: block, text))
        print('wrote', path)
    else:
        current = pattern.search(text)
        assert current and same_table(current.group(0), block), 'Source/DSP/KalimbaModes.h mount table is stale'
        print('mount table matches')


NUMBER = re.compile(r'-?\d+\.?\d*(?:e[-+]?\d+)?')


def same_table(current, generated):
    # The shift-invert eigensolve agrees only to ~1e-7 across OpenBLAS kernels
    # (AVX-512 and Accelerate reproduce the committed digits, AVX2 and Zen
    # differ by up to 7.5e-8 relative), so the last printed digits vary with
    # the CI runner's CPU. Layout must match exactly; values to 1e-6, far
    # inside the 2e-4 discretization tolerance table() asserts.
    if NUMBER.sub('#', current) != NUMBER.sub('#', generated):
        return False
    pairs = zip(map(float, NUMBER.findall(current)), map(float, NUMBER.findall(generated)))
    return all(abs(a - b) <= 1e-6 * max(abs(a), abs(b)) for a, b in pairs)


if __name__ == '__main__':
    main()
