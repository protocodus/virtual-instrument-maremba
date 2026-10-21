#define main UnusedOriginalRound5Main
#include "Round5ResponseTest.cpp"
#undef main
int main() {
    int failures = 0;
    const auto check = [&](bool ok, const std::string& message) {
        if (!ok) { ++failures; std::cerr << "FAIL " << message << '\n'; }
    };
    std::cout << std::setprecision(17);
    {
        const auto p = clean(3);
        const auto ringing = play(p, {on(0, 60, .8f)}, 1.6);
        // Even Variance 0 retains natural strike-character variation. Match
        // the new pluck's random state so subtraction isolates old-ring memory.
        maremba::FastPRNG secondStrikeRandom;
        const auto replucked = play(p, {on(0, 60, .8f), {1.0, [&](MarembaEngine& e) {
            secondStrikeRandom = maremba::MarembaEngineTestAccess::StrikeRandom(e);
            e.NoteOn(60, .8f);
        }}}, 1.6);
        const auto fresh = play(p, {{1.0, [&](MarembaEngine& e) {
            maremba::MarembaEngineTestAccess::SetStrikeRandom(e, secondStrikeRandom);
            e.NoteOn(60, .8f);
        }}}, 1.6);
        const double residual = db(energy(minus(replucked, fresh), 1.05, 1.6) / energy(ringing, 1.05, 1.6));
        std::cout << "REPLUCK kalimba old_ring_left_db=" << residual << '\n';
        check(residual < -25, "a re-plucked tine keeps its old ring");
    }
    return failures ? 1 : 0;
}
