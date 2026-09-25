// The carved-bar tables (DSP/BarModes.h, written by Tests/generate_bar_modes.py)
// as the voice reads them: the per-key interpolation is continuous and keeps
// the modes ordered, the shapes keep their parity about the centre and their
// node counts, and the tuning targets hold where makers tune them. The table
// itself is compared with a fresh generator run by `generate_bar_modes.py --check`.
#include "MarimbaModel.h"
#include "ModalExcitation.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>

namespace {
using maremba::EMarimbaModel;
int failures = 0;
void check(bool ok, const std::string& message) {
    if (!ok) { ++failures; std::cerr << "FAIL " << message << '\n'; }
}
// Sign changes of a mode shape along the whole bar (its interior nodes).
int nodes(const maremba::BarKey& key, int mode) {
    int count = 0;
    double previous = maremba::BarShape(key, mode, 0.0);
    for (int i = 1; i <= 2000; ++i) {
        const double value = maremba::BarShape(key, mode, i / 2000.0);
        if ((value > 0.0) != (previous > 0.0) && std::abs(value - previous) > 0.0) ++count;
        previous = value;
    }
    return count;
}
} // namespace

int main() {
    const char* names[] = {"rosewood", "hormigo", "balafon"};
    for (int model = 0; model < 3; ++model) {
        const auto type = static_cast<EMarimbaModel>(model);
        double worstJump = 0.0;
        for (double midi = 24.0; midi < 108.0; midi += 0.25) {
            const auto key = maremba::GetBarKey(type, midi);
            const auto next = maremba::GetBarKey(type, midi + 0.25);
            check(key.ratio[0] == 1.0, std::string("fundamental ratio is not 1: ") + names[model]);
            for (int m = 1; m < maremba::kBarModes; ++m)
                check(key.ratio[m] > key.ratio[m - 1], std::string("bending modes out of order: ") + names[model]);
            for (int m = 0; m < maremba::kBarModes; ++m) {
                // Relative to the shape's normalization, not to the bar's mass:
                // short treble bars' high modes exceed 1.
                check(std::isfinite(key.massFraction[m]) && key.massFraction[m] > 0.0 && key.massFraction[m] < 10.0,
                      std::string("modal mass fraction invalid: ") + names[model]);
                worstJump = std::max(worstJump, std::abs(std::log(next.ratio[m] / key.ratio[m])));
            }
            // Tuned bars shorten and lighten up the keyboard. A balafon key's
            // length and carving are chosen together to reach its own
            // overtone, so neighbours may differ either way (by up to ~20 %).
            if (model != 2)
                check(key.lengthM > next.lengthM && key.massKg > next.massKg,
                      std::string("bars do not shorten and lighten up the keyboard: ") + names[model]);
            check(key.torsion[0] > 0.0 && key.torsion[1] > key.torsion[0] && key.lateral > 1.0,
                  std::string("torsional or lateral ratios invalid: ") + names[model]);
        }
        // Linear interpolation between anchors three semitones apart: a quarter
        // semitone never moves a ratio by more than a few percent.
        check(worstJump < 0.05, std::string("mode ratios jump between keys: ") + names[model]);

        for (int note : {36, 60, 84}) {
            const auto key = maremba::GetBarKey(type, note);
            for (int m = 0; m < maremba::kBarShapedModes; ++m) {
                double worstParity = 0.0, peak = 0.0;
                for (int i = 0; i <= 100; ++i) {
                    const double x = i / 200.0;
                    const double left = maremba::BarShape(key, m, x), right = maremba::BarShape(key, m, 1.0 - x);
                    worstParity = std::max(worstParity, std::abs(left - (m % 2 ? -right : right)));
                    peak = std::max(peak, std::abs(left));
                }
                check(worstParity <= 1e-12 * peak, std::string("mode shape breaks its parity: ") + names[model]);
                // A free bar's n-th bending mode has n + 1 interior nodes.
                check(nodes(key, m) == m + 2, std::string("mode shape has the wrong node count: ") + names[model]
                      + " note " + std::to_string(note) + " mode " + std::to_string(m));
                // Continuous through the stored samples (Catmull-Rom): no step
                // larger than the shape's own slope allows.
                double worstStep = 0.0;
                for (int i = 1; i <= 4000; ++i)
                    worstStep = std::max(worstStep, std::abs(maremba::BarShape(key, m, i / 4000.0)
                                                            - maremba::BarShape(key, m, (i - 1) / 4000.0)));
                check(worstStep < 0.02 * peak, std::string("mode shape is discontinuous: ") + names[model]);
            }
        }
    }
    // Tuning practice: concert rosewood 1:4:10 in the bass, 1:4 through C5;
    // traditional hormigo bars tuned in their fundamental only (the first
    // overtone falls where the arch puts it, between a flat bar's 2.76 and 4,
    // lower up the keyboard); balafon keys untuned (first overtone 2.3-3.7
    // across a balafon's range).
    const auto rosewoodBass = maremba::GetBarKey(EMarimbaModel::ImperialRosewood, 48);
    check(std::abs(rosewoodBass.ratio[1] - 4.0) < 0.06 && std::abs(rosewoodBass.ratio[2] - 10.0) < 0.1,
          "rosewood bass bars are not tuned 1:4:10");
    check(std::abs(maremba::GetBarKey(EMarimbaModel::ImperialRosewood, 72).ratio[1] - 4.0) < 0.06,
          "rosewood C5 is not tuned 1:4");
    check(maremba::GetBarKey(EMarimbaModel::ImperialRosewood, 96).ratio[1] < 3.0,
          "rosewood top octave is still overtone tuned");
    const double hormigoC4 = maremba::GetBarKey(EMarimbaModel::MayanPadauk, 60).ratio[1];
    check(hormigoC4 > 3.0 && hormigoC4 < 3.85
          && maremba::GetBarKey(EMarimbaModel::MayanPadauk, 72).ratio[1] < hormigoC4 - 0.15
          && maremba::GetBarKey(EMarimbaModel::MayanPadauk, 84).ratio[1] > 2.76,
          "hormigo bars are not carved to their fundamental alone");
    double lowest = 1e9, highest = 0.0;
    for (int note = 36; note <= 96; ++note) {
        const double r = maremba::GetBarKey(EMarimbaModel::BalafonAncestral, note).ratio[1];
        lowest = std::min(lowest, r);
        highest = std::max(highest, r);
    }
    check(lowest >= 2.3 && highest <= 3.75 && highest - lowest > 0.8,
          "balafon first overtones are not the untuned 2.3-3.7 spread");
    std::cout << "BARMODES balafon_first_overtone=[" << lowest << ',' << highest << "]\n";
    std::cout << "BarModesTest " << (failures ? "FAILED" : "PASSED") << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
