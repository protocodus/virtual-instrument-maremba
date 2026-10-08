"""Gross temporal-artifact guards for the existing wet-only Room impulse probe.
These loose engineering limits are not hearing thresholds or a naturalness score.
Keep the separate level, spectral, decay and stereo checks.
"""
import math
import numpy as np

MAX_DENSITY_GAP_MS = 20.0
MAX_WAVEFORM_CORRELATION = .60
MAX_ENVELOPE_CORRELATION = .80


def _correlation_peak(samples, rate, minimum_ms, maximum_ms):
    samples = np.asarray(samples, dtype=float)
    samples = samples - np.mean(samples)
    if np.dot(samples, samples) < 1.e-24:
        return 0.0, None  # A constant envelope has no periodic fluctuation.
    low = max(1, round(minimum_ms * .001 * rate))
    high = min(round(maximum_ms * .001 * rate), len(samples) // 3)
    if high < low:
        raise ValueError("Temporal window cannot contain three periods")
    nfft = 1 << (2 * len(samples) - 1).bit_length()
    spectrum = np.fft.rfft(samples, nfft)
    correlation = np.fft.irfft(spectrum * np.conj(spectrum), nfft)[:high + 1]
    cumulative = np.r_[0, np.cumsum(samples * samples)]
    lags = np.arange(low, high + 1)
    denominator = np.sqrt(cumulative[len(samples) - lags] * (cumulative[-1] - cumulative[lags]))
    values = np.divide(correlation[lags], denominator, out=np.zeros(len(lags)), where=denominator > 0)
    if not np.isfinite(values).all():
        raise ValueError("Nonfinite temporal autocorrelation")
    index = int(np.argmax(values))
    return float(np.clip(values[index], -1, 1)), float(lags[index] * 1000 / rate)


def _density_gap(samples, rate, late_arrival):
    half = round(.01 * rate)
    weights = np.hanning(2 * half + 3)[1:-1]
    weights /= weights.sum()
    hop = max(1, round(.001 * rate))
    run = longest = 0
    # Exclude the diffuse onset: centered Hann support then starts at least
    # 10 ms after first arrival, never in causal pre-arrival silence.
    for center in range(round((late_arrival + .02) * rate), round((late_arrival + .18) * rate) + 1, hop):
        part = samples[center - half:center + half + 1]
        rms = math.sqrt(float(np.sum(weights * part * part)))
        density = float(np.sum(weights * (np.abs(part) > rms))) / math.erfc(1 / math.sqrt(2)) if rms else 0.0
        run = run + 1 if density < .60 else 0
        longest = max(longest, run)
    return longest * hop * 1000 / rate


def measure_temporal(audio, rate, late_arrivals):
    """Return three per-ear scalars plus diagnostic lags; reject invalid input."""
    audio = np.asarray(audio, dtype=float)
    arrivals = np.asarray(late_arrivals, dtype=float)
    if not math.isfinite(rate) or rate < 8000:
        raise ValueError("Invalid temporal sample rate")
    if audio.ndim != 2 or audio.shape[1] != 2 or not np.isfinite(audio).all():
        raise ValueError("Temporal audio must be finite stereo")
    if arrivals.shape != (2,) or not np.isfinite(arrivals).all() or np.any(arrivals < 0):
        raise ValueError("Invalid diffuse arrival times")
    minimum_frames = max(round(.6 * rate), round((float(np.max(arrivals)) + .18) * rate) + round(.01 * rate) + 1)
    if len(audio) < minimum_frames:
        raise ValueError("Temporal audio is too short for its measurement windows")
    channels = []
    for channel in range(2):
        samples = audio[:, channel]
        peak = float(np.max(np.abs(samples)))
        if peak == 0:
            raise ValueError("Silent temporal audio channel")
        samples = samples / peak  # Scale-invariant analysis; never modifies the DSP/audio file.
        part = samples[round(.15 * rate):round(.6 * rate)]
        if not np.any(part):
            raise ValueError("Silent late temporal measurement window")
        block = round(.001 * rate)
        count = len(part) // block
        energy = np.mean(part[:count * block].reshape(count, block) ** 2, axis=1)
        db = 10 * np.log10(np.maximum(energy, float(np.max(energy)) * 1.e-12))
        time = (np.arange(count) + .5) * block / rate
        slope, intercept = np.polyfit(time, db, 1)
        envelope = db - (slope * time + intercept)
        envelope_peak, envelope_lag = _correlation_peak(envelope, rate / block, 5, 120)
        # Remove a fitted exponential decay without boosting numerical silence.
        gain_db = np.clip(-slope * (np.arange(len(part)) / rate - .225), -40, 40)
        waveform_peak, waveform_lag = _correlation_peak(part * 10 ** (gain_db / 20), rate, 3, 80)
        channels.append({"low_density_run_ms": _density_gap(samples, rate, arrivals[channel]),
                         "waveform_correlation": waveform_peak, "waveform_lag_ms": waveform_lag,
                         "envelope_correlation": envelope_peak, "envelope_lag_ms": envelope_lag})
    return {"channels": channels}


def check_temporal(metrics):
    """Return failures, including malformed/nonfinite measurements, never NaN-pass."""
    if not isinstance(metrics, dict) or not isinstance(metrics.get("channels"), list) or len(metrics["channels"]) != 2:
        return ["invalid temporal measurements"]
    failures = []
    for channel, values in enumerate(metrics["channels"]):
        try:
            gap, waveform, envelope = (float(values[k]) for k in
                                      ("low_density_run_ms", "waveform_correlation", "envelope_correlation"))
            lags = [values[k] for k in ("waveform_lag_ms", "envelope_lag_ms")]
            valid = (all(math.isfinite(x) for x in (gap, waveform, envelope)) and gap >= 0
                     and -1 <= waveform <= 1 and -1 <= envelope <= 1
                     and all(x is None or (math.isfinite(x) and x > 0) for x in lags))
        except (KeyError, TypeError, ValueError, OverflowError):
            valid = False
        if not valid:
            failures.append(f"channel{channel}: invalid temporal measurements")
            continue
        if gap > MAX_DENSITY_GAP_MS:
            failures.append(f"channel{channel}: sustained late-relative density deficit")
        if waveform > MAX_WAVEFORM_CORRELATION:
            failures.append(f"channel{channel}: strongly repeated late waveform packet")
        if envelope > MAX_ENVELOPE_CORRELATION:
            failures.append(f"channel{channel}: strongly periodic late energy envelope")
    return failures
