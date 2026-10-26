# Physical pickup reference

MAREMBA now uses a declared virtual bonded-PVDF pickup and charge amplifier. No sensor part number, measured bonding transfer, preamp schematic, or measured calibration was supplied. This is a reproducible reference design, not a replica of unidentified hardware. Acoustic Close and Room signals never enter the pickup's electrical input.

## Sensor and mechanical input

The reference film is 28 µm PVDF. It uses d31 = 23 pC/N, longitudinal Young's modulus 2 GPa, permittivity 106.4 pF/m, density 1780 kg/m³, and bulk resistivity 10¹³ Ωm. The modulus is the low end of the published typical range; resistivity uses the published lower bound. These choices yield the manufacturer's 380 pF/cm² capacitance and about 12.1 mV per microstrain open-circuit sensitivity. These are typical material assumptions, not measured tolerances for a finished pickup. [Measurement Specialties technical manual, Table 1](https://cdn.sparkfun.com/assets/d/a/a/b/b/Piezo_Film_Sensors_Technical_Manual.pdf)

| Model | Fixed electrode, mm | Capacitance, nF |
|---|---:|---:|
| Rosewood | 38 × 10 | 1.444 |
| Padauk | 52 × 10 | 1.976 |
| Balafon | 49 × 10 | 1.862 |
| Kalimba board | 25.2 × 24 | 2.29824 |

For each wooden bar, the generator integrates signed top-surface strain `−h(x)/2 × dθ/dx` over that fixed physical electrode. Here θ is the Timoshenko section rotation, not the displacement slope of a uniform-beam substitute. The integral uses the same carved thickness, FEM rotation and displacement normalization as the physical bar. All 16 modes have independent signed readouts. Notes and Carving interpolate the generated strain along with the existing geometry; the film does not grow with each key. Acoustic modal tables were preserved exactly when these new strain columns were generated.

The Kalimba has one board sensor. Its tines contribute bridge force in newtons to three shared plate modes. The declared board is 210 × 150 × 5 mm with density 600 kg/m³, giving each sine-sine mode a modal mass of 0.023625 kg. Modal frequencies and Q remain 640/1120/1900 Hz and 12/14/16. The equivalent modal stiffness is `M(ωd² + γ²)`, with `γ = ωd/(2Q)`. This is a modal reference plate, not a claim that a uniform isotropic panel predicts those three frequencies.

A declared bridge point at normalized `(0.30, 0.50)` projects force onto the modes. The electrode is centered at `(0.38, 0.36)`; its area-averaged longitudinal curvature converts modal displacement in metres into tensile strain. An exact held-force transition advances displacement and velocity. Static deflection remains in this mechanical model and is rejected by the electrical high-pass. Acoustic Bloom and the cavity's nominal audio output are not additional electrical excitation. Cavity loading can still change the tines' bridge force. Production excludes the old per-tine nominal pickup outputs; only the shared physical sensor reaches the Kalimba Piezo output.

Perfect adhesive strain transfer, negligible transverse stress, and negligible sensor loading on the instrument are assumptions. Film anisotropy beyond d31, adhesive compliance, detailed boundary-condition measurements, pyroelectric effects and electromechanical back-action are not modeled. Nominal contact/support/impact audio layers are excluded from the calibrated charge path; their existing mechanical action still reaches bending modes where implemented. Legacy nominal pickup methods remain for diagnostic compatibility and do not establish the physical production calibration.

## Charge amplifier and output units

The path is `modal displacement (m) → tensile strain → charge (C) → amplifier voltage (V) → ideal reference conversion → fixed digital calibration`.

Generated charge is `q = d31 × E × electrode_area × strain`. Sensor capacitance is `Cp = permittivity × area / thickness`; leakage resistance is `ρ × thickness / area`. The charge amplifier follows the sensor/feedback topology described in [TI SLOA033A](https://www.ti.com/lit/an/sloa033a/sloa033a.pdf). A 470 nF feedback capacitor and 68 kΩ feedback resistor set the approximate midband conversion `−q/Cf` and a 4.98 Hz recovery corner. Cable capacitance is a declared 470 pF; modeled input capacitance is 14 pF.

The opamp uses an OPA1642-derived dominant-pole model with typical DC gain 134 dB, gain-bandwidth 11 MHz, and slew rate 20 V/µs. It is operated on declared ±15 V supplies, with a conservative modeled ±14 V output swing. These choices use published typical behavior, not a transistor-level macro-model or a guaranteed device envelope. [TI OPA1642 datasheet, electrical characteristics](https://www.ti.com/lit/ds/symlink/opa1642.pdf)

Input-node and output voltage are solved together from capacitor charge balance and finite opamp gain. The linear transition is exact for linearly changing input charge during a sample, avoiding a spurious near-Nyquist pole from discretizing MHz dynamics with a coarse trapezoidal step. If a deliberately excessive stimulus reaches the modeled rail or slew limit, attainable output voltage is constrained and input charge balance determines recovery. This is an electrical overload response, not a musical output limiter.

The ±12 V reference conversion is an **ideal linear division by 12 V**, not a simulated ADC: clipping, quantization and converter noise are not implemented. Post-conversion digital gains are fixed by model: 2181.47148623013, 395.768425841642, 931.534912056703 and 1627.21817439378. The three bar gains preserve the former fully carved C4 fundamental magnitude at 88.2 kHz. The Kalimba gain preserves the former 500 ms C4 direct-pickup RMS at velocity 0.5 through a 44.1 kHz engine. None changes analog charge, rails, or input headroom. There is no per-hit normalization.

The electronics omit opamp noise, bias/offset, additional poles, input-protection diodes, output-current limiting, temperature and part tolerances. The film's published single-point dielectric loss tangent is not converted into an invented broadband loss model. In the developing 1.5.0f3 source, Drive processes the complete Main mix downstream of the pickup and capture-level coefficients; direct Piezo bypasses it. This authored musical saturation is not claimed to be OPA1642 overload. Existing output protection is separate from this pickup reference.

For Main, f3 delays each electrical source by its acoustic flight to the Close head centre before mixing. The shared Kalimba sensor uses the board position. This timing compensation preserves the pickup's physical strain, circuit transfer and polarity; the raw Piezo socket bypasses it. Close still measures airborne pressure from radiating motion and cavities, so the two captures have distinct attack and modal balance. Their different responses can retain some interference after arrival alignment. The [arrival audit](https://github.com/protocodus/reason-rack-extensions/blob/be34e920a2b05fa92b2ce38dd142373e238e4e3c/Examples/Maremba/Docs/validation/capture-flight-2026-10-08/README.md) records the measured improvement and full/default/varied-strike scope. The wooden direct-output trim separately changes from .49 to .245 for headroom; the physical pickup and its Main contribution are unchanged. The reference measurements below retain their original calibration and source scope.

## Verification and scope

The portable [evidence receipt](https://github.com/protocodus/reason-rack-extensions/blob/be34e920a2b05fa92b2ce38dd142373e238e4e3c/Examples/Maremba/Docs/validation/capture-2026-09-27/piezo/receipt.json) records the exact tests, source hashes, sizing fixtures and calibration data. The bar geometry test compares blank low-register FEM strain to an independent analytical beam, verifies interpolation and fixed electrode size, and checks the actual voice-to-circuit route. Across 509,976 production voice samples, independently rendered physical strain through the charge circuit matches the emitted bar pickup exactly at float precision.

The circuit test compares 132 responses with an independent continuous nodal solution; maximum complex relative error is 0.1343%. It separately verifies finite bandwidth, overload rails/slew, linearity, reset and tail draining. A synthetic 4.79677 µC charge budget reaches 10.0561 V without invoking the modeled rails or slew. That synthetic fixture establishes the implemented voltage/slew calculation only; it is not a claim that an actual opamp can supply every implied high-frequency output current.

The bar sizing sweep covers all 128 MIDI notes, all five strikers, Carving endpoints and four positions at velocity 127, 88.2 kHz, for 120 ms per fixture: 15,360 cases. Maximum observed charge is 64.511 nC and maximum sampled modal envelope is 99.933 nC. Multiplying that envelope by 24 gives an ideal charge-amplifier voltage of 5.103 V. This is finite-case sizing evidence, not a universal maximum over every gesture, rate, seed or parameter trajectory.

The Kalimba sizing sweep covers 2,560 cases: all notes and strikers, Carving endpoints, two extreme positions, velocity 127, 88.2 kHz and 120 ms. Maximum charge is 6.44114 nC; maximum actual modeled amplifier output is 0.01358493 V. Every fixture remains exactly linear against a half-amplitude input. The independent plate response test has maximum complex error 0.04706%; its engine test proves that injected legacy per-tine pickup state changes no production output. Circuit, plate and engine reset/tail tests pass with sanitizers.

At C4, velocity 1/127 remains quiet: direct-Piezo RMS is 6.69e−7 / 9.04e−7 / 3.88e−7 / 1.317e−5 for Rosewood/Padauk/Balafon/Kalimba in the recorded 500 ms clean fixture. This is a pickup-level check, not a claim that every patch has the same spectral brightness or loudness.
