#pragma once

#include <algorithm>
#include <cmath>

namespace maremba {

// Euler-Bernoulli tine: clamp -- rear span -- bridge -- free playing span.
// Rear/free length=.30 is a representative geometry, not a specimen fit. The
// bridge fixes displacement while slope and bending moment remain continuous.
// See docs/PHYSICAL_MODEL_ROUND3_REFERENCES.md and the offline table generator.
static constexpr double kKalimbaRearSpan = .30;

struct KalimbaBeamSegment {
    // phi(u) = ep*exp(beta*(u-span)) + em*exp(-beta*u)
    //        + cosine*cos(beta*u) + sine*sin(beta*u).
    // Scaled exponentials avoid cancellation of large cosh/sinh terms.
    double ep, em, cosine, sine;
};
struct KalimbaBeamMode {
    double beta, ratio, contactScale;
    KalimbaBeamSegment rear, playing;
};

// Raw shapes have integral(phi^2 dx)=1 over the ENTIRE two-span beam, with
// free playing length=1. Modal mass therefore includes the motion behind the
// bridge, even though only the exposed playing span is directly excited/read.
// Fundamental contactScale preserves unit response at default position .825;
// upper scales preserve the old .5 RMS spatial scale over the whole beam.
// BEGIN GENERATED KALIMBA MODES
static constexpr KalimbaBeamMode kKalimbaBeamModes[8] = {
    {1.7553207528928054, 1, 0.66595327023763518,
     {1.7542464239683386, -1.4771829506501379, 0.44110420446043452, -2.5132616968398414},
     {0.73606552767432631, 0.84314500466924214, 0.97037524919084683, -0.71591476014763744}},
    {4.4488990411989011, 6.4238016989902063, 0.57008771254956903,
     {0.86201550170147745, -0.58739008165719919, 0.36046871806896003, -0.81431144524543853},
     {0.71894750196538826, -0.98872113344177825, -0.98031560784683835, 0.99712665903671815}},
    {7.495719083512089, 18.235330099723551, 0.57008771254956903,
     {0.61763365756959232, -0.40205750506036969, 0.33687573891116507, -0.46723927120957426},
     {0.57465690766031519, 0.98251060535119827, 0.98282980264128994, -0.98219140806110672}},
    {10.512563366321446, 35.867761912486458, 0.57008771254956903,
     {0.42863893249010759, -0.43394963073254095, 0.41565064003038366, -0.45224862143469829},
     {0.41013970208780515, -0.97375155127103741, -0.97374039848036487, 0.97376270406170984}},
    {13.287837969789367, 57.305416395512175, 0.57008771254956903,
     {-0.059914440111627928, -0.94111624852653852, 0.9422287000452727, -0.94000379700780423},
     {-0.077389913187933146, 0.85654056982490046, 0.85654043865032936, -0.85654070099947155}},
    {14.785514310981627, 70.951224819724288, 0.57008771254956903,
     {-0.90814039011949388, -1.3612706291212986, 1.3720296514399253, -1.3505116068026717},
     {-0.92426753754879576, -0.65974231966670105, -0.65974267003953224, 0.65974196929386986}},
    {17.057170785429289, 94.428049889867793, 0.57008771254956903,
     {-0.71678112773140723, -0.55014255059328254, 0.55443827110452437, -0.5458468300820406},
     {-0.7200782085962949, 0.95277464004336643, 0.95277461188908041, -0.95277466819765255}},
    {20.009366097899086, 129.94323832780685, 0.57008771254956903,
     {-0.50313588714269664, -0.40664381965566138, 0.40788746947329524, -0.40540016983802751},
     {-0.50414102615916223, -0.97472475174645545, -0.97472475277588055, 0.97472475071703035}},
};
// END GENERATED KALIMBA MODES

inline double KalimbaModeRatio(int mode) {
    return kKalimbaBeamModes[std::clamp(mode, 0, 7)].ratio;
}

inline double KalimbaSegmentValue(const KalimbaBeamSegment& segment, double beta,
                                 double span, double position) {
    const double z = beta * position;
    return segment.ep * std::exp(beta * (position - span))
         + segment.em * std::exp(-z)
         + segment.cosine * std::cos(z) + segment.sine * std::sin(z);
}

// x=-.30 is the clamp, x=0 the bridge, x=1 the free end. This unscaled mode
// shape is exposed for independent boundary/mass validation, not per-sample use.
inline double KalimbaFullModeShape(int mode, double position) {
    const auto& data = kKalimbaBeamModes[std::clamp(mode, 0, 7)];
    position = std::clamp(position, -kKalimbaRearSpan, 1.0);
    return position < 0.0
        ? KalimbaSegmentValue(data.rear, data.beta, kKalimbaRearSpan, position + kKalimbaRearSpan)
        : KalimbaSegmentValue(data.playing, data.beta, 1.0, 1.0 - position);
}

// Signed force/release projection averaged over the physical contact footprint.
// Position runs from BRIDGE=0 to FREE END=1, retaining the voice's existing
// .65+.35*Position playable-region mapping. Frequencies and shapes come from
// the same boundary problem. This runs only at Trigger, never per sample.
inline double KalimbaContactWeight(int mode, double position, double width) {
    const auto& data = kKalimbaBeamModes[std::clamp(mode, 0, 7)];
    position = std::clamp(position, 0.0, 1.0);
    const double halfWidth = .5 * std::clamp(width, 0.0, .25);
    const double lo = std::max(0.0, position - halfWidth);
    const double hi = std::min(1.0, position + halfWidth);
    const double centre = 1.0 - .5 * (lo + hi);
    const double h = data.beta * .5 * (hi - lo);
    const double hyperbolicAverage = h > 1.e-3 ? (std::exp(h) - std::exp(-h)) / (2.0 * h)
        : 1.0 + h * h / 6.0 + h * h * h * h / 120.0;
    const double circularAverage = h > 1.e-8 ? std::sin(h) / h : 1.0;
    const double z = data.beta * centre;
    const auto& s = data.playing;
    const double hyperbolic = s.ep * std::exp(data.beta * (centre - 1.0)) + s.em * std::exp(-z);
    const double circular = s.cosine * std::cos(z) + s.sine * std::sin(z);
    return data.contactScale * (hyperbolic * hyperbolicAverage + circular * circularAverage);
}

} // namespace maremba
