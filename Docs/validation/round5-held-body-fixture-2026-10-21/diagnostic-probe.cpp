#define main UnusedOriginalRound5Main
#include "Round5ResponseTest.cpp"
#undef main

using EA = maremba::MarembaEngineTestAccess;
using VA = maremba::MarembaVoiceTestAccess;

int main() {
    std::cout << std::setprecision(17);
    const auto p = clean(3);
    const auto ringing = play(p, {on(0, 60, .8f)}, 1.6);
    const auto replucked = play(p, {on(0, 60, .8f), on(1., 60, .8f)}, 1.6);
    const auto fresh = play(p, {on(1., 60, .8f)}, 1.6);
    // Consume exactly the first note-on's engine strike draws, with no old sound.
    MarembaEngine seedAfterFirst(rate);
    seedAfterFirst.SetParameters(p);
    seedAfterFirst.NoteOn(60, .8f);
    const auto freshMatched = play(p, {{1., [&](MarembaEngine& e) {
        EA::CopyStrikeRandom(e, seedAfterFirst); e.NoteOn(60, .8f);
    }}}, 1.6);
    const auto difference = minus(replucked, fresh);
    const auto matchedDifference = minus(replucked, freshMatched);
    const auto freshVariation = minus(freshMatched, fresh);
    const auto emit = [](const char* key, double value) { std::cout << key << '=' << value << '\n'; };
    const double denominator = energy(ringing, 1.05, 1.6);
    const double numerator = energy(difference, 1.05, 1.6);
    emit("counterfactual_ring_energy", denominator);
    emit("original_residual_energy", numerator);
    emit("original_residual_db", db(numerator / denominator));
    emit("fresh_energy", energy(fresh, 1.05, 1.6));
    emit("replucked_energy", energy(replucked, 1.05, 1.6));
    emit("fresh_matched_excitation_energy", energy(freshMatched, 1.05, 1.6));
    emit("matched_excitation_residual_energy", energy(matchedDifference, 1.05, 1.6));
    emit("matched_excitation_residual_db", db(energy(matchedDifference, 1.05, 1.6) / denominator));
    emit("new_pluck_variation_energy", energy(freshVariation, 1.05, 1.6));
    emit("new_pluck_variation_vs_old_ring_db", db(energy(freshVariation, 1.05, 1.6) / denominator));
    emit("original_fundamental_residual_db", 2 * db(line(difference, hz(60), 1.05, 1.6) / line(ringing, hz(60), 1.05, 1.6)));
    emit("matched_fundamental_residual_db", 2 * db(line(matchedDifference, hz(60), 1.05, 1.6) / line(ringing, hz(60), 1.05, 1.6)));
    // Isolate pitched old-state memory, holding all new-pluck excitation,
    // preload and random state identical. Compare after first thumb sample.
    double oldEnergy = 0, freeEnergy = 0, retainedEnergy = 0, residual = 0, preload = 0;
    const auto trace = play(p, {on(0, 60, .8f), {1., [&](MarembaEngine& e) {
        auto counterfactual = EA::Voice(e,60);
        oldEnergy = VA::PitchedEnergy(counterfactual);
        e.NoteOn(60, .8f);
        auto carrying = EA::Voice(e,60);
        auto zeroed = carrying;
        VA::ZeroPitched(zeroed);
        residual = VA::ThumbResidual(carrying);
        float close = 0, far = 0, piezo = 0;
        carrying.ProcessSample(close, far, piezo);
        zeroed.ProcessSample(close, far, piezo);
        counterfactual.ProcessSample(close, far, piezo);
        freeEnergy = VA::PitchedEnergy(counterfactual);
        retainedEnergy = VA::DifferenceEnergy(carrying,zeroed);
        preload = VA::PreloadOffset(carrying)-VA::PreloadOffset(zeroed);
    }}}, 1.01);
    emit("old_pitched_energy_before_thumb",oldEnergy);
    emit("counterfactual_pitched_energy_after_one_sample",freeEnergy);
    emit("retained_pitched_difference_energy_after_one_sample",retainedEnergy);
    emit("pitched_old_memory_suppression_db",db(retainedEnergy/freeEnergy));
    emit("thumb_residual_coefficient",residual);
    emit("thumb_preload_step_difference",preload);
    return 0;
}
