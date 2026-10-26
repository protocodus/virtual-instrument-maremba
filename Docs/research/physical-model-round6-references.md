# Sixth-pass research notes

Research reviewed on 2026-09-25 for the [sixth physical-model pass](physical-models-round6.md),
in addition to the [fifth pass's sources](physical-model-round5-references.md).
Each row says what a source establishes and how the engine uses it. "Derived"
marks a number computed from a source rather than printed in it; "abstract"
and "snippet" mark sources of which only that much could be read. Nothing here
is a recording of the four instruments, and a green contract is not a
listening test.

## Findings used

| Mechanism | What the sources support | Consequence in the engine |
| --- | --- | --- |
| Tube Q | A 25 mm tube's first four modes: damping ratios 0.73 / 0.77 / 0.83 / 0.94 % at 174 / 508 / 845 / 1206 Hz (Q 68 / 65 / 60 / 53); a radiation-only model gives 0.10 % at 174 Hz, the rest is viscothermal [T1]. These are one tube's modes, not tubes tuned to those notes. A 5-octave bank's bores: 98 mm at C2, 54 mm through C4–C♯5, 48 mm from D5 to C7 [T2]. Derived: 1/Q = 1/Q_wall + 1/Q_rad with Q_wall = 310 a √f and Q_rad = π/(ka)² reproduces the measured 174 Hz point at 0.8 × theory. | `ResonatorQ`: that law on the bank's bores, × 0.8: Q 87 at C2, 79 at C3, 63 at C4, 32 at C5, 10 at C6, 3 at C7. The top-octave caps act "more like reflectors than resonators" [T3]. |
| Tube decay share | With its tube the C2 fundamental falls from 6.8 to 3.7 s (0.54) [R1 of pass 5]. With strong coupling both hybrid modes decay at about the mean of the bar's and tube's rates; the regime changes where σ² ≈ (ζr + ζn)² [T1]. | Unchanged coupling (0.0084 at Coupling 0.7): past critical from C2 to C4. C2 keeps 0.56 of its bare decay (3.85 against 6.86 s). |
| Bloom | Reference notes' bloom rises: G3 8.8 dB, C4 5.7, E4 3.8, G4 3.2 dB (pass 5's soundfont fits). | With the new Q: G3 +8.9, C4 +6.3, E4 +4.3, G4 +2.6 dB (fitted by `Demos/fit_modal_reference.py`); the bass bloom peaks later than the references (G3 149 against 104 ms). |
| Tube tuning | "Slightly above" the bar, the offset growing up the range; a top-octave tube "may sound more than a half step high"; checked by blowing into it with the bars in place; "sharpen for volume, flatten for length" [T3]. Yamaha and La Favre set each stopper for the loudest resonance under its bar [T2, T4]. Derived from La Favre's air columns: the bar lowers its tube by 18 cents in the bass to 95 in the top octave (0.80 a end correction against 0.61 a open) [T2]; the bar over the mouth lowers a tube by up to 2 % [T1]. | Tubes sit +3 cents above their bars at C2, +8 through C4–C5, rising to +100 at C7 (was −1 / −9 / −33). |
| Muffling hand | Burritt muffling a low E (≈82 Hz) with his free hand: the decay slope 4–5× the undamped one; D4 and G5 barely differ [H1]. Palm and finger tissue under grip: 17–73 N s/m [H2]; a light fingertip: 1–2 kN/m, 0.75–2.4 N s/m [H3, abstract]; fingertip force transients take 5 ms or more, restitution ≈ 0.44 (derived) [H4, abstract]. "Lay the flat thumb flesh on the tine quickly and it will stop sounding" [H5]. | Damp note-off and transport stop: a hand across the bar's quarter-to-middle span, 24 N s/m, taking each mode by its mean square under the hand; the tube under it down to Q 35. A muffled rosewood E2 decays 5.0× faster (65 against 13 dB/s). The palm lands softly (20 g at 0.1 m/s over 3 ms). On a tine: a thumb laid flat, a one-sided 3 kN/m, 2.4 N s/m contact pressing 1 N, landing over 6 ms: 56 dB down 30 ms later. |
| Dead stroke | The mallet "strikes the bar and remains on the bar to dampen the sound"; "hold it firmly into the bar" [D1, D2]. No hold force or pressed-mallet decay is published. Derived: a pressed Hunt–Crossley head damps with c = λF [H6]. | Note Off: Dead. The head stays pressed with 5 N and a 4 N s/m grip while the key is held, carried by the supports; the hand eases off over 15 ms at the key's release. A rosewood C4 is 36 dB down at 0.4 s. |
| Bow | A double-bass bow on the bar's end; bowed spectra are harmonic whatever the bar's inharmonicity; bowing mid-bar locks to mode 2; the least force that speaks rises with speed; the level follows speed, not force; the pitch moves less than 10 cents; onset times fall with force (8.5 s at 3.5 N, 1 s at 14–20 N on an aluminium bar) [B1]. Rosin: μ = 0.35 + 0.40 e^(−v/0.01) + 0.45 e^(−v/0.1) [B2]. "Stopping the bow while it stays on the bar stops the note" [D3]. | Striker: Bow. A friction contact on the bar's end (Position's upper half moves it toward the quarter point), 0.05–0.25 m/s from velocity, 2–20 N from Hardness, Smith and Woodhouse's rosin law solved every sample; the mod wheel draws faster and presses harder. A rosewood C4 speaks on its pitch with a harmonic spectrum; the note decays once the bow leaves (in Dead the bow stops on the bar). |
| Shaft | Col legno: "the shoulder of the mallets on the edge of the bars" [D2, D4]. Rattan and birch handles of 8–9.5 mm; derived crossed-cylinder Hertz K 5 × 10⁷ – 8 × 10⁸ N/m^1.5 (2 × 10⁸ typical), 27 g effective at the shoulder, 0.2–0.6 ms contact; handle bending modes at 95–1080 Hz [S1, S2, S3]. A birch handle adds audible noise to the attack [S4]. | Striker: Shaft. That contact on the bar's edge (it twists the bar), Hardness from rattan to birch, the handle's three bending modes knocked by the contact. 0.31 ms on a rosewood C4. On the kalimba the same step is a fingernail tap. |
| Membrane tension | A slack membrane's nonlinear threshold grows as T^1.5 / (R √(Eh)); its buzz sits near its own resonance f01 ∝ √T [M1, M2]. | Tension spans half an octave of formant either side of the voicing, with the threshold as the formant ratio cubed. |
| Gourd | A gyil gourd "not precisely tuned" to its key; with the gourd attached one key shows peaks at 0.75 f0 (−10.7 dB) and at 2–8 f0 [G1; digitised by the research]. The 1.2 and 1.8 kHz components are harmonics 5 and 7 of that note (derived). | Unchanged gourds. The 0.75 f0 line is **not** modelled: a gourd tuned a fourth below its key (the research's hypothesis) produced it only 77 dB down and took the key's buzz with it, while the measured key buzzes strongly. |
| Kalimba hole | A disk of radius r at height h over a hole of radius b adds end correction E_d = 0.61 (r/b)^0.18 (b/h)^0.39 once that exceeds 0.65 [K1]. Derived: a palm 5 mm over a 3 cm hole lowers a 0.8 L box about 230 cents, at 20 mm about 40 [K1, K2]. | `KalimbaHoleCover`: the wheel lowers a palm from 40 to 1 mm over the front hole (two thirds of the box's conductance): −54 cents at a quarter, −150 at half, −750 closed. |
| Guatemalan bars | "Customarily traditional builders tuned the keyboard only in its fundamental note" [C1]; three-harmonic tuning marks the concert-style Chiapas marimbas that also drop the buzzing membrane [C2]. Makers thin the centre to lower and the ends to raise the pitch [C3, C4]. No measured overtone ratio of a traditional bar was found. | Hormigo bars carved to their fundamental only (a moderate central arch): the first overtone falls from 4.0 in the bass to 3.54 at C4, 3.26 at C5 and 2.9 at C6. |
| Carving | Uniform bars: overtones at 2.76 and 5.40 × f1. | Carving 0 is each key left uncarved (the same length and width, thinned evenly to the note); the ratios move geometrically to the key as built. |
| Balafon keys | Keys cut to a length progression (58 cm at G♯2 to 43 cm at D♯5) [pass 5, B1]. | The generator holds each key within 10 % of that law and carves it as close to its own overtone target as that allows (2.55–3.58 × f1 across C2–C7). |
| Mandinka balafon | Three 18-key instruments: steps 172 cents on average, SD 5–9 [N1, via N2]. | Bala 7-equal keys keep their own 6-cent (SD) deviation. |
| Chopi timbila | Five orchestras: "as a rule their octaves are true and exact"; the maker copies the tuned octave [N3]. | Timbila unchanged (the Zavala orchestra, exact octaves). |
| Amadinda | Measured keys "often show discrepancies of anything from a few cents to more than 60 cents" from equal 240-cent steps, and musicians accept 80 [N4, N5]. | Pentatonic 5-equal keys keep their own 25-cent (SD) deviation. The Senufo balafon is equipentatonic too; no per-key data exist [N6]. |
| Gyil | Godsey's averaged Birifor kogyil: 102.75 … 611.92 Hz, 238-cent steps, octaves 1169–1205 [N7, via Hogan]; individual instruments stray much further (steps 100–320 cents, SD 35) [N8]. | Scale: Gyil, the fourteen measured keys on C D E G A from C3, each with a further 30-cent (SD) deviation of an individual instrument. |
| Sena valimba | Twenty-one pitched keys measured with forks: 128 … 912 Hz, 170-cent steps, uneven octaves [N9]. | Scale: Valimba, those keys on the white keys from C3. |
| Scala files, MIDI tuning | A Rack Extension has no file access (only audio samples through the host) and receives no SysEx; its MIDI implementation is the CC chart [E1]. | No Scala import is possible. The measured keyboards are compiled in. |

## References

Tubes and hands:

- **T1** F. Soares, V. Debut and J. Antunes (2023), "The bar-resonator interaction in mallet percussion instruments", *J. Sound Vib.* 548, 117528. <https://hal.science/hal-04240665>
- **T2** J. La Favre, marimba bar and resonator data. <http://www.lafavre.us/marimba-bars.htm>
- **T3** L. H. Stevens, "The Acoustics of Resonators", Malletech. <https://malletech.com/wp-content/uploads/2019/01/Acoustics_of_Resonators1-4.pdf>
- **T4** Yamaha Musical Instrument Guide, marimba manufacturing and maintenance. <https://www.yamaha.com/en/musical_instrument_guide/marimba/manufacturing/manufacturing005.html>
- **H1** M. Schutz and S. Lipscomb (2007), "Hearing gestures, seeing music", *Perception* 36, 888 (Fig. 2, read from the plot).
- **H2** R. G. Dong et al. (2013), *J. Sound Vib.*, doi:10.1016/j.jsv.2012.10.003.
- **H3** M. Wiertlewski and V. Hayward (2012), *J. Biomech.* 45 (abstract).
- **H4** D. L. Jindrich et al. (2003), *J. Biomech.* 36, 497 (abstract).
- **H5** Kalimba Magic, "Stopping the tines". <https://www.kalimbamagic.com/blog/item/tips-stopping-the-tines>
- **H6** F. Avanzini and D. Rocchesso (2001), "Modeling collision sounds: non-linear contact force", DAFx-01.

Playing techniques:

- **D1** G. Ford, *Marimba: An Interpretation*.
- **D2** S. Davis (2018), DMA dissertation, University of North Texas.
- **D3** A. Holmes, notes on dead strokes, bowed vibraphone and bowed marimba. <https://adamholmesmusic.com/blog-deadstrokes/>
- **D4** R. Kite (2014), *Percussive Notes*, November.
- **B1** G. Essl and P. R. Cook (2000), "Measurements and efficient simulations of bowed bars", *J. Acoust. Soc. Am.* 108, 379.
- **B2** J. H. Smith and J. Woodhouse (2000), "The tribology of rosin", *J. Mech. Phys. Solids* 48, 1633.
- **S1** A. Chaigne and V. Doutaut (1997), "Numerical simulations of xylophones. I", *J. Acoust. Soc. Am.* 101, 539.
- **S2** USDA Wood Handbook, FPL-GTR-190, ch. 5.
- **S3** Maker specifications of rattan and birch mallet handles (Malletech, Vic Firth, Balter).
- **S4** Malletech, "Handles". <https://malletech.com/medialesson/handles/>

Membranes, gourds and holes:

- **M1** R. Bellet et al. (2014), arXiv:1407.4904.
- **M2** X. Luan et al. (2025), *J. Acoust. Soc. Am.* 157, 3836 (arXiv:2409.04853).
- **G1** D. Godlovitch et al. (2012), "Physical modeling and hybrid synthesis for the gyil African xylophone", SMC 2012.
- **K1** A. H. Benade and J. S. Murday (1967), *J. Acoust. Soc. Am.* 41, 1609 (meeting abstract).
- **K2** Instruments du Monde, "How to make a kalimba" (box and hole dimensions).

Guatemalan marimbas:

- **C1** J. I. Moreno Vázquez (2016), dissertation, KUG Graz, p. 102.
- **C2** J. Bermúdez (2023), *Revista Argentina de Musicología* 24(2), note 57.
- **C3** D. Vela (1962), transcribed at <http://orgulloguatemalteco.blogspot.com/2014/04/la-marimba-parte-2-construccion-de-la.html>.
- **C4** L. Kaptain (1992) (snippet).

Tunings:

- **N1** G. Rouget and J. Schwarz (1969), *Revue de musicologie* 55, 47 (not seen; values via N2).
- **N2** J. McBride and T. Tlusty, DaMuSc scale database (2023). <https://github.com/jomimc/DaMuSc>
- **N3** H. Tracey (1948), *Chopi Musicians*, pp. 119–140.
- **N4** P. Cooke (1992), *African Music* 7(2), 119.
- **N5** G. Kubik (1969), *African Music* 4(3), 22.
- **N6** H. Zemp and S. Soro (2010), *African Music* 8(4), 6.
- **N7** B. Hogan (2015), *Enemy Music*, Fig. 2-11 (Godsey 1980).
- **N8** T. Wiggins (2011), *Journal of Performing Arts* 4(2), 185.
- **N9** A. Tracey (1991), *African Music* 7(1), 82.
- **E1** Reason Studios, Rack Extension SDK documentation (Jukebox concepts, scripting specification), and `SDK/API/Jukebox.h`.
