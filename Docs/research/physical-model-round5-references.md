# Fifth-pass research notes

Research reviewed on 2026-09-24 for the [fifth physical-model pass](physical-models-round5.md),
in addition to the [fourth pass's sources](physical-model-round4-references.md).
Each row says what a source establishes and how the engine uses it. Where a
number is derived from a source rather than printed in it, the row says so.
"Abstract" marks a source of which only the abstract could be read.
Practitioner measurements (Holdaway) are real recordings, not peer reviewed.
Nothing here calibrates the four voiced instrument families against a
particular instrument, and a green contract is not a listening test.

## Findings used

| Mechanism | What the sources support | Consequence in the engine |
| --- | --- | --- |
| Rosewood bar decay by register | A Yamaha YM-5100A struck with a yarn mallet, no tubes: loss factors at f1 give T60 6.7, 4.7, 2.2, 1.1, 0.54 and 0.18 s at C2–C7 (derived, T60 = 2.2/ηf); mean η 0.0045, 0.0036–0.0068 over f1–f20. With its tube the C2 fundamental falls from 6.8 to 3.7 s while f4, f10, f20 barely change [R1]. | Material loss η(f) = η0 + η2 (f/1 kHz)² + a support term; the bare-bar fundamentals render 6.76 / 4.03 / 2.02 / 1.01 / 0.48 / 0.16 s (checked ±25 % by `Round5ResponseTest`). The tube, not the loss law, shortens the sounding fundamental. |
| Loss above 2 kHz | Madagascar *Dalbergia*: η about 3.8 × 10⁻³ at 0.4–1 kHz, rising by about 1.4 × 10⁻³ toward 5.8 kHz [R10]. | The η2 term; no register clamp. |
| Tube Q, loading and tuning | A 25 mm tube with a bar over it: damping ratios 0.73–0.94 % at 174–1206 Hz, Q ≈ 68–53; the bar over the mouth lowers the tube by up to 2 %; a tube shortens the bar's decay when it is more damped than the bar [R8]. A maker: players want the tube slightly above the bar, progressively more in the treble; "sharpen for volume, flatten for length"; top-octave caps act as reflectors [R9]. | Tube Q 45 at C4 rising toward the bass and falling where the mouth radiates; the air mode sits −1 to −9 cents from its bar through C5 (the bar's loading included). The top octave is lowered further (−33 cents at C7), the "length" choice, **not** the makers' sharpened treble. |
| Overtone tuning | Mode 2 at 4:1 over the first 3½ octaves, mode 3 at 10:1 over the first two, then falling; mode 2 about 2.7–2.8× at C7 [R5]. A builder: C2–G♯3 1:4:10, A3–C5 1:4, C♯5–C7 fundamental only [R16]. | Timoshenko finite-element bars (`Tests/generate_bar_modes.py`) carved to 1:4:10 to G♯3, 1:4 to F♯5 and the fundamental alone above. The 4:1 partial holds through C5; the carving reaches 3.64× at F♯5 and 2.27× at C8. |
| Untuned modes of a C3 bar | Rosewood C3 bar 46 × 6 × 2.35 cm, f1 130 Hz: T1 2.48×, L1 7.0×, V3 10.7×, T2 11.1×, bending (5,0)–(8,0) 19.4, 24.6, 38.7, 48.2× [R4]. | Generator validation (`--bork`): V2–V4, T1, L1 within 0.7 %, T2 −4.5 %, V6–V7 −6.2 %, V5 **+14.9 %**. |
| Hertzian mallets | Xylophone mallets: Hertz exponent 1.5; rubber K 3.7 × 10⁷, boxwood 1.31 × 10⁹ N/m^1.5; contact 0.65–1.3 ms (rubber), 0.17–0.4 ms (boxwood), τ ∝ V^−0.2; effective mass 23.6–26.2 g [R2]. No measured yarn or cord marimba mallet was found. | Rubber (26 g) and boxwood (24 g) heads from that law: 0.60 and 0.18 ms on a rigid wall at 1 m/s. Yarn (2.4 ms) and cord (1.5 ms) with a harder core are voiced. |
| Balafon keys | A gyil key without its gourd: strongest overtone 3.4× at −14 dB [B1]. Central African keys: first inharmonic partial 1.87–5.67× per key, second 3.2–9.5×; no deliberate overtone tuning; makers hollow the centre to flatten and thin the ends to sharpen [B2, B5]. | Each balafon key is carved to its own first-overtone target in 2.2–3.7× by those two moves; the generated keys land at 2.2–3.6× (2.55–3.42× from C3 to C6). |
| Vène and hormigo loss | *Pterocarpus erinaceus*: density 0.82, tan δ 5.56 ‰; *Platymiscium*: 4.75–4.94 ‰ [B9]. | Loss factors 0.0056 (Balafon) and 0.0048 (Padauk). |
| Mirlitons | 2–5 membranes per gyil gourd; they buzz only when a key is played with enough force; with gourds, peaks at 2–9× the fundamental between −9.5 and −20 dB and about 5 dB of broadband energy over 1–6 kHz [B1]. A clear threshold under sine drive [B16]. A healthy membrane gives many harmonics of nearly equal amplitude [B2]. | Two or three membranes per gourd, thresholds re-drawn every stroke; the buzz is read out without the fundamental, which stays in the gourd's radiation. |
| Charleo | A small hole near the bottom of each box, covered with pig intestine over a beeswax ring [B19]; wooden boxes are tuned far more precisely than gourds [B11]. No acoustic measurement of the charleo was found. | Charleo at the box's closed end; threshold, formant and level are voiced so that it buzzes at mezzo-forte in the Guatemalan patches. |
| Traditional tunings | Malinké xylophones are equiheptatonic [B12, catalogue record only]; Jessup found Gambian Mandinka balafons "equidistant heptatonic", "not as incredibly accurate as some have stated" [B13]. Chopi timbila 252, 276, 304, 336, 368, 408, 456, 504 Hz [B14]. Sambla: degrees 1, ♭3, 3, 5, 6, ♭3 just under 300 cents above 1 and just over 100 below 3 [B6]. Amadinda and Senufo balafons: roughly equal-step pentatonic [B15, B17]. | The five Scale settings (`DSP/Tunings.h`). The 5-equal table follows the equal-step descriptions; Wachsmann (1950) and Cooke (1970), cited in the code for the Ganda tuning, were not read for this pass. |
| Kalimba decay and balance | Hugh Tracey alto, derived T60: F♯4 fundamental 4.5 s, overtone 0.84 s, 2× line 1.8 s; C5 2.6, 0.34, 1.4 s. At 0.1 s the overtone is 17–26 dB and the 2× line 35–41 dB below the fundamental. Low and mid notes last "a few seconds", top notes "a few tenths of a second" [K-H]. | Tine T60: 5.3 s at C4, 2.4–4.2 s from E4 to B4 (median 2.7), 0.39 s at E6. Bridge-rod 2 f1 line. |
| Kalimba box and holes | Box resonance about 440 Hz (alto) and 493–523 Hz (treble); the note on it is the loudest and dies sooner. Covering the holes lowers the resonance: the most resonant notes get quieter, some lower notes louder; it changes level and timbre, never pitch; only 3–4 mid-range notes respond strongly [K-H]. Structural box modes match fast-decaying parts of the attack [K-L08, abstract]. | One shared box for all tines: a Helmholtz mode at 400 Hz (a 0.8 L 17-key box) and three plate modes, driven by the summed bridge force. The holes radiate opposite to the board (bass-reflex phase). The mod wheel covers them: G4 −4.3 dB, A4 −3.9 dB, C4 +3.5 dB, pitch unchanged. |
| Kalimba sympathetic ring | Playing A4 on the alto mainly sets off its physical neighbours, a third away [K-H]. 17-key C layout D6 B5 G5 E5 C5 A4 F4 D4 C4 E4 G4 B4 D5 F5 A5 C6 E6 [K-EMD]. | The sympathetic bank's kalimba receivers are the 17 tines, coupled through the bridge by distance on that layout. |
| Kalimba re-pluck | Pressing the short rear span damps the note [R4-15]; no lamellophone measurement of thumb contact was found [K-H]. | The thumb stops the tine before plucking it again (the old ring loses 26 dB); release times are voiced. |

## References

Concert marimba:

- **R1** H. Hill (2011), "Exponential Time Decay Constants of Marimba Bars", UIUC REU report. Not peer reviewed. <https://courses.physics.illinois.edu/phys406/sp2017/NSF_REU_Reports/2011_reu/Heather_Hill/Hhill1_FinalPaper.pdf>
- **R2** A. Chaigne and V. Doutaut (1997), "Numerical simulations of xylophones. I", *J. Acoust. Soc. Am.* 101, 539. <https://www.dropbox.com/s/uskhd4mmf96kgn6/chaigne_xylo_jasa97.pdf>
- **R4** I. Bork et al. (1999), "Comparison between modal analysis and finite element modelling of a marimba bar", *Acustica* 85, 258. <https://www.dropbox.com/s/8wxzly02x81u7aq/marimba99AA.pdf>
- **R5** T. D. Rossing (2004), "Acoustics of percussion instruments: An update", *Acoust. Sci. Tech.* 25, 406 (summarising Yoo, Rossing and Larkin). <https://www.jstage.jst.go.jp/article/ast/25/6/25_6_406/_pdf>
- **R8** V. Soares, V. Debut and J. Antunes (2023), "The bar-resonator interaction in mallet percussion instruments", *J. Sound Vib.* <https://www.sciencedirect.com/science/article/pii/S0022460X22007118>
- **R9** L. H. Stevens, "The Acoustics of Resonators", Malletech. <https://malletech.com/wp-content/uploads/2019/01/Acoustics_of_Resonators1-4.pdf>
- **R10** L. Brancheriau, C. Kouchade and I. Brémaud (2010), "Internal friction measurement of tropical species by various acoustic methods", *J. Wood Sci.* 56, 371. <https://link.springer.com/article/10.1007/s10086-010-1111-8>
- **R16** La Favre (2007), "Marimba bar and resonator data" (builder). <https://www.lafavre.us/marimba-bars.htm>

African xylophones and the Guatemalan marimba:

- **B1** D. Godlovitch, T. F. Tavares, S. Trail and G. Tzanetakis (2012), "Physical Modeling and Hybrid Synthesis for the Gyil African Xylophone", SMC. <https://webhome.csc.uvic.ca/~gtzan/smc2012model.pdf>
- **B2** F. Voisin (1993), "L'accord des xylophones Gbaya et Manza de Centrafrique" (EHESS). <https://hal.science/hal-01612624>
- **B5** M. Branger (1993), "Le xylophone à résonateurs multiples des Lobi" (ORSTOM/IRD). <https://horizon.documentation.ird.fr/exl-doc/pleins_textes/pleins_textes_7/b_fdi_03_02/37444.pdf>
- **B6** L. McPherson (2018), "The talking balafon of the Sambla" (preprint). <https://bpb-us-e1.wpmucdn.com/sites.dartmouth.edu/dist/5/444/files/2020/10/Talking-balafon-of-the-Sambla-revised-small.pdf>
- **B9** I. Brémaud (2006), PhD thesis on the diversity of woods used for musical instruments (Table 10). <https://theses.hal.science/tel-00548934>
- **B11** R. Garfias (1983), "The Marimba of Mexico and Central America", *Latin American Music Review* 4(2). <https://escholarship.org/uc/item/6d85q61v>
- **B12** G. Rouget and J. Schwarz (1969), "Sur les xylophones équiheptaphoniques des Malinké" (catalogue record only). <https://collectionsdumusee.philharmoniedeparis.fr/collectionsdumusee/doc/ALOES/0801491/sur-les-xylophones-equiheptaphoniques-des-malinke-gilbert-rouget>
- **B13** A. Tracey (1987), review of L. Jessup, *The Mandinka Balafon*, *African Music* 6(4). <https://journal.ru.ac.za/index.php/africanmusic/article/view/1267>
- **B14** H. Tracey (1958), "Towards an assessment of African scales", *African Music* 2(1). <https://journal.ru.ac.za/index.php/africanmusic/article/view/522>
- **B15** H. Zemp and S. Soro (2010), "Talking balafons", *African Music* 8(4). <https://journal.ru.ac.za/index.php/africanmusic/article/view/1864>
- **B16** S. Trail et al. (2012), "Direct and surrogate sensing for the Gyil African xylophone", NIME. <https://www.nime.org/proceedings/2012/nime2012_222.pdf>
- **B17** Grinnell College Musical Instruments collection, "amadinda". <https://omeka-s.grinnell.edu/s/MusicalInstruments/item/2394>
- **B19** D. Lehnhoff (2007), liner notes, *Marimba Chapinlandia*, Smithsonian Folkways SFW 40542. <https://folkways-media.si.edu/docs/folkways/artwork/SFW40542.pdf>

Kalimba:

- **K-H** M. Holdaway, Kalimba Magic "Tip of the Day", 2006–2010 (dated tips), and the articles "Fundamentals of the Kalimba", "Comparing Different 17-Note Kalimbas in C" and "How Do You Get the High Notes to Sound Good?". <https://web.archive.org/web/2012/http://www.kalimbamagic.com/tipoftheday/archives_friday/tip_fri_MMDDYY.php>, <https://www.kalimbamagic.com/info/how-to-play/fundamentals-of-the-kalimba>
- **K-L08** A. Ludwigsen (2008), "Modes of the kalimba resonator box", *J. Acoust. Soc. Am.* (abstract). <https://doi.org/10.1121/1.4782900>
- **K-EMD** EMD Music (2020), "Kalimba 10/17/21 keys" (manual). <https://static.emdmusic.com/images/products/00//manuals/manual_26433_en.pdf>
- **R4-15** D. M. F. Chapman (2006), source 15 of the [fourth-pass notes](physical-model-round4-references.md).

Not accessible for this pass: Fletcher & Rossing ch. 19, Chapman (2012) beyond
its abstract, the Ludwigsen papers beyond their abstracts, Castejón et al.
(2023) and Vadillo et al. (2025), Rouget (1969) and Jessup (1983) in full,
Debut & Oliveira (2021), and any measurement of yarn or cord marimba mallets,
charleo spectra, balafon key decay or kalimba thumb contact.
