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

// Mounted tine frequencies and bridge losses. The two-span shapes above keep
// the round-3 rigid-bridge solution for Carving's stiff endpoint, but a
// real tine rests on a bridge that yields under its reaction, and the
// pressure bar holds it with a line contact that lets it rotate. That mount
// lowers the upper modes more than the fundamental, more so for short, stiff
// treble tines, and reproduces Chapman's measured f2/f1 = 5.86 ... 5.30
// (Canadian Acoustics 34(3), 2006) with one bridge stiffness for all tines.
// The same bridge motion carries energy into the board: Gamma_n / Gamma_1 is
// the bridge loss of each mode relative to the fundamental's. The table is
// generated (and FEM-checked) by Tests/generate_kalimba_mount.py; outside
// C3..A5, where the fitted law is extrapolating, the end values are held.
struct KalimbaMountPoint {
    double hz;
    double ratio[8];
    double bridgeLoss[8];
};

// BEGIN GENERATED KALIMBA MOUNT
static constexpr int kKalimbaMountPoints = 12;
static constexpr KalimbaMountPoint kKalimbaMount[kKalimbaMountPoints] = {
    {130.81278265029931,
     {1, 6.09569602, 16.4029394, 30.3236977, 47.307231, 68.8091039, 96.1001593, 129.065293},
     {1, 8.66123344, 29.4027846, 55.4615895, 50.4485232, 20.1353982, 3.10973086, 0.0692399923}},
    {155.56349186104043,
     {1, 6.05944657, 16.1475027, 29.4973145, 46.0189648, 67.5919832, 94.997159, 127.862612},
     {1, 8.75893235, 29.1759772, 48.661253, 33.7228693, 9.18454788, 0.41206548, 1.01255063}},
    {184.99721135581717,
     {1, 6.01568002, 15.8410697, 28.6014515, 44.843376, 66.5534837, 93.9886367, 126.643323},
     {1, 8.83777338, 28.4765559, 40.2063257, 20.3435232, 3.31020149, 0.0424031201, 2.23327426}},
    {219.99999999999997,
     {1, 5.96299266, 15.4794925, 27.6773986, 43.8126225, 65.6547363, 93.021885, 125.36186},
     {1, 8.88907555, 27.1731041, 31.0325477, 10.9380118, 0.725541165, 0.667687947, 3.08493016}},
    {261.62556530059862,
     {1, 5.89982236, 15.0624806, 26.7716632, 42.9300569, 64.852877, 92.0523322, 123.989043},
     {1, 8.90179497, 25.1804189, 22.2449227, 5.08924899, 0.0110122593, 1.49507958, 3.29700532}},
    {311.12698372208087,
     {1, 5.8245001, 14.5954156, 25.925354, 42.1792332, 64.1077271, 91.0455731, 122.516613},
     {1, 8.86237487, 22.5126056, 14.7361, 1.90026065, 0.201906467, 2.09399086, 2.88149108}},
    {369.9944227116344,
     {1, 5.73535483, 14.0901756, 25.1663952, 41.5341544, 63.3847524, 89.98005, 120.959631},
     {1, 8.75506452, 19.3149873, 8.96128676, 0.451731072, 0.700438565, 2.28147838, 2.05804966}},
    {440,
     {1, 5.63087994, 13.5641699, 24.5070414, 40.966667, 62.6565568, 88.8499329, 119.352739},
     {1, 8.56309435, 15.8446686, 4.93934605, 0.0147251403, 1.16453872, 2.05565569, 1.14793506}},
    {523.25113060119725,
     {1, 5.50998203, 13.0376861, 23.9461259, 40.4506079, 61.9044006, 87.6665983, 117.73913},
     {1, 8.27106064, 12.4019114, 2.40122029, 0.0858081881, 1.42476629, 1.54405512, 0.43736602}},
    {622.25396744416184,
     {1, 5.37228202, 12.5305178, 23.4736107, 39.96373, 61.119846, 86.4561601, 116.155768},
     {1, 7.8685866, 9.25034974, 0.966682836, 0.346853395, 1.43215732, 0.940117412, 0.0672811891}},
    {739.98884542326891,
     {1, 5.21842577, 12.0591327, 23.0751939, 39.4887185, 60.3059949, 85.2523605, 114.6226},
     {1, 7.35457252, 6.56527514, 0.27095144, 0.612169561, 1.22332735, 0.426408894, 0.00743723807}},
    {880,
     {1, 5.0502983, 11.6350629, 22.7356942, 39.0139355, 59.4769866, 84.0867235, 113.141329},
     {1, 6.74048106, 4.421884, 0.0233374357, 0.786398483, 0.888043688, 0.110449373, 0.115839164}},
};
// END GENERATED KALIMBA MOUNT

struct KalimbaMount {
    double ratio[8];
    double bridgeLoss[8];
};

// Linear in log frequency between the generated notes; allocation-free and
// trigger-time only.
inline KalimbaMount GetKalimbaMount(double fundamentalHz) {
    const double hz = std::clamp(fundamentalHz, kKalimbaMount[0].hz,
                                 kKalimbaMount[kKalimbaMountPoints - 1].hz);
    int upper = 1;
    while (upper < kKalimbaMountPoints - 1 && kKalimbaMount[upper].hz < hz) ++upper;
    const auto& lo = kKalimbaMount[upper - 1];
    const auto& hi = kKalimbaMount[upper];
    const double t = std::clamp(std::log(hz / lo.hz) / std::log(hi.hz / lo.hz), 0.0, 1.0);
    KalimbaMount mount{};
    for (int m = 0; m < 8; ++m) {
        mount.ratio[m] = lo.ratio[m] + t * (hi.ratio[m] - lo.ratio[m]);
        mount.bridgeLoss[m] = lo.bridgeLoss[m] + t * (hi.bridgeLoss[m] - lo.bridgeLoss[m]);
    }
    return mount;
}

// Reaction the bridge takes per unit modal displacement: the jump of the
// third derivative (shear force / EI) across the support, in free-length
// units. Rigid-bridge reference for Carving and fundamental output calibration;
// the as-built voice uses KalimbaMountedBridgeRatio for its modal balance.
inline double KalimbaBridgeShearJump(int mode) {
    const auto& data = kKalimbaBeamModes[std::clamp(mode, 0, 7)];
    const double b = data.beta;
    const auto third = [b](const KalimbaBeamSegment& s, double span, double u) {
        return b * b * b * (s.ep * std::exp(b * (u - span)) - s.em * std::exp(-b * u)
                            + s.cosine * std::sin(b * u) - s.sine * std::cos(b * u));
    };
    // Rear span: x = u - span (x rises with u). Playing span: x = 1 - u.
    const double behind = third(data.rear, kKalimbaRearSpan, kKalimbaRearSpan);
    const double beyond = -third(data.playing, 1.0, 1.0);
    return beyond - behind;
}

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
