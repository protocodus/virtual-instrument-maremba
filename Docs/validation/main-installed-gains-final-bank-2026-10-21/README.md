# Installed-gain final-bank phrase references

The updated registered MainCalibration checks pass **3/3 in 175.95 s** at 44.1,
48 and 96 kHz. They render 24 states in three scenarios, giving 216 rows.
A separate prior 216-row measurement supplied the frozen references; the actual
gated run reproduced every measured CSV field exactly. Measurement mode bypassed
reference-level gates and was not treated as a gated pass. Both phases retained
unchanged source, bank and test inputs within their respective phases.

The restored installed 1.5.5 Main model gains are 2.156637999, 2.113276235,
2.805121587 and 2.111566469. Kalimba retains its approved 0.7 modal lifetime.
With those actual gains, the four fresh Init RMS centers are:

| Model | Init RMS reference, dBFS |
| --- | ---: |
| Imperial Rosewood | −22.0903 |
| Mayan Padauk | −21.8999 |
| Balafon | −20.9930 |
| Kalimba | −22.2698 |

All 20 factory RMS centers use the three-rate measured mean. Peak-window centers
use the midpoint of the measured extrema. Rounding is to four decimal places.
The original ±0.35 dB RMS tolerance and every original peak-window width remain:
0.8 dB except Soft Yarn's existing 0.6 dB. Canonical and Rack tables match exactly;
the Rack patch validator's 20 VCA anchors match the final XML values.

The maximum Main peak across normal/stress captures is −1.00000029002 dBFS.
Factory velocity-80 phrase peaks span −11.23995412 to −1.00068112 dBFS. The independent
Main and stem sample ceilings, finite/buffer/silence checks and additional normal
Main safety check remain unchanged. Recentered phrase fingerprints do not assert
low gain reduction: normal velocity-100 single/pair gentle limits and legacy
scenario finite/ceiling/GR reporting remain separate and unchanged.

This five-second, velocity-80 Linear phrase includes full tails without
normalization. It is a secondary fingerprint. The final bank's separate primary
maximum-300 ms RMS policy is −22.2 dBFS, with the explicit Direct Piezo −22.6 dBFS
headroom target. Neither this test nor that proxy is an actual Reason VU reading.

The bank agent explicitly released both Rack files after its final 126 observed
capture/source-hash verification passed. Its final generated header remains
`30cdb593c4763cdf250f63bf2dc72499bf2fe51e356f96b8e539ba4fe01b5c85`.
No preset/header, DSP, gain, dynamic policy or safety-bound edits were made here.
Rack patch compliance and strict syntax compilation passed. Rack stop, allocation,
stereo and output contracts remain unchanged; their runtime checks await the full
SDK preflight. The Rack phrase retains its existing 20-tail-frame truncation at
44.1 kHz; its unchanged tolerance covers that small native/Rack difference.

[Raw renders](renders.csv), [frozen references](frozen-references.json),
[measurement commands](measurement-runs.json), [passing gated log](gated-ctest.log),
[full gated output](gated-ctest-detail.log), [input verification](gated-input-verification.json),
[Rack audit](rack-fixture-audit.json) and the [receipt](receipt.json) retain commands,
hashes and limits. Independent table review found no material issues. A fresh full
SDK preflight and delivery verification remain required.
