// The five direct outputs are stems of the instrument, not of the mix. Volume,
// the three Level coefficients, Width, Warmth and Limit belong to Main and never
// reach them; what sets a stem's loudness is its fixed calibration. That
// calibration gives Close, Room and Piezo the same headroom on every
// instrument: a full-velocity chord reaches the top of every stem, so no direct
// output is left far behind Main or behind the other two, the Kalimba's
// included. Changing Model while a note rings moves the calibration smoothly
// (docs/OUTPUT_LEVEL.md).
#include "MarembaEngine.h"
#include "OutputHeadroom.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace maremba {
struct MarembaEngineTestAccess {
    static std::array<float, 3> StemTrim(const MarembaEngine& engine) { return engine.mStemTrim; }
};
} // namespace maremba

namespace {
using namespace maremba;
using Audio = std::array<std::vector<float>, 7>;
using Notes = std::vector<std::pair<int, float>>;

constexpr double kRate = 48000.0;
constexpr int kBatch = 64;
constexpr int kMain = 0, kClose = 2, kRoom = 4, kPiezo = 6; // first channel of each group

// What "not quiet" means for a stem, in dBFS peak, on every instrument: a
// full-velocity chord reaches within chordFloorDb of full scale, a mezzo-forte
// note stays above noteFloorDb, and the three stems' chord peaks lie within
// spreadDb of each other. The bars share one calibration and the Kalimba, which
// radiates less into its close microphones, has its own (Rosewood, Padauk and
// Balafon on the default patch peak at -3.5 to -0.5 dBFS on the chord).
struct Expectation { double chordFloorDb, spreadDb, noteFloorDb; };
constexpr Expectation kExpected{-5.0, 4.0, -24.0};

int checks = 0;
std::string context;

void require(bool okay, const char* message) {
    ++checks;
    if (!okay) {
        std::cerr << "FAIL " << context << ": " << message << '\n';
        std::exit(1);
    }
}

// The factory default patch, "Concert Grand Rosewood", as the wrapper maps it.
EngineParameters defaultPatch(int model) {
    EngineParameters p;
    p.model = model;
    p.malletType = 1;
    p.malletHardness = .55f;
    p.strikePosition = .47f;
    p.strikeJitter = 15.0f;
    p.resonatorCoupling = .75f;
    p.decay = 1.0f;
    p.buzzAmount = 0.0f;
    p.artifacts = .35f;
    p.sympathetic = .45f;
    p.bodyBloom = .55f;
    p.closeLevel = .85f;
    p.farLevel = .1667f;
    p.roomType = 0;
    p.piezoLevel = .25f;
    p.stereoWidth = 1.0f;
    p.preampDrive = .12f;
    p.warmth = .1f;
    p.compAmount = .25f;
    p.compAttack = 12.0f;
    p.compRelease = 120.0f;
    p.volume = .8f;
    p.velocityToHardness = p.velocityToPosition = p.velocityToDecay = .1f;
    p.velocityToCarving = p.velocityToCoupling = p.velocityToBloom = p.velocityToSympathetic = .1f;
    p.velocityToTension = p.velocityToArtifacts = .1f;
    return p;
}

Audio render(const EngineParameters& p, const Notes& notes, double seconds) {
    auto engine = std::make_unique<MarembaEngine>(kRate);
    engine->SetParameters(p);
    for (const auto& note : notes) engine->NoteOn(note.first, note.second);
    Audio audio;
    const int frames = static_cast<int>(seconds * kRate);
    for (auto& channel : audio) channel.resize(static_cast<size_t>(frames));
    for (int frame = 0; frame < frames; frame += kBatch) {
        const int count = std::min(kBatch, frames - frame);
        engine->RenderBatch(audio[0].data() + frame, audio[1].data() + frame,
                            audio[2].data() + frame, audio[3].data() + frame,
                            audio[4].data() + frame, audio[5].data() + frame,
                            audio[6].data() + frame, count);
    }
    for (const auto& channel : audio)
        require(std::all_of(channel.begin(), channel.end(), [](float value) { return std::isfinite(value); }),
                "nonfinite output");
    return audio;
}

double energy(const std::vector<float>& signal) {
    double result = 0;
    for (const float value : signal) result += static_cast<double>(value) * value;
    return result;
}

// Sample peak of a group (a stereo pair, or the one Piezo channel), in dBFS.
double peakDb(const Audio& audio, int first, int channels) {
    double peak = 0;
    for (int c = first; c < first + channels; ++c)
        for (const float value : audio[static_cast<size_t>(c)]) peak = std::max(peak, std::fabs(static_cast<double>(value)));
    return 20.0 * std::log10(std::max(peak, 1e-12));
}

// A full-velocity eight-note chord, C3 to E5: the loudest ordinary playing, and
// the stress case the direct outputs' peak protection is sized against.
const Notes kChord = {{48, 1.f}, {52, 1.f}, {55, 1.f}, {60, 1.f}, {64, 1.f}, {67, 1.f}, {72, 1.f}, {76, 1.f}};
const Notes kMezzoNote = {{60, 80.f / 127.f}};

// Volume, the Level coefficients, Width, Warmth and Limit shape the Main mix.
// A direct output is the same audio whatever they are, to the last bit.
void independenceContracts() {
    for (int model = 0; model < 4; ++model) {
        const EngineParameters base = defaultPatch(model);
        const Audio reference = render(base, kChord, 1.0);
        for (int channel = kClose; channel <= kPiezo; ++channel)
            require(energy(reference[static_cast<size_t>(channel)]) > 1e-6, "reference chord leaves a direct output silent");

        struct Variant { const char* name; void (*apply)(EngineParameters&); };
        const Variant variants[] = {
            {"volume 0.3", [](EngineParameters& p) { p.volume = .3f; }},
            {"volume 1", [](EngineParameters& p) { p.volume = 1.f; }},
            {"levels 0", [](EngineParameters& p) { p.closeLevel = p.farLevel = p.piezoLevel = 0.f; }},
            {"levels 1", [](EngineParameters& p) { p.closeLevel = p.farLevel = p.piezoLevel = 1.f; }},
            {"width 0", [](EngineParameters& p) { p.stereoWidth = 0.f; }},
            {"width 2", [](EngineParameters& p) { p.stereoWidth = 2.f; }},
            {"warmth -1", [](EngineParameters& p) { p.warmth = -1.f; }},
            {"warmth 1", [](EngineParameters& p) { p.warmth = 1.f; }},
            {"limit 0", [](EngineParameters& p) { p.compAmount = 0.f; }},
            {"limit 1", [](EngineParameters& p) { p.compAmount = 1.f; }},
        };
        for (const Variant& variant : variants) {
            context = std::string("independence model=") + std::to_string(model) + " " + variant.name;
            EngineParameters p = base;
            variant.apply(p);
            const Audio changed = render(p, kChord, 1.0);
            for (int channel = kClose; channel <= kPiezo; ++channel)
                require(changed[static_cast<size_t>(channel)] == reference[static_cast<size_t>(channel)],
                        "a Main-mix control changes a direct output");
        }

        // Volume is the one control that silences Main outright; the stems play on.
        context = "independence model=" + std::to_string(model) + " volume 0";
        EngineParameters silent = base;
        silent.volume = 0.f;
        const Audio muted = render(silent, kChord, 1.0);
        require(energy(muted[kMain]) == 0 && energy(muted[kMain + 1]) == 0, "volume 0 leaves Main audible");
        for (int channel = kClose; channel <= kPiezo; ++channel)
            require(muted[static_cast<size_t>(channel)] == reference[static_cast<size_t>(channel)],
                    "volume 0 changes a direct output");
    }
}

// Every stem has the headroom of a full-velocity chord to itself, and a
// mezzo-forte note is still a healthy signal on each, on every instrument. Room
// and Piezo sit no lower than Close on the same playing.
void calibrationContracts() {
    const double ceiling = 20.0 * std::log10(static_cast<double>(OutputHeadroom::kCeiling)); // -0.54 dBFS
    double lowest = 0, spreadWorst = 0;
    for (int model = 0; model < 4; ++model) {
        context = "calibration model=" + std::to_string(model);
        const EngineParameters p = defaultPatch(model);
        const Audio chord = render(p, kChord, 1.5);
        const Audio note = render(p, kMezzoNote, 1.5);
        const double chordPeak[3] = {peakDb(chord, kClose, 2), peakDb(chord, kRoom, 2), peakDb(chord, kPiezo, 1)};
        const double notePeak[3] = {peakDb(note, kClose, 2), peakDb(note, kRoom, 2), peakDb(note, kPiezo, 1)};
        const Expectation& expected = kExpected;
        const char* names[3] = {"Close", "Room", "Piezo"};
        for (int stem = 0; stem < 3; ++stem) {
            context = std::string("calibration model=") + std::to_string(model) + " " + names[stem];
            require(chordPeak[stem] <= ceiling + 0.01, "a stem exceeds its protection ceiling");
            require(chordPeak[stem] >= expected.chordFloorDb, "a full-velocity chord leaves the stem far below its ceiling");
            require(notePeak[stem] >= expected.noteFloorDb, "a mezzo-forte note is a very quiet stem");
            lowest = std::min(lowest, chordPeak[stem]);
        }
        const double spread = *std::max_element(chordPeak, chordPeak + 3) - *std::min_element(chordPeak, chordPeak + 3);
        context = "calibration model=" + std::to_string(model) + " spread";
        require(spread <= expected.spreadDb, "the stems' chord peaks are not level with each other");
        spreadWorst = std::max(spreadWorst, spread);
        std::cout << "  model " << model << ": chord peaks Close/Room/Piezo " << chordPeak[0] << " / " << chordPeak[1]
                  << " / " << chordPeak[2] << " dBFS; mezzo-forte note " << notePeak[0] << " / " << notePeak[1]
                  << " / " << notePeak[2] << " dBFS\n";
    }
    std::cout << "  lowest chord peak " << lowest << " dBFS, widest spread " << spreadWorst << " dB\n";
}
// The trims follow Model. Idle or after a reset they are exact; while a note
// rings, a Model change moves them at the 5 ms smoothing rate instead of
// stepping them, in either direction.
void modelSwitchContracts() {
    using Trim = std::array<float, 3>;
    const Trim bars{MarembaEngine::kCloseStemTrim, MarembaEngine::kRoomStemTrim, MarembaEngine::kPiezoStemTrim};
    const Trim kalimba{MarembaEngine::kKalimbaCloseStemTrim, MarembaEngine::kKalimbaRoomStemTrim,
                       MarembaEngine::kKalimbaPiezoStemTrim};
    const auto trim = [](const MarembaEngine& engine) { return MarembaEngineTestAccess::StemTrim(engine); };

    context = "model switch: idle";
    {
        MarembaEngine engine(kRate);
        engine.SetParameters(defaultPatch(0));
        require(trim(engine) == bars, "a bar's trims are not exact at rest");
        engine.SetParameters(defaultPatch(3));
        require(trim(engine) == kalimba, "an idle Model change does not snap to the Kalimba's trims");
        engine.SetParameters(defaultPatch(1));
        require(trim(engine) == bars, "an idle Model change does not snap back to the bars' trims");
        engine.SetParameters(defaultPatch(3));
        engine.Reset();
        require(trim(engine) == kalimba, "Reset does not snap to the current Model's trims");
    }

    for (int from : {0, 3}) {
        const int to = from == 0 ? 3 : 0;
        const Trim& start = from == 0 ? bars : kalimba;
        const Trim& target = from == 0 ? kalimba : bars;
        context = "model switch: " + std::string(from == 0 ? "bar to Kalimba" : "Kalimba to bar") + " while ringing";
        MarembaEngine engine(kRate);
        engine.SetParameters(defaultPatch(from));
        for (const auto& note : kChord) engine.NoteOn(note.first, note.second);
        std::array<std::vector<float>, 7> scratch;
        for (auto& channel : scratch) channel.resize(kBatch);
        const auto render = [&](int frames) {
            engine.RenderBatch(scratch[0].data(), scratch[1].data(), scratch[2].data(), scratch[3].data(),
                               scratch[4].data(), scratch[5].data(), scratch[6].data(), frames);
        };
        for (int block = 0; block < 4; ++block) render(kBatch);
        require(trim(engine) == start, "trims drift while a note rings without a Model change");
        engine.SetParameters(defaultPatch(to));
        require(!engine.IsSilent(), "the switch fixture is not ringing");
        require(trim(engine) == start, "a Model change while a note rings steps the trims at once");
        Trim previous = start;
        double largestStep = 0;
        for (int frame = 0; frame < static_cast<int>(0.2 * kRate); ++frame) {
            render(1);
            const Trim now = trim(engine);
            for (size_t stem = 0; stem < 3; ++stem) {
                const double delta = static_cast<double>(now[stem]) - previous[stem];
                const double direction = static_cast<double>(target[stem]) - start[stem];
                require(delta * direction >= 0, "a trim moves away from its target");
                largestStep = std::max(largestStep, std::fabs(delta));
            }
            previous = now;
        }
        require(largestStep < 0.005, "a trim moves faster than the 5 ms smoothing allows");
        require(largestStep > 0, "the trims never move after a Model change");
        require(previous == target, "the trims do not settle exactly on the new Model's values");
    }

    context = "model switch: bars keep their exact trims";
    {
        MarembaEngine engine(kRate);
        engine.SetParameters(defaultPatch(0));
        for (const auto& note : kChord) engine.NoteOn(note.first, note.second);
        std::array<std::vector<float>, 7> scratch;
        for (auto& channel : scratch) channel.resize(kBatch);
        for (int block = 0; block < static_cast<int>(0.5 * kRate / kBatch); ++block)
            engine.RenderBatch(scratch[0].data(), scratch[1].data(), scratch[2].data(), scratch[3].data(),
                               scratch[4].data(), scratch[5].data(), scratch[6].data(), kBatch);
        require(trim(engine) == bars, "a bar's trims drift while it sounds");
    }
}
} // namespace

int main() {
    independenceContracts();
    calibrationContracts();
    modelSwitchContracts();
    std::cout << "DirectOutputLevelTest PASSED checks=" << checks << '\n';
}
