# Piezo capture and main-output level — historical f2 update

The subsequent [headroom and capture update](headroom-and-capture.md) changes
Drive routing, Room coefficients and final output protection. The measurements
below describe the earlier f2 implementation, not the current working tree.

This source update makes the pickup's mechanical and electrical response more
explicit and raises Main L/R without increasing Drive. It follows the packaged
**1.4.0f1** candidate and is included in the rebuilt **1.4.0f2** U45; see the
[build evidence](https://github.com/protocodus/reason-rack-extensions/blob/230f07681f56c3797626910a3f8556f68cfcc725/Examples/Maremba/Docs/release/release-evidence-1.4.0f2.md). The installed plugin,
Recon session and complete f1 distribution bundle remain unchanged. Existing
parameter identities, normalized factory
preset values, velocity response and the direct-output trim are retained;
existing patches intentionally sound different through the updated paths.

## Contact pickup and close microphones

Close captures the instrument's acoustic radiation. Piezo samples signed local
bending strain, transmitted contact force and mounting motion. Its existing
rail/bridge resonances, bonded-patch resonance and adhesive bandwidth remain in
the signal path. No new noise or nonlinear distortion is introduced by this
update. The difference comes from what moves beneath the sensor and how its
electrical load responds. Depending on the note and the strain nodes, Piezo
can emphasize or suppress an overtone; it is not necessarily brighter on every
key.

The wooden-bar and tine pickups retain their existing mechanical readouts. The
shared Kalimba box now contributes a separate local plate-strain readout. A
voiced rectangular-plate approximation assumes a 21 × 15 cm surface, a sensor
center at fractional coordinates (0.38, 0.36), and a 0.12 × 0.16 fractional
footprint. Spatially averaged curvature of modes (1,1), (2,1) and (3,1),
normalized to the first mode, gives weights **1, 1.456 and −1.616**. The negative
weight preserves bending polarity. The box's air mode contributes a mechanical
weight of 0.35 because air pressure can move the plate; microphone and room
signals are not fed to this pickup.

That plate readout now passes through an engine-owned `PiezoPickup` instead of
joining the Piezo bus as unprocessed plate motion. Its mounting resonances and
stored charge continue to decay after the last tine or box sample. Reset, rate
configuration and subnormal-state flushing include this shared pickup. The
geometry and modal weights are voiced assumptions, not a measured kalimba or
an independently solved plate model.

## Electrical equivalent circuit

`PiezoInputCircuit` represents generated charge shared by sensor capacitance
`Cp`, cable capacitance `Cc` and amplifier input capacitance `Cin`. Finite input
and sensor leakage resistances discharge it. A non-inverting buffer supplies
resistor-set gain, followed by an output RC bandwidth limit. TI's
[Signal Conditioning Piezoelectric Sensors](https://www.ti.com/lit/an/sloa033a/sloa033a.pdf)
describes this voltage-mode sensor/load topology and the dependence on cable
capacitance. Analog Devices' [CN0350 circuit note](https://www.analog.com/en/resources/reference-designs/circuits-from-the-lab/cn0350.html)
provides a related charge-amplifier example of finite leakage, gain and
bandwidth. These establish the electrical principles; Maremba does not clone
either reference circuit.

For the open-circuit sensor voltage used by the DSP:

```text
Ctotal = Cp + Cc + Cin
Rload  = Rin || Rleak
G      = (Cp / Ctotal) × (1 + Rfeedback / Rgain)
H(s)   = G × [s Rload Ctotal / (1 + s Rload Ctotal)]
           × [1 / (1 + s Rbandwidth Cbandwidth)]
```

The implementation uses trapezoidal discretization of these time constants.
Model-specific assumptions are:

| Instrument | Cp | Rin | Rfeedback | Input high-pass corner |
| --- | ---: | ---: | ---: | ---: |
| Rosewood | 3.3 nF | 560 kΩ | 1.60 kΩ | 74.6 Hz |
| Padauk | 2.2 nF | 590 kΩ | 2.40 kΩ | 99.4 Hz |
| Balafon | 1.8 nF | 549 kΩ | 2.94 kΩ | 125.1 Hz |
| Kalimba | 4.7 nF | 634 kΩ | 1.13 kΩ | 48.3 Hz |

All profiles use `Cc = 470 pF`, `Cin = 60 pF`, `Rleak = 100 MΩ` and
`Rgain = 10 kΩ`. The output network is 10 kΩ with 820 pF, approximately
19.4 kHz in the continuous-time equivalent. The resistor gain compensates the
capacitive divider to within 0.1% of unity; this is a gain-factor statement,
not a claim that the full frequency response is flat. The earlier mechanical
adhesive filter remains separate. Component values and mounting responses are
voiced choices, not measurements of a named commercial pickup or preamp.

## Main-output calibration

Main L/R now receives a clean **2× gain (+6.02 dB)** after Drive/Warmth and
before the existing analog-modelled limiter. Quiet signals receive the full
increase; the limiter can reduce it on stronger attacks. Because the gain
follows the preamp, it does not drive that preamp into additional saturation.
Physical strike dynamics and the squared master-Volume response are unchanged.
The active limiter's final sample-peak ceiling remains **0.98**.

At 48 kHz, the factory reference phrase gained **1.5–6.0 dB**, depending on
the preset's limiting. Median phrase RMS moved from −20.55 to −18.55 dBFS.
Kalimba Thumb Lullaby, previously the quietest, rose from −25.3 to −19.3 dBFS;
the bank's new quietest phrase is Equipentatonic Log Keys at −21.0 dBFS.
These are phrase measurements, not a guarantee of the same increase for
every note or chord.

The five direct stems bypass this gain and retain their existing 0.65 mix scale,
0.06 trim (−24.4 dB) and master Volume. The baseline factory forte-chord test
already approached −3.8 dBFS at a direct output; adding 6 dB globally would
spend more headroom than that example has. Piezo stem tone changes with the
sensor model, while its output trim remains unchanged. (That trim and the
master Volume on the direct stems were replaced in 1.4.0f5; see
[the output-level report](output-level.md).)

## Validation scope

`PiezoPickupTest` compares the circuit with an independent nodal-admittance
calculation, checks electrical linearity and DC rejection, and exercises
loading, rates, reset and tails. `PiezoEngineTest` compares Close and Piezo
through the complete engine, including the shared plate, and checks capture
isolation, block-size independence, reset and pickup-tail lifetime.

The initial 36 engine comparisons across four instruments, three notes and
three rate/quality combinations had a minimum **8.02% gain-fit residual
energy** between Close and Piezo. This rejects a mere gain-scaled copy; it is
not a realism rating or a listening-test result. The broader audio, limiter,
factory-preset and timing results are recorded separately in the
[validation receipt](https://github.com/protocodus/reason-rack-extensions/blob/230f07681f56c3797626910a3f8556f68cfcc725/Examples/Maremba/Docs/validation/piezo-level-2026-09-26.json). Native tests do not
establish an interactive Recon audition or Reason host/target performance.
