#!/usr/bin/env python3
"""Synthesize Close's head-related filters for Maremba's declared capture scene.

The listener's head is a rigid sphere with its ears on the interaural axis, as
CaptureGeometry.h declares it (head centre CloseHeadCentre(), ears CloseEar()).
Each source's pressure at each ear is the exact series solution for a point
source at the source's own distance from the sphere (Duda and Martens, "Range
dependence of the response of a spherical head model", JASA 104(5), 1998,
eq. 4), relative to the free field at the head centre. Nothing is measured or
recorded: the only inputs are the scene geometry and the speed of sound.

Like any diffuse-field-equalized binaural capture, each ear is then divided by
its own diffuse-field response (the sphere's power average over directions at
the reference range), so the head shapes direction rather than overall tone.
Magnitudes are minimum-phase reconstructed; the interaural excess-phase slope
supplies the residual delay separately, as the renderer expects.

Requires numpy and scipy only at generation time.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
DSP = ROOT / "Source/DSP"
FFT = 8192
TAPS = 160
RATE = 48000
# CaptureGeometry.h: kCaptureSpeedOfSound, and the ears at x = +-0.09 m from
# the head centre, which makes the sphere's radius.
SPEED_OF_SOUND = 343.0
HEAD_RADIUS = 0.09
# The diffuse-field average is taken at 1.2 m, the range the previous
# measured set was equalized at, so the overall tone stays comparable.
DIFFUSE_RANGE = 1.2
DIFFUSE_DIRECTIONS = 2048
BAND_EDGE_START = 20000.0
# Close's level calibration (MainCalibration.h, the factory patches' Volumes)
# was pinned with the measured KU100 set this replaced, whose overall level
# sat under the free field. One broadband trim keeps it: at -3.64 dB the four
# Init anchors of MainCalibrationTest (velocity-80 Main RMS, one per Model)
# keep their mean, the bars 0.1-0.25 dB up and the Kalimba 0.3 dB down.
CALIBRATION_GAIN_DB = -3.64


def sha(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


def geometry():
    with tempfile.TemporaryDirectory(prefix="maremba-hrtf-geometry-") as temporary:
        path = pathlib.Path(temporary)
        source = path / "geometry.cpp"
        source.write_text(r'''#include "CaptureGeometry.h"
#include <cstdio>
int main() {
    auto h=maremba::CloseHeadCentre();
    std::printf("%.17g %.17g %.17g\n", h.x,h.y,h.z);
    for(int c=0;c<2;++c){auto e=maremba::CloseEar(c);std::printf("%.17g %.17g %.17g\n",e.x,e.y,e.z);}
    std::printf("%.17g 0 0\n", maremba::kCaptureSpeedOfSound);
    for(int i=0;i<maremba::kCaptureSourceCount;++i) {
        auto p=maremba::CaptureSourcePosition(i);
        std::printf("%.17g %.17g %.17g\n",p.x,p.y,p.z);
    }
}
''')
        compiler = "clang++" if subprocess.run(["which", "clang++"], capture_output=True).returncode == 0 else "g++"
        subprocess.run([compiler, "-std=c++17", "-O2", "-I", str(DSP),
                        str(source), "-o", str(path / "geometry")], check=True)
        rows = np.array([[float(x) for x in line.split()] for line in
                         subprocess.check_output([str(path / "geometry")], text=True).splitlines()])
    return rows[0], rows[1:3], rows[3, 0], rows[4:]


def sphere_response(rho, cosines, frequency):
    """Duda-Martens eq. 4: pressure on a rigid sphere at angle acos(cosine)
    from a point source at rho radii, over the free-field pressure at the
    centre. numpy's sign convention (a delay tau multiplies by e^{-i w tau})."""
    mu = 2 * np.pi * frequency * HEAD_RADIUS / SPEED_OF_SOUND
    mu = np.maximum(mu, 1e-6)
    cosines = np.atleast_1d(cosines)
    total = np.zeros((cosines.size, mu.size), complex)
    # Past m ~ mu the derivative h'_m(mu) grows factorially and each term
    # falls away; stop once a term no longer moves the sum. A ratio whose two
    # Hankel values both overflow is one of those vanishing terms.
    legendre_previous = np.ones_like(cosines)
    legendre = cosines.copy()
    with np.errstate(all="ignore"):
        for m in range(0, 400):
            if m == 0:
                p = legendre_previous
            elif m == 1:
                p = legendre
            else:
                legendre_previous, legendre = legendre, ((2*m - 1) * cosines * legendre
                                                         - (m - 1) * legendre_previous) / m
                p = legendre
            h = scipy.special.spherical_jn(m, mu*rho) + 1j*scipy.special.spherical_yn(m, mu*rho)
            dh = (scipy.special.spherical_jn(m, mu, derivative=True)
                  + 1j*scipy.special.spherical_yn(m, mu, derivative=True))
            ratio = h / dh
            ratio[~np.isfinite(ratio)] = 0.0
            term = (2*m + 1) * p[:, None] * ratio[None, :]
            total += term
            if m > np.max(mu) + 10 and \
               np.max(np.abs(term) / np.maximum(np.abs(total), 1e-30)) < 1e-12:
                break
        response = -(rho / mu) * np.exp(-1j * mu * rho) * total
    # The series is written for e^{-i w t}; numpy's FFT uses e^{+i w t}.
    return np.conj(response)


def fibonacci_sphere(count):
    index = np.arange(count) + 0.5
    z = 1 - 2 * index / count
    angle = np.pi * (1 + 5 ** 0.5) * index
    r = np.sqrt(1 - z * z)
    return np.column_stack((r * np.cos(angle), r * np.sin(angle), z))


def minimum_phase(log_magnitude):
    cepstrum = np.fft.irfft(log_magnitude, n=FFT, axis=-1)
    cepstrum[..., 1:FFT // 2] *= 2
    cepstrum[..., FFT // 2 + 1:] = 0
    spectrum = np.exp(np.fft.rfft(cepstrum, axis=-1))
    return np.fft.irfft(spectrum, n=FFT, axis=-1), spectrum


def emit_header(path, kernels, residual, indices, geometry_hash):
    lines = ["// Generated by Tests/generate_close_hrtf.py; do not edit.",
             "// Synthesized, not measured: a rigid spherical head (Duda and Martens 1998)",
             f"// of radius {HEAD_RADIUS} m with the scene's ears, each source at its own range,",
             f"// diffuse-field equalized per ear, trimmed {CALIBRATION_GAIN_DB} dB. No recorded or measured response is used.",
             "// Minimum-phase per ear; 160 taps; residual interaural delay separate.",
             f"// CaptureGeometry SHA256: {geometry_hash}",
             "#pragma once", "#include <cstdint>", "namespace maremba {",
             f"constexpr int kCloseHrtfReferenceRate = {RATE};",
             f"constexpr int kCloseHrtfTaps = {TAPS};",
             f"constexpr int kCloseHrtfProfileCount = {len(kernels)};",
             f"constexpr int kCloseHrtfSourceCount = {len(indices)};",
             "struct CloseHrtfProfile { float residualItdSeconds; float impulse[2][kCloseHrtfTaps]; };",
             "static constexpr CloseHrtfProfile kCloseHrtfProfiles[kCloseHrtfProfileCount] = {"]
    def f(value):
        s = format(float(np.float32(value)), ".9g")
        return s + ("f" if "." in s or "e" in s else ".0f")
    for ir, delay in zip(kernels, residual):
        lines.append("    {" + f(delay) + ", {")
        for channel in ir:
            lines.append("        {" + ", ".join(f(value) for value in channel) + "},")
        lines.append("    }},")
    lines.extend(["};", "static constexpr std::uint16_t kCloseHrtfSourceProfile[kCloseHrtfSourceCount] = {"])
    for i in range(0, len(indices), 16):
        lines.append("    " + ", ".join(str(value) for value in indices[i:i+16]) + ",")
    lines.extend(["};", "} // namespace maremba", ""])
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text("\n".join(lines))
    temporary.replace(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="check frozen hashes without scientific dependencies")
    parser.add_argument("--header", type=pathlib.Path, default=DSP / "CloseHrtf.h")
    parser.add_argument("--receipt", type=pathlib.Path,
                        default=ROOT / "Tests/close-hrtf-generation.json")
    args = parser.parse_args()
    if args.check:
        receipt = json.loads(args.receipt.read_text())
        assert receipt["header_sha256"] == sha(args.header), "generated header differs from receipt"
        assert receipt["generator_sha256"] == sha(__file__), "generator changed; regenerate assets"
        for name, digest in receipt["geometry_dependency_sha256"].items():
            assert sha(DSP / name) == digest, f"geometry dependency changed: {name}"
        print("Synthesized Close HRTF hashes and geometry dependencies match")
        return
    global np, scipy
    import numpy as np
    import scipy
    import scipy.special
    head, ears, speed, points = geometry()
    if abs(speed - SPEED_OF_SOUND) > 1e-12:
        raise SystemExit("kCaptureSpeedOfSound changed; update SPEED_OF_SOUND")
    ear_axis = []
    for ear in ears:
        offset = ear - head
        if abs(np.linalg.norm(offset) - HEAD_RADIUS) > 1e-12:
            raise SystemExit("CloseEar no longer sits HEAD_RADIUS from the head centre")
        ear_axis.append(offset / np.linalg.norm(offset))
    ear_axis = np.array(ear_axis)
    frequency = np.fft.rfftfreq(FFT, 1 / RATE)

    # Each ear's diffuse-field power response: the sphere's power average
    # over uniformly spread directions at DIFFUSE_RANGE. The sphere is
    # symmetric about its ear axis, so one ear's average serves both.
    directions = fibonacci_sphere(DIFFUSE_DIRECTIONS)
    diffuse = np.mean(np.abs(sphere_response(DIFFUSE_RANGE / HEAD_RADIUS,
                                             directions @ ear_axis[0], frequency)) ** 2, axis=0)
    diffuse_gain = 10 ** (CALIBRATION_GAIN_DB / 20) / np.sqrt(diffuse)

    # The table cannot hold the head above its 24 kHz Nyquist, and a sphere is
    # still loud there, so its edge would be a brick wall that rings once the
    # renderer resamples the kernels for a 96 kHz host. Fade the response out
    # over 20-24 kHz, the band above hearing, to a -60 dB floor instead.
    fade = np.clip((frequency - BAND_EDGE_START) / (RATE / 2 - BAND_EDGE_START), 0, 1)
    band_edge = np.maximum(0.5 * (1 + np.cos(np.pi * fade)), 1e-3)

    point_lookup = {}
    indices, kernels, delays, records = [], [], [], []
    all_error, max_error = [], 0.0
    band = (frequency >= 100) & (frequency <= 18000)
    for point in points:
        key = tuple(point.tolist())
        if key in point_lookup:
            indices.append(point_lookup[key]); continue
        profile = len(kernels)
        point_lookup[key] = profile
        indices.append(profile)
        offset = point - head
        distance = float(np.linalg.norm(offset))
        if distance <= 1.05 * HEAD_RADIUS:
            raise SystemExit(f"source {profile} lies at the head")
        direction = offset / distance
        spectrum = np.stack([sphere_response(distance / HEAD_RADIUS, np.array([direction @ axis]),
                                             frequency)[0] * diffuse_gain for axis in ear_axis])
        spectrum = spectrum * band_edge[None, :]
        logmag = np.log(np.maximum(np.abs(spectrum), 1e-9))
        impulse, minimum = minimum_phase(logmag)
        # The interaural excess-phase slope, as the measured set was fitted:
        # the common part is the head-centre flight the renderer already adds.
        phase = np.unwrap(np.angle(spectrum[1] * minimum[0] / (spectrum[0] * minimum[1])))
        fit_band = (frequency >= 200) & (frequency <= 2000)
        omega = 2 * np.pi * frequency[fit_band] / RATE
        weight = np.sqrt(np.abs(spectrum[0, fit_band] * spectrum[1, fit_band]))
        fit = np.polyfit(omega, phase[fit_band], 1, w=weight)
        delay = -float(fit[0]) / RATE
        result = impulse[:, :TAPS].copy()
        result[:, -8:] *= np.linspace(1, 0, 8)
        result = result.astype(np.float32)
        exact = np.abs(spectrum)
        error = np.abs(20 * np.log10(np.maximum(np.abs(np.fft.rfft(result, n=FFT, axis=-1)), 1e-9)
                                    / np.maximum(exact, 1e-9)))
        use = band[None, :] & (exact >= np.max(exact, axis=-1, keepdims=True) * 10**(-30/20))
        selected = error[use]
        all_error.extend(selected.tolist())
        max_error = max(max_error, float(np.max(selected)))
        kernels.append(result); delays.append(delay)
        records.append({"profile": profile, "position_m": point.tolist(), "range_m": distance,
                        "ear_angles_deg": [float(np.rad2deg(np.arccos(np.clip(direction @ a, -1, 1))))
                                           for a in ear_axis],
                        "residual_itd_seconds": delay,
                        "max_magnitude_error_db": float(np.max(selected))})
    if max_error > 0.65:
        raise RuntimeError(f"160-tap truncation error {max_error:.3f}dB exceeds validated bound")
    geometry_hash = sha(DSP / "CaptureGeometry.h")
    emit_header(args.header, kernels, delays, indices, geometry_hash)
    receipt = {"model": "rigid sphere, point source at each source's range (Duda and Martens, JASA 104(5) 1998, eq. 4)",
               "head_radius_m": HEAD_RADIUS, "speed_of_sound_m_s": SPEED_OF_SOUND,
               "diffuse_field_range_m": DIFFUSE_RANGE, "calibration_gain_db": CALIBRATION_GAIN_DB, "band_edge_fade_hz": [BAND_EDGE_START, RATE / 2], "diffuse_field_directions": DIFFUSE_DIRECTIONS,
               "generator_sha256": sha(__file__), "geometry_sha256": geometry_hash,
               "geometry_dependency_sha256": {name: sha(DSP / name) for name in
                                                ("CaptureGeometry.h", "MarimbaModel.h", "BarModes.h")},
               "generation_environment": {"python": sys.version, "numpy": np.__version__,
                                           "scipy": scipy.__version__},
               "header_sha256": sha(args.header), "source_count": len(points), "profile_count": len(kernels),
               "reference_rate": RATE, "taps": TAPS, "fft_size": FFT,
               "processing": "exact sphere series per ear over the free field at the head centre; per-ear diffuse-field equalization; raised-cosine fade 20-24 kHz to -60 dB; homomorphic minimum phase; final 8-tap linear taper; float32; residual interaural excess-phase weighted slope 200..2000 Hz",
               "magnitude_error_band": "truncation/float32 against the full synthesized response; 100..18000 Hz, bins within 30 dB of each profile ear maximum",
               "magnitude_error_db_percentiles_50_95_99_100": np.quantile(all_error, [.5,.95,.99,1]).tolist(),
               "source_profile_indices": indices, "profiles": records,
               "limits": "A sphere has no pinnae, neck or torso: no elevation cues beyond the head's own shadow. Ears on the interaural axis as the scene declares them. Extra all-pass phase discarded; the residual ITD is retained separately."}
    args.receipt.parent.mkdir(parents=True, exist_ok=True)
    args.receipt.write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({key: receipt[key] for key in ("source_count", "profile_count", "taps", "magnitude_error_db_percentiles_50_95_99_100", "header_sha256")}, indent=2))

if __name__ == "__main__":
    main()
