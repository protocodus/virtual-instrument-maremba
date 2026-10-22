# Main phrase references, 1.5.6

The final authored bank passes all three registered native MainCalibration tests
at 44.1, 48 and 96 kHz: **3/3 passed**, 167.21 seconds total. Each rate renders
24 states in three scenarios, for 216 retained rows. The highest Main peak was
−1.00000029002 dBFS; the highest stem peak was −0.537448457691 dBFS. The independent
Main/stem sample ceilings, finite/buffer checks and normal Main safety check remain.

The five-second velocity-80 Linear phrase includes complete tails. It is a
secondary regression fingerprint. It does not establish the primary velocity-100
single/pair balance, Main gain reduction, or an actual Reason mixer VU reading.
No audio files or hosted/SDK tests were produced in this focused check.

Twenty factory RMS anchors and peak-window centers now describe the rebalanced
bank. Three-rate measurements of the earlier −25.85 primary-score candidate
established the centers; the final bank's uniform −0.10 dB trim shifts them to
−25.95. The final registered tests independently rendered that final header.
Their factory Main RMS/peak changes match the expected shift within 0.000006422 dB.
Init values, normal stem values and maximum-Volume stress results are unchanged.

All RMS tolerances remain ±0.35 dB. Peak-window widths remain 0.8 dB, except
Soft Yarn Nocturne's existing 0.6 dB width. Soft Yarn and Thumb Lullaby are now
below full VCA, so their former full-VCA exception flags are retired. The three
bar-model Init anchors remain −27 dBFS. Kalimba Init now uses the measured
−28.4658 dBFS anchor after its requested 30% shorter tine lifetime; observed
values span −28.5109639232 to −28.3911334046 dBFS across the three rates.

The unchanged old −27 dBFS Kalimba Init expectation first failed at 48 kHz with
−28.4952401661 dBFS. That [failure](before-contract-failure.log), all 72 earlier
candidate measurements, [reference centers](reference-centers.json),
[216 final renders](final-renders.csv), [passing CTest log](final-ctest.log),
[watched input verification](final-input-verification.json) and [receipt](receipt.json)
are retained. The earlier candidate also passed its three registered tests;
its subsequent driver hash assertion noticed the parent's intended header
replacement during that diagnostic run. All eight watched inputs remained
unchanged throughout the final build and tests.

The isolated native Release build has plug-ins, tools and universal builds off.
Its initial configure warned that the chosen Python lacked numpy/scipy/mpmath,
so Python generator/measurement checks were not registered. The selected C++
MainCalibration tests were registered and passed. Separate full SDK and ordinary
playing gain-reduction validation remain necessary.
