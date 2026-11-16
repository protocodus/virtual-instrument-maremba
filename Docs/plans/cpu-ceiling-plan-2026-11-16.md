# CPU ceiling plan, 2026-11-16

Standard U12 asks every product to keep each 256-frame block at 48 kHz under
10% of one core on the CI runner at its default settings, and a product above
it to keep a Known gap and this plan.

## Where Maremba stands

CI run 43703023602 (`Maremba.CpuBudget`, ubuntu-24.04, 1x oversampling, three
timed renders after a warm-up):

| Workload | Real-time factor | Worst block |
| --- | --- | --- |
| README demos (8 presets) | 0.090-0.111 | 12.1-15.3% |
| Stress phrase through each demo's preset | 0.090-0.135 | 21.1-27.2% |

The stress phrase (sixteen voices under the pedal, re-struck) roughly doubles
the worst block while the mean rises far less, so the peak comes from voice
starts: strike excitation, new modal banks and the sympathetic halo all
starting in the same block.

## Plan

1. Profile the stress phrase's worst blocks by stage (strike, bar modal bank,
   resonator, sympathetic halo, Close/Room/Piezo paths, limiter) with
   `RenderMarembaPresetDemos --cpu` and a sampling profiler; record the split
   here.
2. Spread voice-start work: precompute strike tables per hardness and position,
   and initialise new voices' banks without per-sample setup cost, checking each
   change bit for bit or by null test against the README demos.
3. Extend the 1.6.5 vector kernels to the remaining scalar per-voice loops.
4. Re-measure on CI after each step; close this plan and the Known gap when the
   worst block stays under 10%.
