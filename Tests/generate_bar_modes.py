#!/usr/bin/env python3
"""Per-key modal data of undercut wooden bars from a Timoshenko finite-element model.

Every Maremba wooden bar (concert rosewood marimba, Guatemalan hormigo marimba,
West African vene balafon) is a free-free beam with a symmetric thickness
profile. This script designs that profile per key the way a maker does - by
removing wood until the tuning targets are met - and solves the resulting beam:

* vertical bending (Timoshenko: shear deformation and rotary inertia matter for
  the short, thick treble bars), first 16 elastic modes;
* lateral (in-plane) bending, first mode;
* St-Venant torsion, first two modes.

Tuning targets follow measurements (see docs/PHYSICAL_MODEL_ROUND5_REFERENCES.md
and docs/PHYSICAL_MODEL_ROUND6_REFERENCES.md): rosewood 1:4:10 up to G#3, 1:4
up to F#5 and the fundamental alone above (Yoo/Rossing/Larkin, La Favre); the
Guatemalan hormigo bars are tuned in their fundamental only, as traditional
builders do (Moreno Vazquez 2016; overtone tuning marks the concert-style
instruments that also drop the charleo, Bermudez 2023), so their overtones fall
where a moderate central arch puts them; balafon keys are not overtone tuned
either (Godlovitch 2012, Voisin 1993): each key gets a fixed first-overtone
target in 2.2..3.7 x f1, reached with central hollowing (flattens) and end
thinning (sharpens), the two carving moves makers describe.

Every key is also stored uncarved: a bar of the same length and width thinned
evenly to the note (the Carving control's zero).

The run-time table stores anchors every three semitones: bending ratios,
modal-mass fractions, half-bar shapes of the first shaped modes, torsional and
lateral ratios, and the bar's geometry and mass. Usage:

    python3 Tests/generate_bar_modes.py            # rewrite DSP/BarModes.h
    python3 Tests/generate_bar_modes.py --check    # compare with DSP/BarModes.h
    python3 Tests/generate_bar_modes.py --bork     # validation against Bork et al. 1999
"""

import argparse
import hashlib
import math
import re
import sys
from pathlib import Path

import numpy as np
import scipy.sparse as sp
from scipy.optimize import least_squares, brentq
from scipy.linalg import eigh

PROJECT = Path(__file__).resolve().parent.parent
HEADER = PROJECT / "DSP" / "BarModes.h"

N_BEND = 16          # stored bending modes
N_SHAPED = 6         # modes whose sampled shapes are stored
SHAPE_POINTS = 21    # half bar, x = 0 .. 0.5 in steps of 0.025
ANCHOR_FIRST, ANCHOR_LAST, ANCHOR_STEP = 24, 108, 3
KAPPA = 5.0 / 6.0


def note_hz(midi):
    return 440.0 * 2.0 ** ((midi - 69.0) / 12.0)


# ---------------------------------------------------------------------------
# Materials and geometry laws
# ---------------------------------------------------------------------------

class Material:
    def __init__(self, name, density, e_long, shear_ratio, torsion_shear_ratio):
        self.name = name
        self.rho = density
        self.E = e_long
        self.G = e_long / shear_ratio           # transverse shear for bending
        self.Gt = e_long / torsion_shear_ratio  # effective torsional shear


# Honduran rosewood: density ~1.05, E ~19 GPa (specific stiffness ~18 GPa cm3/g).
# Shear moduli are calibrated on Bork et al.'s C3 bar (--bork): E/G = 8.5 puts its
# untuned bending modes V4..V7 within about 14 % of the measurements, and the
# effective torsional E/G = 5.8 reproduces its first torsional mode, T1 = 2.48 f1.
# The same ratios are assumed for the other woods (no measurements found).
ROSEWOOD = Material("rosewood", 1050.0, 19.0e9, 8.5, 5.8)
# Hormigo (Platymiscium): supplier sheet 12.3 GPa, air-dry density ~0.78.
HORMIGO = Material("hormigo", 780.0, 12.3e9, 8.5, 5.8)
# Vene (Pterocarpus erinaceus): density 0.82, specific stiffness 18.7 GPa (Bremaud).
VENE = Material("vene", 820.0, 15.3e9, 8.5, 5.8)


def uniform_f1(length, thickness, material):
    """Euler-Bernoulli free-free fundamental of a uniform bar."""
    return (4.730040744862704 ** 2 / (2.0 * math.pi * length ** 2)) \
        * math.sqrt(material.E / material.rho) * thickness / math.sqrt(12.0)


class ModelSpec:
    def __init__(self, key, material, length, width, thickness, targets, balafon=False):
        self.key = key
        self.material = material
        self.length_law = length
        self.width_law = width
        self.thickness_law = thickness
        self.targets = targets          # f(midi) -> (r2 or None, r3 or None)
        self.balafon = balafon

    def geometry(self, midi):
        f = note_hz(midi)
        h0 = self.thickness_law(f)
        length = self.length_law(f)
        # A bar can only be flattened by carving: keep the uncarved bar at
        # least 8 % above the target (short treble bars need little wood removed).
        f_limit = 1.08 * f
        if uniform_f1(length, h0, self.material) < f_limit:
            length = math.sqrt(uniform_f1(1.0, h0, self.material) / f_limit)
        return length, self.width_law(f), h0


def rosewood_targets(midi):
    if midi <= 56:          # C2..G#3: 1:4:10
        return 4.0, 10.0
    if midi <= 78:          # A3..F#5: 1:4
        return 4.0, None
    return None, None       # fundamental only


def hormigo_targets(midi):
    return None, None       # traditional: the fundamental only, at every key


def balafon_first_overtone(midi):
    """A fixed, key-specific first-overtone ratio in 2.2 .. 3.7 (hash of the key)."""
    digest = hashlib.sha256(f"balafon-key-{midi}".encode()).digest()
    u = int.from_bytes(digest[:4], "little") / 2 ** 32
    # Measured ratios cluster between ~2.4 and ~3.4 with a few outliers.
    return 2.2 + 1.5 * (0.5 + 0.5 * math.sin(math.pi * (u - 0.5)))


MODELS = {
    "rosewood": ModelSpec(
        "rosewood", ROSEWOOD,
        length=lambda f: 0.38 * (261.63 / f) ** 0.36,
        width=lambda f: 0.055 * (261.63 / f) ** 0.12,
        thickness=lambda f: 0.022 * (261.63 / f) ** 0.07,
        targets=rosewood_targets),
    "hormigo": ModelSpec(
        "hormigo", HORMIGO,
        length=lambda f: 0.40 * (261.63 / f) ** 0.38,
        width=lambda f: 0.052 * (261.63 / f) ** 0.12,
        thickness=lambda f: 0.024 * (261.63 / f) ** 0.08,
        targets=hormigo_targets),
    # Gyil keys: 58 x 7.4 cm at G#2 to 43 x 5.1 cm at D#5, all ~2.4 cm thick.
    "balafon": ModelSpec(
        "balafon", VENE,
        length=lambda f: 0.43 * (622.25 / f) ** (0.17 if f <= 622.25 else 0.5),
        width=lambda f: 0.051 * (622.25 / f) ** 0.21,
        thickness=lambda f: 0.024,
        targets=lambda midi: (balafon_first_overtone(midi), None),
        balafon=True),
}


# ---------------------------------------------------------------------------
# Thickness profile: a central arch (hollowing) and optional end thinning
# ---------------------------------------------------------------------------

UNDERCUT_FLATNESS = 4.0  # (1 - xi^4)^p: the flat-bottomed arch that tunes Bork's bar


def profile(x, length, h0, depth, arch, power, end_depth=0.0, end_span=0.18):
    """Thickness at positions x (m). depth/end_depth are fractions of h0."""
    xi = np.abs(x / length - 0.5) / (0.5 * arch)
    centre = np.where(xi < 1.0, (1.0 - np.minimum(xi, 1.0) ** UNDERCUT_FLATNESS) ** power, 0.0)
    edge = np.minimum(x, length - x) / (end_span * length)
    ends = np.where(edge < 1.0, (1.0 - np.minimum(edge, 1.0)) ** 2, 0.0)
    h = h0 * (1.0 - depth * centre - end_depth * ends)
    return np.maximum(h, 0.08 * h0)


# ---------------------------------------------------------------------------
# Finite elements (linear Timoshenko elements with reduced shear integration)
# ---------------------------------------------------------------------------

def element_pattern(elements, dofs_per_node, nodes_per_element=2):
    """Row and column indices of every element matrix entry, in (i, j) order."""
    width = dofs_per_node * nodes_per_element
    dofs = dofs_per_node * np.arange(elements)[:, None] + np.arange(width)[None, :]
    return np.repeat(dofs, width, axis=1).ravel(), np.tile(dofs, (1, width)).ravel()


def assemble_beam(length, width_of, height_of, material, elements, lateral=False):
    """Free-free Timoshenko beam. width_of/height_of give section dims at x."""
    n = elements + 1
    le = length / elements
    xm = (np.arange(elements) + 0.5) * le
    b = width_of(xm)
    h = height_of(xm)
    if lateral:
        b, h = h, b                      # bend about the other axis
    area = b * h
    inertia = b * h ** 3 / 12.0
    E, G, rho = material.E, material.G, material.rho
    bending = np.array([[0, 0, 0, 0], [0, 1, 0, -1], [0, 0, 0, 0], [0, -1, 0, 1]], float) / le
    bs = np.array([-1.0 / le, -0.5, 1.0 / le, -0.5])
    shear = le * np.outer(bs, bs)
    # Average of consistent and lumped mass: well-balanced high-mode accuracy.
    translation = np.array([[5 / 12, 0, 1 / 12, 0], [0, 0, 0, 0], [1 / 12, 0, 5 / 12, 0], [0, 0, 0, 0]])
    rotation = np.array([[0, 0, 0, 0], [0, 5 / 12, 0, 1 / 12], [0, 0, 0, 0], [0, 1 / 12, 0, 5 / 12]])
    k = (E * inertia)[:, None, None] * bending + (KAPPA * G * area)[:, None, None] * shear
    m = (rho * area * le)[:, None, None] * translation + (rho * inertia * le)[:, None, None] * rotation
    rows, cols = element_pattern(elements, 2)
    size = 2 * n
    K = sp.csc_matrix((k.ravel(), (rows, cols)), shape=(size, size))
    M = sp.csc_matrix((m.ravel(), (rows, cols)), shape=(size, size))
    return K, M


def assemble_torsion(length, width_of, height_of, material, elements):
    n = elements + 1
    le = length / elements
    xm = (np.arange(elements) + 0.5) * le
    b = np.maximum(width_of(xm), height_of(xm))
    t = np.minimum(width_of(xm), height_of(xm))
    beta = (1.0 - 0.630 * t / b + 0.052 * (t / b) ** 5) / 3.0
    J = beta * b * t ** 3
    Ip = b * t * (b * b + t * t) / 12.0
    k = (material.Gt * J / le)[:, None, None] * np.array([[1, -1], [-1, 1]], float)
    m = (material.rho * Ip * le)[:, None, None] * np.array([[5 / 12, 1 / 12], [1 / 12, 5 / 12]])
    rows, cols = element_pattern(elements, 1)
    K = sp.csc_matrix((k.ravel(), (rows, cols)), shape=(n, n))
    M = sp.csc_matrix((m.ravel(), (rows, cols)), shape=(n, n))
    return K, M


def elastic_modes(K, M, count, rigid):
    """Lowest `count` elastic modes (rigid-body modes skipped). A dense solver:
    exact to rounding and independent of call order. (ARPACK's shift-invert
    iteration about the rigid modes resolved f1 to only ~1e-7, which spoiled
    the tuning fits' finite-difference Jacobians.)"""
    values, vectors = eigh(K.toarray(), M.toarray(), subset_by_index=[0, count + rigid + 1])
    freqs = np.sqrt(np.maximum(values, 0.0)) / (2.0 * math.pi)
    keep = freqs > 1.0
    return freqs[keep][:count], vectors[:, keep][:, :count]


def elastic_frequencies(K, M, count, rigid):
    """elastic_modes without the vectors (the tuning fits need only these)."""
    values = eigh(K.toarray(), M.toarray(), subset_by_index=[0, count + rigid + 1], eigvals_only=True)
    freqs = np.sqrt(np.maximum(values, 0.0)) / (2.0 * math.pi)
    return freqs[freqs > 1.0][:count]


class Bar:
    def __init__(self, length, width, h0, material, depth, arch, power, end_depth=0.0, end_span=0.18):
        self.length, self.width, self.h0, self.material = length, width, h0, material
        self.depth, self.arch, self.power = depth, arch, power
        self.end_depth, self.end_span = end_depth, end_span

    def height(self, x):
        return profile(x, self.length, self.h0, self.depth, self.arch, self.power,
                       self.end_depth, self.end_span)

    def widths(self, x):
        return np.full_like(x, self.width)

    def bending(self, count=3, elements=160, lateral=False):
        K, M = assemble_beam(self.length, self.widths, self.height, self.material, elements, lateral)
        return elastic_modes(K, M, count, 2), M

    def frequencies(self, count=3, elements=120):
        K, M = assemble_beam(self.length, self.widths, self.height, self.material, elements)
        return elastic_frequencies(K, M, count, 2)

    def torsion(self, count=2, elements=160):
        K, M = assemble_torsion(self.length, self.widths, self.height, self.material, elements)
        return elastic_modes(K, M, count, 1)[0]

    def mass(self, samples=4001):
        x = np.linspace(0.0, self.length, samples)
        return float(np.trapz(self.material.rho * self.width * self.height(x), x))


# ---------------------------------------------------------------------------
# Tuning (the maker's carving)
# ---------------------------------------------------------------------------

def tune_bar(spec, midi, previous=None):
    target = note_hz(midi)
    length, width, h0 = spec.geometry(midi)
    r2, r3 = spec.targets(midi)
    material = spec.material

    def ratios(params, elements=120):
        depth, arch, power, end_depth = params
        return Bar(length, width, h0, material, depth, arch, power, end_depth).frequencies(3, elements)

    if spec.balafon:
        # Central hollowing flattens, end thinning sharpens. The maker cuts
        # the keys to a length progression (the gyil's 58 cm at G#2 to 43 cm
        # at D#5) and carves each to pitch: length stays within 10 % of the
        # law, the pitch is met and the first overtone comes as close to the
        # key's own target as that carving allows.
        # Thinning from the ends toward the quarter points lowers the first
        # overtone (short thick keys also sit low through shear).
        def build(p):
            scale, depth, end_depth, end_span = p
            return Bar(length * scale, width, h0, material, depth, 0.62, 1.2, end_depth, end_span)

        def residual(p):
            f = build(p).frequencies(3, 120)
            return [math.log(f[0] / target) * 20.0, math.log((f[1] / f[0]) / r2) * 4.0,
                    0.5 * math.log(p[0])]
        start = previous if previous is not None and len(previous) == 4 else (1.0, 0.3, 0.1, 0.25)
        lower, upper = (0.9, 0.0, 0.0, 0.15), (1.1, 0.86, 0.8, 0.42)
        best = None
        for guess in (start, (1.0, 0.4, 0.0, 0.2), (1.1, 0.2, 0.4, 0.35), (0.9, 0.1, 0.6, 0.4),
                      (1.1, 0.6, 0.2, 0.2), (0.9, 0.05, 0.5, 0.3)):
            guess = tuple(min(max(v, lo), hi) for v, lo, hi in zip(guess, lower, upper))
            sol = least_squares(residual, guess, bounds=(lower, upper), xtol=1e-12, ftol=1e-12)
            if best is None or sol.cost < best.cost:
                best = sol
        return build(best.x), tuple(best.x)

    if r2 is not None and r3 is not None:
        def residual(p):
            f = ratios((p[0], p[1], p[2], 0.0))
            return [math.log(f[0] / target) * 4.0, math.log((f[1] / f[0]) / r2),
                    math.log((f[2] / f[0]) / r3)]
        start = previous if previous is not None and len(previous) == 3 else (0.7, 0.52, 0.7)
        best = None
        for guess in (start, (0.7, 0.52, 0.7), (0.75, 0.6, 1.2), (0.6, 0.45, 0.4), (0.8, 0.7, 2.0)):
            sol = least_squares(residual, guess, bounds=([0.05, 0.3, 0.15], [0.92, 0.99, 6.0]),
                                xtol=1e-10, ftol=1e-10)
            if best is None or sol.cost < best.cost:
                best = sol
        depth, arch, power = best.x
        return Bar(length, width, h0, material, depth, arch, power), tuple(best.x)

    if r2 is not None:
        power = 0.9
        def residual(p):
            f = ratios((p[0], p[1], power, 0.0))
            return [math.log(f[0] / target) * 4.0, math.log((f[1] / f[0]) / r2)]
        start = previous[:2] if previous is not None else (0.6, 0.55)
        best = None
        for guess in (start, (0.6, 0.55), (0.4, 0.75), (0.75, 0.45)):
            sol = least_squares(residual, guess, bounds=([0.02, 0.3], [0.92, 0.99]), xtol=1e-10, ftol=1e-10)
            if best is None or sol.cost < best.cost:
                best = sol
        depth, arch = best.x
        return Bar(length, width, h0, material, depth, arch, power), (depth, arch, power)

    # Fundamental only: a moderate arch deep enough to reach the pitch.
    arch, power = 0.55, 0.9
    def pitch(depth):
        return ratios((depth, arch, power, 0.0))[0] - target
    depth = brentq(pitch, 0.0, 0.88, xtol=1e-9) if pitch(0.0) > 0.0 else 0.0
    return Bar(length, width, h0, material, depth, arch, power), (depth, arch, power)


# ---------------------------------------------------------------------------
# Modal data for one tuned bar
# ---------------------------------------------------------------------------

def modal_data(bar, elements=720):
    (freqs, vectors), M = bar.bending(N_BEND, elements)
    f1 = freqs[0]
    total_mass = bar.mass()
    xs = np.linspace(0.0, bar.length, elements + 1)
    masses, shapes = [], []
    for m in range(N_BEND):
        w = vectors[0::2, m]
        vec = vectors[:, m]
        scale = np.max(np.abs(w))
        if w[0] < 0:
            scale = -scale
        vec = vec / scale
        w = w / scale
        masses.append(float(vec @ (M @ vec)) / total_mass)
        if m < N_SHAPED:
            half = np.linspace(0.0, 0.5 * bar.length, SHAPE_POINTS)
            shapes.append(np.interp(half, xs, w))
    (lat, _), _ = bar.bending(1, 480, lateral=True)
    tor = bar.torsion(2, 480)
    return {
        "f1": f1,
        "ratio": freqs / f1,
        "mass_fraction": np.array(masses),
        "shapes": np.array(shapes),
        "torsion": tor / f1,
        "lateral": lat[0] / f1,
        "length": bar.length, "width": bar.width, "thickness": bar.h0,
        "mass": total_mass,
        "centre": float(bar.height(np.array([0.5 * bar.length]))[0]),
    }


def uncarved_bar(spec, midi, carved):
    """The same key tuned without an undercut: a bar of the carved key's length
    and width, thinned evenly until it sounds the note (its overtones fall
    where a uniform bar's do, about 2.76 and 5.40 x f1)."""
    target = note_hz(midi)

    def pitch(thickness):
        return Bar(carved.length, carved.width, thickness, spec.material, 0.0, 0.5, 1.0).frequencies(1, 120)[0] - target
    return Bar(carved.length, carved.width, brentq(pitch, 0.05 * carved.h0, 1.5 * carved.h0, xtol=1e-12),
               spec.material, 0.0, 0.5, 1.0)


def generate_model(spec, verbose=False):
    """Carved keys and their uncarved counterparts, anchor by anchor."""
    anchors, blanks = [], []
    previous = None
    for midi in range(ANCHOR_FIRST, ANCHOR_LAST + 1, ANCHOR_STEP):
        bar, previous = tune_bar(spec, midi, previous)
        data = modal_data(bar)
        data["midi"] = midi
        anchors.append(data)
        blank = modal_data(uncarved_bar(spec, midi, bar))
        blank["midi"] = midi
        blanks.append(blank)
        if verbose:
            r = data["ratio"]
            print(f"{spec.key:8s} {midi:3d} f1 {data['f1']:8.2f} (target {note_hz(midi):8.2f}) "
                  f"L {data['length']*100:5.1f} cm h {data['thickness']*1000:4.1f}->{data['centre']*1000:4.1f} mm "
                  f"r {r[1]:.3f} {r[2]:.3f} {r[3]:.2f} {r[4]:.2f} {r[5]:.2f} "
                  f"T {data['torsion'][0]:.2f} {data['torsion'][1]:.2f} L1 {data['lateral']:.2f} "
                  f"m {data['mass']*1000:6.1f} g", flush=True)
    return anchors, blanks


# ---------------------------------------------------------------------------
# Validation: Bork et al. 1999 C3 rosewood bar (46 x 6 x 2.35 cm, f1 130 Hz)
# ---------------------------------------------------------------------------

def bork_validation():
    measured = {"f1": 130.0, "V2": 4.0, "V3": 10.7, "V4": 19.4, "V5": 24.6, "V6": 38.7, "V7": 48.2,
                "T1": 2.48, "T2": 11.1, "L1": 7.0}
    length, width, h0 = 0.46, 0.06, 0.0235

    def residual(p):
        f = Bar(length, width, h0, ROSEWOOD, p[0], p[1], p[2]).frequencies(3, 160)
        return [math.log(f[0] / 130.0) * 4, math.log(f[1] / f[0] / 4.0), math.log(f[2] / f[0] / 10.7)]
    sol = least_squares(residual, (0.7, 0.52, 0.7), bounds=([0.05, 0.3, 0.15], [0.92, 0.99, 6.0]))
    bar = Bar(length, width, h0, ROSEWOOD, *sol.x)
    data = modal_data(bar)
    r = data["ratio"]
    model = {"f1": data["f1"], "V2": r[1], "V3": r[2], "V4": r[3], "V5": r[4], "V6": r[5], "V7": r[6],
             "T1": data["torsion"][0], "T2": data["torsion"][1], "L1": data["lateral"]}
    print(f"Bork C3 bar, carving depth {sol.x[0]:.3f} h0, arch {sol.x[1]:.3f} L, power {sol.x[2]:.2f}")
    worst = 0.0
    for key, value in measured.items():
        err = model[key] / value - 1.0
        if key not in ("f1", "V2", "V3"):
            worst = max(worst, abs(err))
        print(f"  {key:3s} measured {value:7.2f}  model {model[key]:7.2f}  ({err * 100:+5.1f} %)")
    print(f"  worst untuned deviation {worst * 100:.1f} %")
    return worst


# ---------------------------------------------------------------------------
# C++ table
# ---------------------------------------------------------------------------

def fmt(value):
    return f"{value:.7g}"


def render_header(models):
    lines = [
        "#pragma once",
        "",
        "// GENERATED by Tests/generate_bar_modes.py -- do not edit by hand.",
        "// Per-key modal data of the carved wooden bars (Timoshenko finite elements).",
        "// Anchors every three semitones; the voice interpolates in log frequency.",
        "// Shapes: first kBarShapedModes bending modes on the half bar x = 0..0.5",
        "// (normalized to max |phi| = 1, positive at the end; even modes symmetric,",
        "// odd modes antisymmetric). Mass fractions use that normalization.",
        "",
        "namespace maremba {",
        "",
        f"static constexpr int kBarModes = {N_BEND};",
        f"static constexpr int kBarShapedModes = {N_SHAPED};",
        f"static constexpr int kBarShapePoints = {SHAPE_POINTS};",
        f"static constexpr int kBarAnchors = {len(next(iter(models.values())))};",
        "",
        "struct BarModeAnchor {",
        "    double midi;",
        "    double lengthM, widthM, thicknessM, massKg;",
        "    double ratio[kBarModes];",
        "    double massFraction[kBarModes];",
        "    double torsion[2];",
        "    double lateral;",
        "    float shape[kBarShapedModes][kBarShapePoints];",
        "};",
        "",
    ]
    names = {"rosewood": "kRosewoodBars", "hormigo": "kHormigoBars", "balafon": "kBalafonBars",
             "rosewood-uncarved": "kRosewoodBlanks", "hormigo-uncarved": "kHormigoBlanks",
             "balafon-uncarved": "kBalafonBlanks"}
    for key, anchors in models.items():
        if key.endswith("-uncarved") and not any(k.endswith("-uncarved") for k in list(models)[:list(models).index(key)]):
            lines.append("// The same keys tuned without an undercut: bars of the carved keys' length and")
            lines.append("// width thinned evenly to the note (the Carving control's zero).")
        name = names[key]
        lines.append(f"static constexpr BarModeAnchor {name}[kBarAnchors] = {{")
        for a in anchors:
            ratio = ", ".join(fmt(v) for v in a["ratio"])
            mass = ", ".join(fmt(v) for v in a["mass_fraction"])
            shapes = ",\n       ".join("{" + ", ".join(f"{v:.5f}f" for v in s) + "}" for s in a["shapes"])
            lines.append(f"    {{{a['midi']}, {fmt(a['length'])}, {fmt(a['width'])}, {fmt(a['thickness'])}, "
                         f"{fmt(a['mass'])},")
            lines.append(f"     {{{ratio}}},")
            lines.append(f"     {{{mass}}},")
            lines.append(f"     {{{fmt(a['torsion'][0])}, {fmt(a['torsion'][1])}}}, {fmt(a['lateral'])},")
            lines.append(f"     {{{shapes}}}}},")
        lines.append("};")
        lines.append("")
    lines.append("} // namespace maremba")
    lines.append("")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="fail if DSP/BarModes.h differs from a fresh run")
    parser.add_argument("--bork", action="store_true", help="validate against Bork et al.'s C3 bar and exit")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()
    if args.bork:
        worst = bork_validation()
        sys.exit(0 if worst < 0.15 else 1)
    generated = {key: generate_model(spec, args.verbose) for key, spec in MODELS.items()}
    models = {key: pair[0] for key, pair in generated.items()}
    models.update({key + "-uncarved": pair[1] for key, pair in generated.items()})
    text = render_header(models)
    if args.check:
        current = HEADER.read_text() if HEADER.exists() else ""
        if current != text:
            print("DSP/BarModes.h is stale: run Tests/generate_bar_modes.py", file=sys.stderr)
            sys.exit(1)
        print("PASS: DSP/BarModes.h matches the generator")
        return
    HEADER.write_text(text)
    print(f"wrote {HEADER.relative_to(PROJECT)} ({len(text) // 1024} KB)")


if __name__ == "__main__":
    main()
