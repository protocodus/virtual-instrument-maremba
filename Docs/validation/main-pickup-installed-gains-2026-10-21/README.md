# Main pickup installed-gain reference

`Maremba.MainPickupAlignment` passes **1/1 in 2.51 s** after updating only its
independent four-value expected Main gain array. Production DSP and all numeric
tolerances remain unchanged.

Attempt 4 passed `CloseEngineArrivalTest` (90 cases, 3,727,656 checks), then failed
the following Main pickup alignment fixture. That fixture still expected gains
1.225115902, 1.174502045, 1.406057112 and 1.034682015. The deliberately restored
installed 1.5.5 gains are 2.156637999, 2.113276235, 2.805121587 and 2.111566469.
The stale oracle therefore predicted a large scalar mismatch despite unchanged
routing. The receipt labels calculated ratios separately from observed results.

The fixture retains an explicit independent declared array rather than reading
the production constant. Its 1% magnitude and 0.015 rad phase guards remain.
Physical flight, quiet bounds, exact raw-direct isolation, mute, travelling keys,
slot reuse, model transitions, reset and batch-split replay checks also remain.
Exact and abbreviated searches found no other active test with the old gain array.

The [failure](before-failure-excerpt.log), [failed process receipt](failed-build-process.json),
[focused result](focused-ctest.log), [full output](focused-ctest-detail.log) and
[hash/command receipt](receipt.json) retain the evidence. The existing native
build produced no source archive or audio. All watched DSP files and the edited
fixture stayed unchanged during the focused run. Full SDK preflight is still required.
