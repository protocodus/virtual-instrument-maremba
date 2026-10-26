# Fourth-pass research notes

Research reviewed on 2026-09-22 for the [fourth physical-model pass](physical-models-round4.md).
Each row says what a source establishes and how the engine uses it. Where a
number is derived from a source rather than printed in it, the row says so.
Nothing here is a calibration of the four voiced instrument families, and a
green contract is not a listening test.

## Findings used

| Mechanism | What the sources support | Consequence in the engine |
| --- | --- | --- |
| Bar dipole, tube monopole | The xylophone model treats the bar as a line of dipoles and the tube mouth as a monopole. Measured directivity turns from dipole to monopole between about 10 and 50 ms; for an A4 bar with an 18.5 mm-radius tube the bar's components lie about 20 dB below the tube's [2]. The resonator shortens the fundamental's decay because it radiates better [3]. On a Yamaha instrument only the fundamental's decay changed with the resonator [8]. | The cavity readout radiates most of the low fundamental; direct bar radiation follows a dipole efficiency normalized at C4's 4:1 partial. |
| Recorded bloom | Not a publication: the repository's marimba reference samples (`Demos/Reference`, see `THIRD_PARTY_NOTICES.md`) peak 50–160 ms after the stroke at the fundamental, 8–13 dB above its first 20 ms, while 4:1 and 10:1 partials peak at once. | Target for the tube Q and mouth gain; checked by `Round4ResponseTest`. |
| Support cords | Friction at the cords matters only below the lowest bar's fundamental [1]. | No support-damping term was added. |
| Wood loss law | Decay rate a0 + a2 f² for xylophone bars; padauk G4 values give Q about 192, 463 and 359 at 1×, 4× and 10× (derived) [1]. The law is confirmed across 59 species; Dalbergia has the lowest first-mode loss [7]. Rosewood bars without resonators show a roughly constant loss factor 0.0036–0.0068 over f1, f4, f10 and f20 [8, not peer reviewed]. | Upper partials: T60 ratios near 0.3 (4:1) and 0.1–0.13 (10:1) of the fundamental instead of 0.15 and 0.04. |
| Torsional modes | A rosewood C3 bar (46 × 6 × 2.35 cm, f1 130 Hz): T1 323 Hz (2.48×), T2 1441 Hz (11.1×), lateral bending 912 Hz (7.0×). A strike near an edge makes T2 nearly as loud as an upper bending mode; the centre line does not excite it; T1 radiates weakly [3]. Across a Malletech 5-octave instrument T1 falls from about 1.9× in the largest bars to 1.2× in the smallest [4]. T2 lies near the third bending mode from D3 to C4, and makers dislike untuned torsional modes over a range of notes [5, 6]. | Two torsional modes per wooden bar with that frequency law, driven by the off-axis part of each contact; no cavity coupling. The note law between the anchors and the drive levels are voiced. |
| Mirliton | On the gyil, 2–5 spider-silk membranes (0.5–1 cm radius) per gourd are highly nonlinear and buzz only when a bar is played with enough force; old membranes need more energy. The gourds add non-harmonic components near 1.2 and 1.8 kHz and energy below the fundamental; an asymmetric wavefolder and clipper sounded best [9]. A thin membrane with cubic stiffness coupled to an air column pumps energy out almost irreversibly above a threshold [10]. A dizi membrane lowers and flattens the resonance peaks in its linear regime [11]. | A slack membrane with asymmetric taut limits, a fixed formant near 1.15 kHz and threshold-gated buzz; its slap loss is drained from the air. The reactive detuning of [11] is not modelled; no source measures how much a mirliton shortens a note. |
| Charleo | Guatemalan marimba resonators have holes at the bottom crowned with wax rings and covered with pig-intestine membranes that buzz ("charleo") [12]; the same on the marimba de arco, with a beeswax-ringed hole near the closed end of each cedar tube [13] and in a museum instrument [14]. | Padauk's native Buzz: a heavier membrane, lower formant, more bulged rest. Its level and formant are voiced. |
| Mounted kalimba tine | On a small kalimba, free lengths 5.4–3.7 cm with bridge-to-bar spans 1.3–1.4 cm give f2/f1 = 5.86, 5.63, 5.76, 5.61, 5.30, falling with a/b and below the cantilever's 6.2669; pressing the short rear span damps the note [15]. Overtone ratios depend on where the bridge subdivides the tine [16]. Mbira overtones lie near 5× and 14× [17]. | A pinned bar and a yielding bridge, one stiffness for all tines, fitted to [15] (RMS 0.08); bridge losses from the same solution. The fit and the hold outside C3–A5 are the engine's, not the papers'. |
| Mbira buzzers | Buzzers of metal beads or loose bottle caps near the bottom of the board, and caps around the gourd rim [18]; traditionally shells (machachara). | A loose buzzer on the board, lifted by board acceleration above its preload. Ring frequencies, preload and restitution are voiced; no measured spectra were found. |

## References

1. A. Chaigne and V. Doutaut (1997), **Numerical simulations of xylophones. I.
   Time-domain modeling of the vibrating bars**, *J. Acoust. Soc. Am.* 101, 539.
   [Author PDF](https://www.dropbox.com/s/uskhd4mmf96kgn6/chaigne_xylo_jasa97.pdf).
2. V. Doutaut, D. Matignon and A. Chaigne (1998), *J. Acoust. Soc. Am.* 104, 1633
   (second part of the xylophone series: resonator and radiated pressure).
   [Author PDF](https://www.dropbox.com/s/z29g9tg7m751i0d/doutaut_xylo_jasa98.pdf).
3. I. Bork, A. Chaigne, L.-C. Trebuchet, M. Kosfelder and D. Pillot (1999),
   **Comparison between modal analysis and finite element modelling of a marimba
   bar**, *Acustica* 85, 258–266.
   [Author PDF](https://www.dropbox.com/s/8wxzly02x81u7aq/marimba99AA.pdf).
4. T. D. Rossing, J. Yoo and A. Morrison (2004), **Acoustics of percussion
   instruments: An update**, *Acoust. Sci. & Tech.* 25(6), 406.
   [J-STAGE](https://www.jstage.jst.go.jp/article/ast/25/6/25_6_406/_pdf).
5. D. Beaton and G. Scavone (2019), ISMA 2019, finite-element study of Yamaha
   bars C2–C4. [Paper](https://pub.dega-akustik.de/ISMA2019/data/articles/000039.pdf).
6. D. Beaton and G. Scavone (2021), *J. Acoust. Soc. Am.* 149, 3758.
   [Indexed abstract](https://pubmed.ncbi.nlm.nih.gov/34241415/).
7. M. Aramaki et al. (2007), *J. Acoust. Soc. Am.* 121, 2407 (xylophone bar
   woods; decay-rate law across 59 species).
   [PDF](http://kronland.fr/wp-content/uploads/2016/08/jasa_Xylophone.pdf).
8. H. Hill (2011), UIUC physics REU report on a Yamaha YM-5100A marimba. Not peer
   reviewed. [Report](https://courses.physics.illinois.edu/phys406/sp2017/NSF_REU_Reports/2011_reu/Heather_Hill/Hhill1_FinalPaper.pdf).
9. D. Godlovitch, S. Trail, T. F. Tavares and G. Tzanetakis (2012), **Physical
   Modeling and Hybrid Synthesis for the Gyil African Xylophone**, SMC 2012.
   [Paper](https://webhome.csc.uvic.ca/~gtzan/smc2012model.pdf).
10. R. Bellet, B. Cochelin, P. Herzog and P.-O. Mattei (2010), *J. Sound Vib.* 329,
    2768 (nonlinear membrane coupled to an acoustic system).
    [Publisher abstract](https://www.sciencedirect.com/science/article/abs/pii/S0022460X10000908).
11. Luan, Wang, Scavone and Li (2024), dizi membrane in the linear regime.
    [arXiv:2409.04853](https://arxiv.org/abs/2409.04853).
12. Arrivillaga Cortés et al., **Social History of the Marimba in Guatemala**
    (USAC). [PDF](https://ceceg.usac.edu.gt/wp-content/uploads/2025/09/historia-social-de-la-marimba.pdf).
13. Grinnell College Musical Instruments collection, marimba de arco (after
    Scruggs 1999). [Item 3446](https://omeka-s.grinnell.edu/s/MusicalInstruments/item/3446).
14. Musical Instruments Museum, Brussels, marimba (S. Willaert).
    [Collection piece](https://www.mim.be/en/collection-piece/marimba).
15. D. M. F. Chapman (2006), **Investigating the Sound of an African Thumb Piano
    (Kalimba)**, *Canadian Acoustics* 34(3), 26–27.
    [PDF](https://jcaa.caa-aca.ca/index.php/jcaa/article/download/1805/1552/1942).
16. D. M. F. Chapman (2012), **The tones of the kalimba (African thumb piano)**,
    *J. Acoust. Soc. Am.* 131(1), 945–950. [Indexed abstract](https://pubmed.ncbi.nlm.nih.gov/22280717/).
17. McNeil and Mitran (2008), *J. Acoust. Soc. Am.* 123, 1169 (mbira overtones).
    [Indexed abstract](https://pubmed.ncbi.nlm.nih.gov/18247916/).
18. Austin and Vetter, Grinnell College Musical Instruments collection, mbira
    dzavadzimu. [Item 1828](https://omeka-s.grinnell.edu/s/MusicalInstruments/item/1828).

Every finding above rests on a passage read for this pass; for sources 6, 10,
11, 16 and 17 that was the abstract. Bretos et al. (1999) and the Soares, Debut
and Antunes bar–resonator papers were not accessible for this pass.
