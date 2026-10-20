# Plug-in Quality state and automation compatibility

The plug-in retains the released `oversampling` parameter's ID, version hint
`1`, host position and three choices. Its normalized host values remain
`0 = 2x`, `0.5 = 4x`, `1 = 8x`, including the original JUCE rounding between
those positions. Adding 1x directly to that parameter would instead interpret
old normalized `1` as 1x, changing existing automation playback.

The plug-in appends a Boolean `nativeRate` parameter, named **Native Rate
(1x)**, with version hint `2`. When enabled, this mode selects effective Rack
Quality raw `3`; when disabled, the legacy `oversampling` choice selects raw
`0`, `1` or `2`. Native Rate intentionally takes priority over legacy quality
automation while enabled. A generic host exposes both controls. The plug-in
editor combines them into one four-choice Quality menu and changes the legacy
choice before disabling Native Rate. The audio thread reads the mode first
so leaving native mode cannot expose the previous dormant legacy choice.

New instances and all factory presets select native mode. The shared DSP,
Rack property mapping, generated display tables and factory preset bytes are
unchanged by this plug-in compatibility correction.

State restoration migrates a copy of the saved APVTS tree before replacing
the live state; it performs no migration work in the audio callback:

| Saved state | Restored mode and Quality |
| --- | --- |
| Released state without `nativeRate`, raw `0`, `1` or `2` | Native Rate off; same legacy factor |
| Interim development state without `nativeRate`, raw `3` | Native Rate on; dormant legacy raw `0` |
| Modern state containing `nativeRate` | Preserve the saved mode and valid dormant legacy choice |

The raw-0 legacy case explicitly inserts `nativeRate = false`; migration does
not depend on a parameter-change notification for a value that is already 0.
Old session automation therefore continues to act on the original three-choice
parameter after restoration. Selecting 1x later is an explicit mode change.

`MarembaPluginProcessorTests` covers legacy parameter IDs/order/version hints,
all factory programs, normalized endpoint/midpoint and quarter-position
automation against the released three-choice mapping, old raw-0/1/2 state
restoration, interim raw-3 migration, both modern modes and state round trips,
and the actual editor's composite selection and host-driven updates.

The focused native-arm64 Release build used JUCE 8.0.14, pinned commit
`2cdfca8feb300fb424002ba2c2751569e5bacb64` and archive SHA-256
`814019716960a16f8f850a1593eeb56e2f21910aff2d673f7d3513b4ebaf251d`.
`Maremba.PluginProcessor` passed **1/1 in 2.29 seconds**, including all the
cases above and the existing processor audio/state contracts.

The first focused run failed eight editor assertions because a synchronous
parameter attachment callback read APVTS's raw cache before its update. The
editor now reads the two parameters directly, whose values are updated before
listener dispatch. The failed run remains in
[attempt 1](plugin-quality-compatibility-attempt-1-2026-10-20.log); the corrected
run is retained in [the passing log](plugin-quality-compatibility-2026-10-20.log)
and [verification receipt](plugin-quality-compatibility-2026-10-20.json).

The build completed with non-fatal recommended-warning diagnostics from DSP
headers and exact floating-point comparisons, plus JUCE's notice that the
obsolete splash-screen flag is ignored. Python measurement dependencies were
absent from this isolated configuration, so those unrelated checks were not
registered or run. This focused check does not claim a full-suite, plug-in
format, hosted playback or target-chip validation.
