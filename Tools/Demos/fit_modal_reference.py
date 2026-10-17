#!/usr/bin/env python3
"""Fit per-partial modal parameters to single-strike recordings.

For each WAV file (16/24/32-bit PCM or 32/64-bit float, any channel count,
averaged to mono) this measures the strongest partials of one strike:
frequency, damping (T60 = 6.91/sigma), loss factor eta = 2.2/(T60*f), the
complex amplitude extrapolated to the onset, the ratio to the fundamental and
the level re the fundamental 20 ms and 100 ms after the onset; plus the
fundamental's bloom (heterodyne envelope peak), an attack centroid and a noise
floor. See Docs/research/calibration.md for recording practice and parameter mapping.

  python3 Tools/Demos/fit_modal_reference.py Tools/Demos/Reference/MusyngKite_C4.wav --markdown
  python3 Tools/Demos/fit_modal_reference.py take.wav --note C4 --json take.json
  python3 Tools/Demos/fit_modal_reference.py ref.wav --compare model.wav --markdown
  python3 Tools/Demos/fit_modal_reference.py ref.json --compare-json model.json
  python3 Tools/Demos/fit_modal_reference.py --self-test

Method. The strike is found by its energy rise. The post-onset signal is
transformed once; each analysis window (default 20-120 ms, 50-400 ms and
200 ms to the end) splits it into flat-top bands with Gaussian skirts, sized so
that every band kernel stays inside the window, and decimates each band in the
frequency domain. ESPRIT (shift invariance of a Hankel covariance) estimates
damped complex exponentials per band; the exact band gain at each complex pole
is divided out. Poles need a matched-filter SNR of 15 dB, positive damping, a
bandwidth under 30 % of their frequency, at most 40 dB of implied decay between
the onset and their window, and a level within 80 dB of the strongest partial.
Estimates are merged across bands and windows per partial; the reported values
come from the window with the smallest Cramer-Rao bound on the damping, so fast
high partials come from the early window and long low partials from the late
one. Weaker poles within half an ERB (at most 40 Hz) of a partial 6 dB stronger
are its sub-poles: they shape its envelope (bloom, beating), which gives the
20/100 ms levels. Requires numpy and scipy only.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import re
import struct
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path

# Hundreds of small eigenproblems run several times faster on one BLAS thread
# than with the thread pool spinning up for each; an explicit setting wins.
for _variable in ("OPENBLAS_NUM_THREADS", "OMP_NUM_THREADS", "MKL_NUM_THREADS", "VECLIB_MAXIMUM_THREADS"):
    os.environ.setdefault(_variable, "1")

import numpy as np  # noqa: E402  (after the thread settings)
from scipy import fft as sfft  # noqa: E402
from scipy import special  # noqa: E402

TOOL = "fit_modal_reference.py"
VERSION = "1.0.0"
LN1000 = 3.0 * math.log(10.0)       # T60 = LN1000 / sigma = 6.91 / sigma
DEFAULT_WINDOWS = ((0.020, 0.120), (0.050, 0.400), (0.200, None))
SNR_MIN_DB = 15.0                   # matched-filter SNR a pole needs to count as signal
LEVEL_FLOOR_DB = -80.0              # partials further below the strongest are rejected
MAX_BANDWIDTH = 0.30                # -3 dB bandwidth limit, fraction of the frequency
MAX_EXTRAPOLATION_DB = 40.0         # decay allowed between the onset and a pole's window
SATELLITE_DB = 6.0                  # a weaker pole within half an ERB is part of a partial
GUARD_S = 0.003                     # contact transient kept outside every band kernel
KERNEL_SIGMAS = 4.5                 # band kernel half-support in time-domain sigmas
MAX_KERNEL_S = 0.012                # longest kernel sigma (sharpest band skirts)
F_LOW = 25.0                        # lowest analysed frequency
F0_SEARCH_CENTS = 60.0              # fundamental must lie this close to the nominal note
NOTE_OFFSETS = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}
NOTE_NAMES = ("C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B")
NOTE_TOKEN = re.compile(r"^([A-Ga-g])(#|b|s|♯|♭)?(-?\d)$")
ENVELOPE_TIMES_MS = (2, 5, 10, 20, 30, 50, 75, 100, 150, 200, 300, 500)


# ─────────────────────────────── WAV input/output ───────────────────────────────

def read_wav(path):
    """Return (samples float64 [frames, channels] in -1..1, sample rate, format text)."""
    data = Path(path).read_bytes()
    if len(data) < 12 or data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError(f"{path}: not a RIFF/WAVE file")
    pos, fmt, payload = 12, None, None
    while pos + 8 <= len(data):
        chunk, size = data[pos:pos + 4], struct.unpack_from("<I", data, pos + 4)[0]
        body = data[pos + 8:pos + 8 + size]
        if chunk == b"fmt ":
            fmt = body
        elif chunk == b"data":
            payload = body          # a streamed file may declare more than it holds
        pos += 8 + size + (size & 1)
    if fmt is None or payload is None or len(fmt) < 16:
        raise ValueError(f"{path}: missing fmt or data chunk")
    tag, channels, rate, _, align, bits = struct.unpack_from("<HHIIHH", fmt)
    if tag == 0xFFFE and len(fmt) >= 26:            # WAVE_FORMAT_EXTENSIBLE
        tag = struct.unpack_from("<H", fmt, 24)[0]
    if channels < 1 or align < channels or rate < 1:
        raise ValueError(f"{path}: invalid format header")
    width = align // channels
    frames = len(payload) // align
    raw = payload[:frames * align]
    if tag == 1 and width == 1:
        x = (np.frombuffer(raw, np.uint8).astype(np.float64) - 128.0) / 128.0
    elif tag == 1 and width == 2:
        x = np.frombuffer(raw, "<i2") / 32768.0
    elif tag == 1 and width == 3:
        b = np.frombuffer(raw, np.uint8).reshape(-1, 3).astype(np.int32)
        v = b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)
        x = np.where(v >= 1 << 23, v - (1 << 24), v) / float(1 << 23)
    elif tag == 1 and width == 4:                   # 32-bit, or 24-in-32 left-justified
        x = np.frombuffer(raw, "<i4") / 2147483648.0
    elif tag == 3 and width in (4, 8):
        x = np.frombuffer(raw, "<f4" if width == 4 else "<f8").astype(np.float64)
    else:
        raise ValueError(f"{path}: unsupported WAV encoding (format {tag}, {bits}-bit)")
    kind = "float" if tag == 3 else "PCM"
    return x.reshape(-1, channels).astype(np.float64), int(rate), f"{kind} {bits}-bit"


def write_wav(path, samples, rate, sample_format="pcm16", dither_seed=None):
    """Write pcm16 / pcm24 (optionally TPDF-dithered) or float32 WAV."""
    x = np.asarray(samples, dtype=np.float64)
    if x.ndim == 1:
        x = x[:, None]
    frames, channels = x.shape
    if sample_format == "float32":
        tag, bits, payload = 3, 32, x.astype("<f4").tobytes()
    elif sample_format in ("pcm16", "pcm24"):
        tag, bits = 1, 16 if sample_format == "pcm16" else 24
        scale = float(1 << (bits - 1))
        y = x * scale
        if dither_seed is not None:
            rng = np.random.default_rng(dither_seed)
            y = y + rng.random(y.shape) - rng.random(y.shape)
        q = np.clip(np.round(y), -scale, scale - 1).astype("<i4")
        payload = (q.astype("<i2").tobytes() if bits == 16
                   else q.reshape(-1, 1).view(np.uint8)[:, :3].tobytes())
    else:
        raise ValueError(f"unknown sample format {sample_format}")
    width = bits // 8
    fmt = struct.pack("<HHIIHH", tag, channels, rate, rate * channels * width, channels * width, bits)
    chunks = b"fmt " + struct.pack("<I", len(fmt)) + fmt
    if tag == 3:  # non-PCM: cbSize and a fact chunk
        fmt += struct.pack("<H", 0)
        chunks = (b"fmt " + struct.pack("<I", len(fmt)) + fmt
                  + b"fact" + struct.pack("<II", 4, frames))
    chunks += b"data" + struct.pack("<I", len(payload)) + payload + (b"\0" if len(payload) & 1 else b"")
    Path(path).write_bytes(b"RIFF" + struct.pack("<I", 4 + len(chunks)) + b"WAVE" + chunks)


# ─────────────────────────────────── notes ───────────────────────────────────

def midi_to_hz(midi):
    return 440.0 * 2.0 ** ((midi - 69.0) / 12.0)


def midi_name(midi):
    return f"{NOTE_NAMES[midi % 12]}{midi // 12 - 1}"


def note_token_to_midi(token):
    """'C4' -> 60 (scientific pitch, C4 = MIDI 60); '#', 's' or 'b' accidentals."""
    match = NOTE_TOKEN.match(token)
    if not match:
        return None
    letter, accidental, octave = match.groups()
    shift = {None: 0, "#": 1, "s": 1, "♯": 1, "b": -1, "♭": -1}[accidental]
    return NOTE_OFFSETS[letter.upper()] + shift + 12 * (int(octave) + 1)


def resolve_note(text=None, path=None):
    """Nominal note from --note (name, MIDI number or '261.6Hz') or the file name."""
    source = "--note"
    if text is None and path is not None:
        tokens = [t for t in re.split(r"[_\-\s.]+", Path(path).stem) if t]
        for token in reversed(tokens):
            if note_token_to_midi(token) is not None:
                text, source = token, "file name"
                break
        if text is None:
            raise ValueError(f"{path}: no note in the file name (e.g. *_C4.wav); pass --note")
    text = str(text).strip()
    hz = re.fullmatch(r"(\d+(?:\.\d+)?)\s*[Hh][Zz]", text)
    if hz:
        freq = float(hz.group(1))
        midi = int(round(69 + 12 * math.log2(freq / 440.0)))
        return {"name": midi_name(midi), "midi": midi, "nominal_hz": freq, "source": source}
    if re.fullmatch(r"\d{1,3}", text):
        midi = int(text)
    else:
        midi = note_token_to_midi(text)
    if midi is None or not 0 <= midi <= 127:
        raise ValueError(f"cannot read note {text!r} (use C4, F#3, Bb2, Cs4, 60 or 261.6Hz)")
    return {"name": midi_name(midi), "midi": midi, "nominal_hz": midi_to_hz(midi), "source": source}


# ───────────────────────── onset, noise, attack, envelope ─────────────────────────

def detect_onset(x, fs, threshold_db=-30.0):
    """Sample index where the strike starts: the energy rise leading to the attack.

    The first centred 2 ms power frame within `threshold_db` of the maximum marks
    the attack; the onset is the first sample from 20 ms before it to the end of
    that frame whose magnitude exceeds both 6x the pre-roll noise RMS and -24 dB
    re the attack peak (codec pre-echo sits below that; a struck bar's radiated
    pressure passes it within a fraction of a millisecond of contact).
    """
    x = np.asarray(x, dtype=np.float64)
    win = max(2, int(round(0.002 * fs)))
    power = np.convolve(x * x, np.ones(win) / win, mode="same")
    peak = float(power.max())
    if not peak > 0:
        raise ValueError("silent signal")
    first = int(np.flatnonzero(power >= peak * 10 ** (threshold_db / 10))[0])
    pre = x[:max(0, first - int(0.005 * fs))]
    noise = float(np.sqrt(np.mean(pre * pre))) if len(pre) >= int(0.005 * fs) else 0.0
    attack_peak = float(np.max(np.abs(x[first:first + win + int(0.01 * fs) + 1])))
    level = max(6.0 * noise, attack_peak * 10 ** (-24 / 20))
    start = max(0, first - int(0.02 * fs))
    above = np.flatnonzero(np.abs(x[start:first + win + 1]) > level)
    return start + int(above[0]) if len(above) else first


def estimate_noise(x, fs, onset, end):
    """Noise floor: pre-roll RMS when there is >= 20 ms of it, else the quietest tail frame."""
    pre = x[:max(0, onset - int(0.002 * fs))]
    frame = int(0.02 * fs)
    if len(pre) >= frame and np.count_nonzero(pre) > 0.5 * len(pre):
        return float(np.sqrt(np.mean(pre * pre))), "pre-onset RMS"
    tail = x[min(end, onset + int(0.1 * fs)):end]
    count = len(tail) // frame
    if count >= 3:
        rms = np.sqrt(np.mean(tail[:count * frame].reshape(count, frame) ** 2, axis=1))
        rms = rms[rms > 0]
        if len(rms):
            return float(np.percentile(rms, 5)), "quietest 20 ms tail frames (upper bound)"
    return 0.0, "unavailable"


def attack_centroid(x, fs, onset, fmax, length_s=0.020):
    """Spectral centroid of the first 20 ms (half-cosine taper keeps the attack)."""
    seg = x[onset:onset + int(length_s * fs)]
    if len(seg) < 16:
        return None
    taper = np.cos(0.5 * np.pi * np.arange(len(seg)) / len(seg)) ** 2
    n = sfft.next_fast_len(8 * len(seg))
    power = np.abs(np.fft.rfft(seg * taper, n)) ** 2
    f = np.fft.rfftfreq(n, 1.0 / fs)
    use = (f >= F_LOW) & (f <= fmax)
    total = float(power[use].sum())
    return float((f[use] * power[use]).sum() / total) if total > 0 else None


# ───────────────────────────── band decomposition ─────────────────────────────

def flat_top(f, lo, hi, sigma_f):
    """Band response: a rectangle [lo, hi] convolved with a Gaussian of std sigma_f.

    Its kernel is a sinc times a Gaussian of std 1/(2 pi sigma_f) in time, so the
    band output at t depends only on input within +-4.5 of those sigmas. For a
    complex frequency f + i sigma/(2 pi) this is the exact gain for a damped
    exponential (analytic continuation), used to correct amplitudes.
    """
    s = math.sqrt(2.0) * sigma_f
    return 0.5 * (special.erf((f - lo) / s) - special.erf((f - hi) / s))


def band_layout(f_lo, f_hi, sigma_f):
    """Contiguous core bands [lo, hi) tiling f_lo..f_hi, widening with frequency."""
    edges = [f_lo]
    while edges[-1] < f_hi - 1e-6:
        width = max(8.0 * sigma_f, 0.2 * edges[-1])
        nxt = edges[-1] + width
        if f_hi - nxt < 0.5 * width:
            nxt = f_hi
        edges.append(min(nxt, f_hi))
    return list(zip(edges[:-1], edges[1:]))


class Spectrum:
    """One FFT of the post-onset signal, padded so circular filtering is linear."""

    def __init__(self, x_post, fs, pad_s):
        self.fs = fs
        self.length_s = len(x_post) / fs
        n = sfft.next_fast_len(len(x_post) + int(math.ceil(2 * pad_s * fs)) + 16)
        buffer = np.zeros(n)
        buffer[:len(x_post)] = x_post
        self.n = n
        self.df = fs / n
        self.spec = np.fft.fft(buffer)

    def band(self, lo, hi, sigma_f, min_rate, gaussian_center=None):
        """Complex baseband of one band at a reduced rate: (y, dt, fc, noise bandwidth).

        y[m] is the band-filtered analytic signal at t = m*dt after the onset,
        heterodyned by fc (an exact bin frequency). With gaussian_center the band
        is a plain Gaussian of std sigma_f around that frequency.
        """
        center = gaussian_center if gaussian_center is not None else 0.5 * (lo + hi)
        kc = int(round(center / self.df))
        fc = kc * self.df
        span = (0.0 if gaussian_center is not None else hi - lo) + 11.0 * sigma_f
        k = sfft.next_fast_len(int(math.ceil(max(span, min_rate) / self.df)) + 1)
        j = np.arange(k) - k // 2
        f = fc + j * self.df
        if gaussian_center is not None:
            g = np.exp(-0.5 * ((f - gaussian_center) / sigma_f) ** 2)
        else:
            g = flat_top(f, lo, hi, sigma_f)
        bins = self.spec[(kc + j) % self.n] * g
        y = np.fft.ifft(np.fft.ifftshift(bins)) * (k / self.n)
        return y, self.n / (k * self.fs), fc, float(np.sum(g * g) * self.df)


# ─────────────────────────────────── ESPRIT ───────────────────────────────────

def hankel_covariance(y, rows):
    """R[a, b] = sum_{j<K} y[j+a] conj(y[j+b]) / K for the rows x K Hankel matrix.

    One correlation for the first row, then each diagonal by the recursion
    R[a, b] = R[a-1, b-1] + y[K+a-1] conj(y[K+b-1]) - y[a-1] conj(y[b-1]):
    O(K rows) instead of O(K rows^2).
    """
    k = len(y) - rows + 1
    cov = np.empty((rows, rows), dtype=complex)
    cov[0] = [np.vdot(y[b:b + k], y[:k]) for b in range(rows)]
    head, tail = y[:rows - 1].conj(), y[k:k + rows - 1].conj()
    for a in range(1, rows):
        cov[a, 1:] = cov[a - 1, :-1] + y[k + a - 1] * tail - y[a - 1] * head
        cov[a, 0] = np.conj(cov[0, a])
    return cov / k


def esprit_subspace(y):
    """Eigen-decomposition of the forward Hankel covariance (descending)."""
    n = len(y)
    rows = int(min(max(n // 3, 6), 160))
    if n - rows + 1 < rows:
        return None
    cov = hankel_covariance(y, rows)
    w, u = np.linalg.eigh(0.5 * (cov + cov.conj().T))
    return w[::-1], u[:, ::-1]


def esprit_poles(u, order):
    sig = u[:, :order]
    phi = np.linalg.lstsq(sig[:-1], sig[1:], rcond=None)[0]
    return np.linalg.eigvals(phi)


def fit_amplitudes(y, z):
    """Least-squares complex amplitudes of y[m] = sum c z^m; returns (z, c, residual)."""
    n = len(y)
    z = z[np.isfinite(z) & (np.abs(z) > 1e-12)]
    z = z[(n - 1) * np.log(np.abs(z)) < math.log(1e4)]      # drop explosive poles
    if len(z) == 0:
        return z, np.zeros(0, complex), y
    v = np.exp(np.outer(np.arange(n), np.log(z)))
    c = np.linalg.lstsq(v, y, rcond=None)[0]
    return z, c, y - v @ c


@dataclass
class Pole:
    window: int
    freq: float
    sigma: float
    amp0: complex            # A e^{i phi} at the onset, x(t) = A e^{-sigma t} cos(2 pi f t + phi)
    snr_db: float
    energy: float            # input-referred energy inside the window
    crb_sigma: float         # Cramer-Rao std of sigma (1/s)
    crb_freq: float          # Cramer-Rao std of freq (Hz)
    t_start: float           # first and last sample of the (band's) window
    t_end: float

    @property
    def valid(self):
        """Decaying, narrower than MAX_BANDWIDTH, and not implausibly loud at the onset:
        a strike partial first seen at t_start cannot have decayed by more than
        MAX_EXTRAPOLATION_DB since the onset (codec frames and noise can)."""
        return (self.sigma > 0 and self.sigma / math.pi <= MAX_BANDWIDTH * self.freq
                and self.sigma * self.t_start <= MAX_EXTRAPOLATION_DB / (20 * math.log10(math.e)))

    @property
    def t60(self):
        return LN1000 / self.sigma if self.sigma > 0 else math.inf

    def amp_at(self, t):
        return self.amp0 * np.exp((-self.sigma + 2j * math.pi * self.freq) * t)


def analyse_band(spectrum, windex, t0, t1, lo, hi, sigma_f):
    """ESPRIT on one band of one window: significant poles whose frequency is in [lo, hi)."""
    y, dt, fc, enbw = spectrum.band(lo, hi, sigma_f, min_rate=160.0 / (t1 - t0))
    m0, m1 = int(math.ceil(t0 / dt)), int(math.floor(t1 / dt))
    seg = y[m0:m1 + 1]
    if len(seg) < 24:
        return []
    # When this band decays into its noise within the window (a fall of 20 dB or
    # more), drop what follows its last point 5 dB above that floor.
    smooth = max(4, len(seg) // 50)
    power = np.convolve(np.abs(seg) ** 2, np.ones(smooth) / smooth, mode="same")
    floor = np.percentile(power, 5)
    if power.max() > 100.0 * floor:
        live = np.flatnonzero(power > 3.0 * floor)
        seg = seg[:max(int(live[-1]) + 1, min(len(seg), 96))]
    t_start = m0 * dt
    scale = float(np.sqrt(np.mean(np.abs(seg) ** 2)))
    if not scale > 0:
        return []
    seg = seg / scale
    sub = esprit_subspace(seg)
    if sub is None:
        return []
    w, u = sub
    order = int(min(24, len(w) // 2 - 1, len(seg) - len(w) - 1))
    if order < 1:
        return []
    poles = []
    for _ in range(2):          # overfit, keep significant poles, refit at that order
        z, c, resid = fit_amplitudes(seg, esprit_poles(u, order))
        n0 = max(float(np.mean(np.abs(resid) ** 2)), 1e-30) / enbw   # input noise PSD (scaled)
        n = np.arange(len(seg))
        poles, significant = [], 0
        for zk, ck in zip(z, c):
            alpha, omega = math.log(abs(zk)), float(np.angle(zk))
            sigma, freq = -alpha / dt, fc + omega / (2 * math.pi * dt)
            if abs(sigma) / (2 * math.pi) > 3.0 * sigma_f:
                continue            # changes within the kernel: its band gain is not trustworthy
            gain = complex(flat_top(freq + 1j * sigma / (2 * math.pi), lo, hi, sigma_f))
            if not abs(gain) >= 1e-3:
                continue
            weights = np.exp(2 * alpha * n)
            s0, s1, s2 = float(weights.sum()), float((n * weights).sum()), float((n * n * weights).sum())
            energy = abs(ck / gain) ** 2 * s0 * dt
            snr = energy / n0
            if snr < 10 ** (SNR_MIN_DB / 10):
                continue
            significant += 1
            if not lo <= freq < hi:
                continue            # belongs to a neighbouring band
            u_k = complex(math.log(abs(zk)), omega) / dt
            amp0 = 2.0 * ck * scale * np.exp(-u_k * t_start) / gain
            # Single-pole Cramer-Rao bound in the decimated domain.
            noise_var = n0 * abs(complex(flat_top(freq, lo, hi, sigma_f))) ** 2 / dt
            det = s0 * s2 - s1 * s1
            var = noise_var * s0 / (2 * abs(ck) ** 2 * det) if det > 0 and abs(ck) > 0 else math.inf
            std = math.sqrt(var)
            poles.append(Pole(windex, freq, sigma, complex(amp0), 10 * math.log10(snr), energy * scale ** 2,
                              std / dt, std / (2 * math.pi * dt), t_start, t_start + (len(seg) - 1) * dt))
        if significant == 0:
            return []
        if significant >= order:
            break
        order = significant
    return poles


# ────────────────────────────── partial assembly ──────────────────────────────

def erb(freq):
    """Equivalent rectangular bandwidth of hearing at freq (Glasberg & Moore 1990), Hz."""
    return 24.7 * (4.37 * freq / 1000.0 + 1.0)


def satellite_range(freq):
    """Half an ERB, but at most 40 Hz: the offset of an envelope-shape pole."""
    return min(0.5 * erb(freq), 40.0)


@dataclass
class Partial:
    """A cluster of poles at one frequency, gathered from every window and band."""
    poles: list = field(default_factory=list)         # significant poles, valid or not
    tolerance: float = 1.0                            # cluster half-width in Hz
    primary: Pole | None = None                       # reported estimate (smallest bound)
    window_reps: dict = field(default_factory=dict)   # strongest valid pole per window
    satellites: list = field(default_factory=list)    # weaker clusters within half an ERB
    early: list = field(default_factory=list)         # first-window poles nearest to it

    @property
    def center(self):
        return self.poles[0].freq

    @property
    def freq(self):
        return self.primary.freq

    def strength(self):
        """Energy from 20 to 220 ms of the primary estimate (its level at 20 ms, not
        the onset extrapolation, so fast partials do not dominate): the ranking."""
        p = self.primary
        return abs(p.amp_at(0.02)) ** 2 / 2.0 * (1 - math.exp(-0.4 * p.sigma)) / (2 * p.sigma)

    def envelope(self, t):
        """Amplitude of this partial at time t: the sum of the first-window poles
        assigned to it (a blooming or beating partial needs several) while t lies
        in that window, else its primary estimate. A band's first sample lies up
        to one decimated period after the nominal window start."""
        members = [q for q in self.early if q.t_start - 0.0025 <= t <= q.t_end + 0.0025]
        if members:
            return abs(sum(q.amp_at(t) for q in members))
        return abs(self.primary.amp_at(t))


def cluster_poles(poles, f0_nominal):
    """Leader clustering of all poles by frequency; picks each cluster's primary:
    its strongest valid pole in the window with the smallest relative bound on
    the damping (fast partials: early window; long partials: late window)."""
    clusters = []
    for pole in sorted(poles, key=lambda p: -p.snr_db):
        best = min(clusters, key=lambda c: abs(c.center - pole.freq), default=None)
        if best is not None and abs(best.center - pole.freq) <= best.tolerance:
            best.poles.append(pole)
        else:
            tolerance = max(1.0, min(0.008 * pole.freq, 0.25 * f0_nominal))
            clusters.append(Partial(poles=[pole], tolerance=tolerance))
    for cluster in clusters:
        for pole in cluster.poles:
            current = cluster.window_reps.get(pole.window)
            if pole.valid and (current is None or pole.energy > current.energy):
                cluster.window_reps[pole.window] = pole
        if cluster.window_reps:
            cluster.primary = min(cluster.window_reps.values(),
                                  key=lambda p: (p.crb_sigma / p.sigma, p.t_start - p.t_end))
    return clusters


def group_partials(clusters, poles):
    """Musical partials. A cluster within SATELLITE_RANGE of one at least
    SATELLITE_DB stronger (energy from 20 to 220 ms) becomes its satellite: such
    poles describe that partial's envelope (bloom, beating; time structure of
    25 ms or longer) and the ear fuses them with it. Every significant first-
    window pole (decaying or not) is then assigned to the nearest partial within
    half an ERB; their sum is that partial's early envelope, counted once."""
    leaders = []
    for cluster in sorted((c for c in clusters if c.primary), key=lambda c: -c.strength()):
        limit = cluster.strength() * 10 ** (SATELLITE_DB / 10)
        host = next((p for p in leaders if abs(cluster.freq - p.freq) <= satellite_range(p.freq)
                     and p.strength() >= limit), None)
        (host.satellites if host else leaders).append(cluster)
    for pole in poles:
        if pole.window == 0 and leaders:
            host = min(leaders, key=lambda p: abs(p.freq - pole.freq))
            if abs(host.freq - pole.freq) <= max(0.5 * erb(host.freq), host.tolerance):
                host.early.append(pole)
    return leaders


# ─────────────────────────────────── analysis ───────────────────────────────────

def parse_windows(text):
    """'0.02-0.12,0.05-0.4,0.2-end' -> ((0.02, 0.12), (0.05, 0.4), (0.2, None))."""
    windows = []
    for part in text.split(","):
        start, _, end = part.strip().partition("-")
        start, end = float(start), None if end.strip() in ("end", "") else float(end)
        if not start > GUARD_S or (end is not None and end <= start):
            raise ValueError(f"window {part!r} must start after {GUARD_S} s and end after it starts")
        windows.append((start, end))
    return tuple(windows)


def window_label(start, end):
    return f"{start * 1000:.0f}-{end * 1000:.0f} ms"


def analyse_signal(x, fs, note, max_partials=12, fmax=16000.0, windows=DEFAULT_WINDOWS,
                   onset=None, meta=None):
    """Analyse a mono signal; returns the per-file result dictionary."""
    x = np.asarray(x, dtype=np.float64)
    warnings = []
    nonzero = np.flatnonzero(x != 0)
    if len(nonzero) == 0:
        raise ValueError("signal is digital silence")
    end = int(nonzero[-1]) + 1
    if onset is None:
        onset = detect_onset(x[:end], fs)
    peak = float(np.max(np.abs(x)))
    clipped = int(np.count_nonzero(np.abs(x) >= 0.9999))
    if clipped >= 3:
        warnings.append(f"{clipped} samples at full scale: the recording may be clipped")
    noise_rms, noise_method = estimate_noise(x, fs, onset, end)
    x_post = x[onset:end]
    t_signal = len(x_post) / fs
    f_hi = min(fmax, 0.45 * fs)
    spectrum = Spectrum(x_post, fs, pad_s=KERNEL_SIGMAS * MAX_KERNEL_S + 0.05)
    f0_nominal = note["nominal_hz"]

    poles, used_windows = [], []
    for w0, w1 in windows:
        sigma_t = min(MAX_KERNEL_S, (w0 - GUARD_S) / KERNEL_SIGMAS)
        if sigma_t < 0.0008:
            warnings.append(f"window starting at {w0 * 1000:.0f} ms is too early for a band kernel; skipped")
            continue
        t1 = t_signal - KERNEL_SIGMAS * sigma_t if w1 is None else min(w1, t_signal - KERNEL_SIGMAS * sigma_t)
        if t1 - w0 < 0.03:
            warnings.append(f"recording too short for the {w0 * 1000:.0f} ms window; skipped")
            continue
        sigma_f = 1.0 / (2 * math.pi * sigma_t)
        for lo, hi in band_layout(F_LOW, f_hi, sigma_f):
            poles += analyse_band(spectrum, len(used_windows), w0, t1, lo, hi, sigma_f)
        used_windows.append((w0, t1))
    if not poles:
        raise ValueError("no partial found above the noise")

    partials = group_partials(cluster_poles(poles, f0_nominal), poles)
    if not partials:
        raise ValueError("no decaying partial found above the noise")
    # The fundamental: the strongest partial within F0_SEARCH_CENTS of the nominal note.
    near = [p for p in partials if abs(1200 * math.log2(p.freq / f0_nominal)) <= F0_SEARCH_CENTS]
    if near:
        fundamental = max(near, key=Partial.strength)
    else:
        strong = max(p.strength() for p in partials)
        candidates = [p for p in partials if p.strength() >= strong * 1e-2]
        fundamental = min(candidates, key=lambda p: p.freq)
        warnings.append(f"no partial within {F0_SEARCH_CENTS:.0f} cents of {note['name']} "
                        f"({f0_nominal:.2f} Hz); using the lowest strong partial, {fundamental.freq:.2f} Hz")
    f0 = fundamental.freq

    # Reject partials more than LEVEL_FLOOR_DB below the strongest one, compared
    # where each is measured (its primary window start); rank by energy 20-220 ms.
    def relative_level(partial):
        t = partial.primary.t_start
        strongest = max(abs(q.primary.amp_at(t)) for q in partials)
        return 20 * math.log10(max(abs(partial.primary.amp_at(t)), 1e-300) / strongest)
    kept = [p for p in partials if p is fundamental or relative_level(p) >= LEVEL_FLOOR_DB]
    ranked = sorted(kept, key=lambda p: -p.strength())
    chosen = [fundamental] + [p for p in ranked if p is not fundamental][:max(0, max_partials - 1)]
    rank = {id(p): i + 1 for i, p in enumerate(ranked)}

    ref20, ref100 = fundamental.envelope(0.02), fundamental.envelope(0.10)
    rows = []
    for partial in sorted(chosen, key=lambda p: p.freq):
        p = partial.primary
        a = abs(p.amp0)
        level_20 = 20 * math.log10(max(partial.envelope(0.02), 1e-300) / ref20)
        level_100 = 20 * math.log10(max(partial.envelope(0.10), 1e-300) / ref100)
        satellites = sorted((c for c in partial.satellites if c.primary), key=lambda c: -c.strength())
        rows.append({
            "rank": rank.get(id(partial)),
            "freq_hz": round(p.freq, 4),
            "freq_std_hz": float(f"{p.crb_freq:.3g}"),
            "ratio": round(p.freq / f0, 5),
            "t60_s": round(p.t60, 4),
            "t60_re_f0": round(p.t60 / fundamental.primary.t60, 4),
            "sigma_per_s": round(p.sigma, 4),
            "eta": round(p.sigma / (math.pi * p.freq), 6),
            "amp": float(f"{a:.6g}"),
            "amp_dbfs": round(20 * math.log10(a), 2),
            "phase_rad": round(float(np.angle(p.amp0)), 4),
            "level_20ms_db": round(level_20, 2),
            "level_100ms_db": round(level_100, 2),
            "snr_db": round(p.snr_db, 1),
            "t60_rel_std": round(p.crb_sigma / p.sigma, 5),
            "window": window_label(*used_windows[p.window]),
            "per_window": [{"window": window_label(*used_windows[w]), "freq_hz": round(q.freq, 3),
                            "t60_s": round(q.t60, 4), "amp_dbfs": round(20 * math.log10(abs(q.amp0)), 2),
                            "snr_db": round(q.snr_db, 1)}
                           for w, q in sorted(partial.window_reps.items())],
            "sub_poles": [sub_pole(c, f0, used_windows) for c in satellites[:6]],
        })

    # Only partials within 20 dB of the fundamental at 20 ms constrain the bloom kernel.
    loud = [p.freq for p in partials if p is not fundamental and p.envelope(0.02) >= 0.1 * ref20]
    bloom = measure_bloom(spectrum, f0, loud, fundamental.primary.t60)
    centroid = attack_centroid(x, fs, onset, f_hi)
    return {
        "file": None,
        "note": note,
        "f0_hz": round(f0, 4),
        "f0_cents_re_nominal": round(1200 * math.log2(f0 / f0_nominal), 2),
        "partials": rows,
        "bloom": bloom,
        "attack": {"centroid_hz": None if centroid is None else round(centroid, 1),
                   "centroid_re_f0": None if centroid is None else round(centroid / f0, 3)},
        "noise_floor": {"rms_dbfs": round(20 * math.log10(noise_rms), 2) if noise_rms > 0 else None,
                        "db_re_peak": round(20 * math.log10(noise_rms / peak), 2) if noise_rms > 0 else None,
                        "method": noise_method},
        "metadata": dict(meta or {}, **{
            "sample_rate": fs, "onset_s": round(onset / fs, 5), "end_s": round(end / fs, 4),
            "digital_silence_from_s": round(end / fs, 4) if len(x) - end > int(0.01 * fs) else None,
            "peak_dbfs": round(20 * math.log10(peak), 2),
            "windows": [window_label(*w) for w in used_windows],
            "partials_found": len(kept), "max_partials": max_partials, "fmax_hz": f_hi,
            "tool": f"{TOOL} {VERSION}"}),
        "warnings": warnings,
    }


def sub_pole(cluster, f0, used_windows):
    """A satellite's own estimate (it also contributes to its partial's L20/L100)."""
    p = cluster.primary
    return {"freq_hz": round(p.freq, 3), "ratio": round(p.freq / f0, 5), "t60_s": round(p.t60, 4),
            "amp_dbfs": round(20 * math.log10(abs(p.amp0)), 2),
            "window": window_label(*used_windows[p.window])}


def rise_time_constant(t_peak, tau_decay):
    """tau_r such that exp(-t/tau_decay) - exp(-t/tau_r) peaks at t_peak.

    The peak time ln(tau_d/tau_r) tau_d tau_r / (tau_d - tau_r) rises from 0 to
    tau_d as tau_r goes from 0 to tau_d, so a geometric bisection finds it.
    """
    if not 0 < t_peak < tau_decay:
        return None

    def peak(tau_r):
        return math.log(tau_decay / tau_r) * tau_decay * tau_r / (tau_decay - tau_r)
    lo, hi = 1e-7, tau_decay * (1 - 1e-9)
    for _ in range(200):
        mid = math.sqrt(lo * hi)
        lo, hi = (mid, hi) if peak(mid) < t_peak else (lo, mid)
    return math.sqrt(lo * hi)


def measure_bloom(spectrum, f0, other_freqs, t60_f0=None):
    """Heterodyne envelope of the fundamental: peak time and rise from 20 ms.

    A Gaussian band around f0 (std 1/(2 pi 3 ms), narrower only when needed to
    reject DC, the negative-frequency image or a distinct partial beyond half an
    ERB by > 60 dB); everything closer to f0 is part of the fundamental's
    envelope. Near the onset the envelope is divided by the kernel's mass after
    the onset (normalised convolution). With the fundamental's T60, the peak
    time also gives the build-up time constant of an equivalent resonator
    (envelope exp(-t/tau_d) - exp(-t/tau_r)) and its Q = pi f0 tau_r.
    """
    distinct = [abs(f - f0) for f in other_freqs if abs(f - f0) > 0.5 * erb(f0)]
    sigma_f = min(min([f0] + distinct) / 3.7, 1.0 / (2 * math.pi * 0.003))
    sigma_t = 1.0 / (2 * math.pi * sigma_f)
    y, dt, fc, _ = spectrum.band(0, 0, sigma_f, min_rate=2000.0, gaussian_center=f0)
    horizon = min(0.8, spectrum.length_s - KERNEL_SIGMAS * sigma_t)
    if horizon < 0.03:
        return None
    t = np.arange(int(horizon / dt) + 1) * dt
    env = 2.0 * np.abs(y[:len(t)]) / (0.5 * (1.0 + special.erf(t / (math.sqrt(2) * sigma_t))))
    peak_index = int(np.argmax(env))
    peak = float(env[peak_index])

    def at(ms):
        return float(np.interp(ms / 1000.0, t, env))
    t_peak = float(t[peak_index])
    tau_r = rise_time_constant(t_peak, t60_f0 / LN1000) if t60_f0 and t_peak > 2 * sigma_t else None
    return {
        "peak_ms": round(t_peak * 1000, 1),
        "peak_db_re_20ms": round(20 * math.log10(peak / at(20)), 2) if at(20) > 0 else None,
        "rise_tau_ms": None if tau_r is None else round(tau_r * 1000, 1),
        "equivalent_q": None if tau_r is None else round(math.pi * f0 * tau_r, 1),
        "resolution_ms": round(sigma_t * 1000, 2),
        "envelope_db_re_peak": {str(ms): round(20 * math.log10(max(at(ms), 1e-300) / peak), 2)
                                for ms in ENVELOPE_TIMES_MS if ms / 1000.0 <= horizon},
    }


def analyse_file(path, note_text=None, **options):
    samples, fs, encoding = read_wav(path)
    note = resolve_note(note_text, path)
    onset_s = options.pop("onset_s", None)
    meta = {"channels": samples.shape[1], "format": encoding, "duration_s": round(len(samples) / fs, 4),
            "sha256": hashlib.sha256(Path(path).read_bytes()).hexdigest()}
    onset = None if onset_s is None else int(round(onset_s * fs))
    result = analyse_signal(samples.mean(axis=1), fs, note, onset=onset, meta=meta, **options)
    result["file"] = str(path)
    return result


# ─────────────────────────────── comparison ───────────────────────────────

def compare_results(ref, model, max_cents=50.0):
    """Match partials by ratio and tabulate ratio, level and T60 differences."""
    rows, used = [], set()
    for rp in sorted(ref["partials"], key=lambda p: p["rank"] or 999):
        best = None
        for i, mp in enumerate(model["partials"]):
            cents = 1200 * math.log2(mp["ratio"] / rp["ratio"])
            if i not in used and abs(cents) <= max_cents and (best is None or abs(cents) < abs(best[1])):
                best = (i, cents)
        mp = None
        if best is not None:
            used.add(best[0])
            mp = model["partials"][best[0]]
        rows.append({
            "ref_ratio": rp["ratio"], "model_ratio": mp and mp["ratio"],
            "ratio_cents": None if mp is None else round(best[1], 1),
            "ref_level_20ms_db": rp["level_20ms_db"], "model_level_20ms_db": mp and mp["level_20ms_db"],
            "level_20ms_diff_db": None if mp is None else round(mp["level_20ms_db"] - rp["level_20ms_db"], 2),
            "ref_level_100ms_db": rp["level_100ms_db"], "model_level_100ms_db": mp and mp["level_100ms_db"],
            "level_100ms_diff_db": None if mp is None else round(mp["level_100ms_db"] - rp["level_100ms_db"], 2),
            "ref_t60_s": rp["t60_s"], "model_t60_s": mp and mp["t60_s"],
            "t60_model_over_ref": None if mp is None else round(mp["t60_s"] / rp["t60_s"], 3),
        })
    rows.sort(key=lambda r: r["ref_ratio"])
    extra = [{"model_ratio": mp["ratio"], "model_level_20ms_db": mp["level_20ms_db"], "model_t60_s": mp["t60_s"]}
             for i, mp in enumerate(model["partials"]) if i not in used]
    rb, mb = ref.get("bloom") or {}, model.get("bloom") or {}
    return {"reference": ref, "model": model, "diff": {
        "f0_cents": round(1200 * math.log2(model["f0_hz"] / ref["f0_hz"]), 2),
        "bloom_peak_ms": [rb.get("peak_ms"), mb.get("peak_ms")],
        "bloom_db_re_20ms": [rb.get("peak_db_re_20ms"), mb.get("peak_db_re_20ms")],
        "attack_centroid_re_f0": [ref["attack"]["centroid_re_f0"], model["attack"]["centroid_re_f0"]],
        "partials": rows, "unmatched_model_partials": extra}}


def load_result(path, midi=None):
    data = json.loads(Path(path).read_text())
    if isinstance(data, dict) and "partials" in data:
        return data
    if isinstance(data, list) and data:
        for entry in data:
            if midi is not None and entry.get("note", {}).get("midi") == midi:
                return entry
        return data[0]
    raise ValueError(f"{path}: not a {TOOL} result")


# ─────────────────────────────── markdown ───────────────────────────────

def num(value, spec):
    return "–" if value is None else format(value, spec)


def markdown_result(r):
    b, n = r.get("bloom") or {}, r["noise_floor"]
    lines = [f"### {Path(r['file']).name if r.get('file') else 'signal'} — {r['note']['name']} "
             f"(nominal {r['note']['nominal_hz']:.2f} Hz), f0 {r['f0_hz']:.2f} Hz "
             f"({r['f0_cents_re_nominal']:+.1f} cents)", "",
             f"onset {r['metadata']['onset_s'] * 1000:.1f} ms · bloom peak {num(b.get('peak_ms'), '.0f')} ms, "
             f"{num(b.get('peak_db_re_20ms'), '+.1f')} dB re 20 ms · attack centroid "
             f"{num(r['attack']['centroid_hz'], '.0f')} Hz · noise {num(n['rms_dbfs'], '.1f')} dBFS "
             f"({num(n['db_re_peak'], '.1f')} dB re peak, {n['method']})", "",
             "| # | f (Hz) | ratio | T60 (s) | T60/T60₁ | η | amp (dBFS) | L20 (dB) | L100 (dB) | SNR (dB) | window |",
             "|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|:--|"]
    for p in r["partials"]:
        lines.append(f"| {num(p['rank'], 'd')} | {p['freq_hz']:.2f} | {p['ratio']:.3f} | {p['t60_s']:.3f} | "
                     f"{p['t60_re_f0']:.3f} | {p['eta']:.5f} | {p['amp_dbfs']:.1f} | {p['level_20ms_db']:+.1f} | "
                     f"{p['level_100ms_db']:+.1f} | {p['snr_db']:.0f} | {p['window']} |")
    for w in r["warnings"]:
        lines.append(f"\n> warning: {w}")
    return "\n".join(lines) + "\n"


def markdown_compare(c):
    ref, model, d = c["reference"], c["model"], c["diff"]
    lines = [f"### {Path(ref['file'] or 'reference').name} (reference) vs "
             f"{Path(model['file'] or 'model').name} (model), {ref['note']['name']}", "",
             f"f0 {ref['f0_hz']:.2f} vs {model['f0_hz']:.2f} Hz ({d['f0_cents']:+.1f} cents) · bloom peak "
             f"{num(d['bloom_peak_ms'][0], '.0f')} vs {num(d['bloom_peak_ms'][1], '.0f')} ms, "
             f"{num(d['bloom_db_re_20ms'][0], '+.1f')} vs {num(d['bloom_db_re_20ms'][1], '+.1f')} dB re 20 ms · "
             f"attack centroid {num(d['attack_centroid_re_f0'][0], '.1f')} vs "
             f"{num(d['attack_centroid_re_f0'][1], '.1f')} × f0", "",
             "| ratio ref | ratio model | Δ cents | L20 ref | L20 model | ΔL20 | L100 ref | L100 model | ΔL100 "
             "| T60 ref (s) | T60 model (s) | T60 model/ref |",
             "|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|"]
    for row in d["partials"]:
        lines.append(
            f"| {row['ref_ratio']:.3f} | {num(row['model_ratio'], '.3f')} | {num(row['ratio_cents'], '+.1f')} | "
            f"{row['ref_level_20ms_db']:+.1f} | {num(row['model_level_20ms_db'], '+.1f')} | "
            f"{num(row['level_20ms_diff_db'], '+.1f')} | {row['ref_level_100ms_db']:+.1f} | "
            f"{num(row['model_level_100ms_db'], '+.1f')} | {num(row['level_100ms_diff_db'], '+.1f')} | "
            f"{row['ref_t60_s']:.3f} | {num(row['model_t60_s'], '.3f')} | {num(row['t60_model_over_ref'], '.2f')} |")
    if d["unmatched_model_partials"]:
        extra = ", ".join(f"{p['model_ratio']:.3f} ({p['model_level_20ms_db']:+.1f} dB)"
                          for p in d["unmatched_model_partials"])
        lines.append(f"\nModel partials without a reference match: {extra}")
    return "\n".join(lines) + "\n"


# ─────────────────────────────── self-test ───────────────────────────────

SELF_TEST_CASES = (
    {"name": "marimba-like 1:4:10", "note": "C4", "cents": 7.0, "ratios": (1.0, 4.0, 10.0),
     "t60": (3.0, 0.8, 0.3), "levels_db": (0.0, -8.0, -16.0), "format": "pcm24", "channels": 2},
    {"name": "kalimba-like 1:5.9, fast overtone", "note": "A4", "cents": -12.0, "ratios": (1.0, 5.9),
     "t60": (4.0, 0.35), "levels_db": (0.0, -4.0), "format": "float32", "channels": 1},
    {"name": "balafon-like 1:3.1:7.4 + buzz comb", "note": "G3", "cents": 22.0, "ratios": (1.0, 3.1, 7.4),
     "t60": (1.6, 0.45, 0.2), "levels_db": (0.0, -5.0, -12.0), "buzz_db": -10.0,
     "format": "pcm16", "channels": 2},
    {"name": "marimba-like with resonator bloom", "note": "C3", "cents": 0.0, "ratios": (1.0, 4.0, 10.0),
     "t60": (3.0, 0.9, 0.35), "levels_db": (0.0, -10.0, -18.0), "bloom": (0.97, 0.06),
     "format": "pcm24", "channels": 1},
)


def synthesize_case(case, fs, seed, seconds=3.0, noise_db=-60.0):
    """A strike with known modes, a pre-roll and white noise at noise_db re the peak."""
    rng = np.random.default_rng(seed)
    f0 = midi_to_hz(note_token_to_midi(case["note"])) * 2 ** (case["cents"] / 1200)
    onset_s = 0.030 + rng.random() * 0.001
    t = np.arange(int(seconds * fs)) / fs - onset_s
    after = t >= 0
    x = np.zeros_like(t)
    modes = []
    for ratio, t60, level_db in zip(case["ratios"], case["t60"], case["levels_db"]):
        sigma = LN1000 / t60
        amp = 0.5 * 10 ** (level_db / 20)
        phase = rng.uniform(-math.pi, math.pi)
        env = amp * np.exp(-sigma * t)
        if ratio == 1.0 and "bloom" in case:
            depth, rise = case["bloom"]
            env = amp * (np.exp(-sigma * t) - depth * np.exp(-(sigma + 1.0 / rise) * t))
        x += np.where(after, env * np.cos(2 * math.pi * f0 * ratio * t + phase), 0.0)
        modes.append({"freq": f0 * ratio, "t60": t60, "amp_db": 20 * math.log10(amp), "sigma": sigma,
                      "amp": amp, "bloom": case.get("bloom") if ratio == 1.0 else None})
    if "buzz_db" in case:
        # Phase-locked harmonics 2..20 of f0 under a 1.4 kHz formant; exponential,
        # then gated off between 100 and 350 ms (a buzz stops as the note decays).
        k = np.arange(2, 21)
        weights = np.exp(-0.5 * (np.log2(k * f0 / 1400.0) / 0.6) ** 2)
        weights *= 0.5 * 10 ** (case["buzz_db"] / 20) / math.sqrt(np.sum(weights ** 2))
        gate = np.exp(-t / 0.12) * 0.5 * (1 + np.cos(math.pi * np.clip((t - 0.1) / 0.25, 0, 1)))
        phases = rng.uniform(-math.pi, math.pi, len(k))
        buzz = sum(w * np.cos(2 * math.pi * kk * f0 * t + ph) for w, kk, ph in zip(weights, k, phases))
        x += np.where(after, gate * buzz, 0.0)
    peak = float(np.max(np.abs(x)))
    x += rng.standard_normal(len(x)) * peak * 10 ** (noise_db / 20)
    return x, f0, onset_s, modes


def true_envelope(mode, t):
    env = mode["amp"] * math.exp(-mode["sigma"] * t)
    if mode["bloom"]:
        depth, rise = mode["bloom"]
        env -= mode["amp"] * depth * math.exp(-(mode["sigma"] + 1.0 / rise) * t)
    return env


def self_test():
    fs = 48000
    failures, lines = [], []

    def check(ok, text):
        lines.append(("  ok   " if ok else "  FAIL ") + text)
        if not ok:
            failures.append(text)

    with tempfile.TemporaryDirectory() as tmp:
        for index, case in enumerate(SELF_TEST_CASES):
            x, f0, onset_s, modes = synthesize_case(case, fs, seed=1000 + index)
            path = Path(tmp) / f"selftest_{case['note']}.wav"
            data = np.repeat(x[:, None], case["channels"], axis=1)
            write_wav(path, data, fs, case["format"], dither_seed=7 if case["format"] != "float32" else None)
            result = analyse_file(path, max_partials=24 if "buzz_db" in case else 12)
            lines.append(f"{case['name']} ({case['note']}, {case['format']}, {case['channels']} ch): "
                         f"f0 {result['f0_hz']:.3f} Hz, {len(result['partials'])} partials reported")
            check(abs(result["metadata"]["onset_s"] - onset_s) <= 0.001,
                  f"onset {result['metadata']['onset_s'] * 1000:.2f} ms vs {onset_s * 1000:.2f} ms")
            nf = result["noise_floor"]["db_re_peak"]
            check(nf is not None and abs(nf + 60.0) <= 3.0, f"noise floor {nf} dB re peak vs -60")
            check(abs(result["f0_hz"] / f0 - 1) <= 0.002, f"f0 {result['f0_hz']:.3f} vs {f0:.3f} Hz")
            ref = modes[0]
            matched = set()
            for mode in modes:
                best = min(result["partials"], key=lambda p: abs(p["freq_hz"] - mode["freq"]))
                matched.add(id(best))
                df = best["freq_hz"] / mode["freq"] - 1
                dt60 = best["t60_s"] / mode["t60"] - 1
                damp = best["amp_dbfs"] - mode["amp_db"]
                check(abs(df) <= 0.002, f"  {mode['freq']:8.2f} Hz: freq error {df * 100:+.4f} %")
                check(abs(dt60) <= 0.10, f"  {mode['freq']:8.2f} Hz: T60 {best['t60_s']:.3f} vs "
                                         f"{mode['t60']:.3f} s ({dt60 * 100:+.1f} %)")
                check(abs(damp) <= 1.5, f"  {mode['freq']:8.2f} Hz: onset amplitude {best['amp_dbfs']:.2f} vs "
                                        f"{mode['amp_db']:.2f} dBFS ({damp:+.2f} dB)")
                if mode is not ref:
                    for ms, key in ((20, "level_20ms_db"), (100, "level_100ms_db")):
                        truth = 20 * math.log10(true_envelope(mode, ms / 1000) / true_envelope(ref, ms / 1000))
                        check(abs(best[key] - truth) <= 1.5,
                              f"  {mode['freq']:8.2f} Hz: level at {ms} ms {best[key]:+.2f} vs {truth:+.2f} dB re f0")
            harmonic = "buzz_db" in case
            spurious = [p for p in result["partials"] if id(p) not in matched and p["level_20ms_db"] > -40
                        and not (harmonic and abs(p["ratio"] - round(p["ratio"])) < 0.01)]
            check(not spurious, f"no spurious partial above -40 dB re f0 at 20 ms"
                                f"{'' if not spurious else ': ' + str([p['freq_hz'] for p in spurious])}")
            if harmonic:
                buzz = [p for p in result["partials"] if abs(p["ratio"] - round(p["ratio"])) < 0.01 and p["ratio"] > 1.5]
                check(len(buzz) >= 3, f"buzz comb visible as {len(buzz)} harmonic partials")
            if "bloom" in case:
                grid = np.arange(0, 0.8, 1e-4)
                env = np.array([true_envelope(ref, t) for t in grid])
                t_peak = float(grid[np.argmax(env)])
                gain = 20 * math.log10(env.max() / true_envelope(ref, 0.02))
                b = result["bloom"]
                check(abs(b["peak_ms"] / 1000 - t_peak) <= max(0.005, 0.1 * t_peak),
                      f"bloom peak {b['peak_ms']:.1f} vs {t_peak * 1000:.1f} ms")
                check(abs(b["peak_db_re_20ms"] - gain) <= 1.5,
                      f"bloom rise {b['peak_db_re_20ms']:+.2f} vs {gain:+.2f} dB re 20 ms")
    print("\n".join(lines))
    print(f"\nself-test: {'FAILED' if failures else 'passed'} "
          f"({len(failures)} failure{'s' if len(failures) != 1 else ''})")
    return 1 if failures else 0


# ─────────────────────────────────── CLI ───────────────────────────────────

def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("inputs", nargs="*", help="WAV files (or, for comparisons, a stored JSON result)")
    parser.add_argument("--note", help="nominal note for every input: C4, F#3, Bb2, Cs4, MIDI 60 or 261.6Hz "
                                       "(default: parsed from each file name, e.g. *_C4.wav)")
    parser.add_argument("--max-partials", type=int, default=12, help="partials to report (default 12)")
    parser.add_argument("--fmax", type=float, default=16000.0, help="highest analysed frequency in Hz (default 16000)")
    parser.add_argument("--windows", type=parse_windows, default=DEFAULT_WINDOWS,
                        help="analysis windows in seconds after the onset (default 0.02-0.12,0.05-0.4,0.2-end)")
    parser.add_argument("--onset", type=float, help="override the detected onset (seconds from file start)")
    parser.add_argument("--json", metavar="PATH", help="write the JSON result here instead of stdout")
    parser.add_argument("--markdown", action="store_true", help="print compact Markdown tables to stdout")
    parser.add_argument("--compare", metavar="MODEL_WAV", help="diff the (single) reference input against a model render")
    parser.add_argument("--compare-json", metavar="MODEL_JSON", help="diff the reference against a stored model result")
    parser.add_argument("--self-test", action="store_true", help="verify the fit on synthetic strikes and exit")
    args = parser.parse_args(argv)

    if args.self_test:
        return self_test()
    if not args.inputs:
        parser.error("give at least one WAV file (or --self-test)")
    if args.max_partials < 1:
        parser.error("--max-partials must be at least 1")
    options = {"max_partials": args.max_partials, "fmax": args.fmax, "windows": args.windows}
    if args.onset is not None:
        options["onset_s"] = args.onset

    def load(path, note=None):
        if str(path).lower().endswith(".json"):
            return load_result(path, None if note is None else resolve_note(note)["midi"])
        return analyse_file(path, note, **dict(options))

    try:
        if args.compare or args.compare_json:
            if len(args.inputs) != 1:
                parser.error("--compare/--compare-json take exactly one reference input")
            ref = load(args.inputs[0], args.note)
            note = args.note or ref["note"]["name"]
            model = (analyse_file(args.compare, note, **dict(options)) if args.compare
                     else load_result(args.compare_json, ref["note"]["midi"]))
            output = compare_results(ref, model)
            text = markdown_compare(output)
        else:
            output = [load(path, args.note) for path in args.inputs]
            text = "\n".join(markdown_result(r) for r in output)
    except (OSError, ValueError) as error:
        print(f"{TOOL}: error: {error}", file=sys.stderr)
        return 2
    payload = json.dumps(output, indent=2, ensure_ascii=False) + "\n"
    if args.json:
        Path(args.json).write_text(payload)
    if args.markdown:
        sys.stdout.write(text)
    elif not args.json:
        sys.stdout.write(payload)
    return 0


if __name__ == "__main__":
    sys.exit(main())
