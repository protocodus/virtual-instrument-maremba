// Offline showcase adapter. Every audible sample comes from the current engine.
#include "DSP/MarembaEngine.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

struct Event { uint64_t frame; int type, note, velocity; };

int main(int argc, char** argv) {
  try {
    if (argc != 6) throw std::runtime_error("Usage: render_showcase parameters.txt events.txt output.f32 seconds sample_rate");
    const double duration = std::stod(argv[4]), sampleRate = std::stod(argv[5]);
    if (!std::isfinite(duration) || duration < 1 || duration > 120 || sampleRate != 48000)
      throw std::runtime_error("Invalid duration/sample rate");
    const uint64_t totalFrames = static_cast<uint64_t>(std::llround(duration * sampleRate));
    maremba::EngineParameters params;
    std::map<std::string, float*> floats;
    std::map<std::string, int*> ints;
#define F(name) floats[#name] = &params.name
#define I(name) ints[#name] = &params.name
    I(model); I(malletType); I(roomType); I(oversampling); I(velocityCurve); I(polyphony);
    I(tuningScale); I(noteOffMode);
    F(malletHardness); F(strikePosition); F(resonatorTune); F(resonatorCoupling);
    F(decay); F(buzzAmount); F(artifacts); F(sympathetic); F(pitchGlide); F(bodyBloom);
    F(closeLevel); F(farLevel); F(piezoLevel); F(stereoWidth); F(preampDrive); F(warmth);
    F(compAmount); F(compAttack); F(compRelease); F(volume); F(tuneCents); F(detune);
    F(strikeJitter); F(velocityToHardness); F(velocityToPosition); F(velocityToDecay);
    F(velocityToCarving); F(velocityToCoupling); F(velocityToBloom); F(velocityToSympathetic);
    F(velocityToBuzz); F(velocityToTension); F(velocityToArtifacts);
    F(modWheel); F(carving); F(membraneTension);
#undef F
#undef I
    std::ifstream settings(argv[1]);
    if (!settings) throw std::runtime_error("Cannot open settings");
    std::string line, key;
    size_t count = 0;
    while (std::getline(settings, line)) {
      if (line.empty() || line[0] == '#') continue;
      std::istringstream row(line);
      double value; std::string extra;
      if (!(row >> key >> value) || (row >> extra) || !std::isfinite(value))
        throw std::runtime_error("Invalid setting: " + line);
      if (floats.count(key)) *floats.at(key) = static_cast<float>(value);
      else if (ints.count(key) && value == std::floor(value)) *ints.at(key) = static_cast<int>(value);
      else throw std::runtime_error("Unknown/noninteger setting: " + key);
      ++count;
    }
    if (count != floats.size() + ints.size()) throw std::runtime_error("All engine parameters must be explicit");
    std::ifstream input(argv[2]);
    if (!input) throw std::runtime_error("Cannot open events");
    std::vector<Event> events;
    while (std::getline(input, line)) {
      if (line.empty() || line[0] == '#') continue;
      std::istringstream row(line);
      double seconds; Event event{}; std::string extra;
      if (!(row >> seconds >> event.type >> event.note >> event.velocity) || (row >> extra) ||
          !std::isfinite(seconds) || seconds < 0 || seconds >= duration ||
          (event.type != 0 && event.type != 1) || event.note < 0 || event.note > 127 ||
          event.velocity < 0 || event.velocity > 127) throw std::runtime_error("Invalid event: " + line);
      event.frame = static_cast<uint64_t>(std::llround(seconds * sampleRate));
      if (event.frame >= totalFrames) throw std::runtime_error("Event rounds beyond render");
      events.push_back(event);
    }
    std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) { return a.frame < b.frame; });
    if (events.empty()) throw std::runtime_error("Empty performance");
    std::ofstream output(argv[3], std::ios::binary);
    if (!output) throw std::runtime_error("Cannot open audio output");
    maremba::MarembaEngine engine(sampleRate);
    engine.SetParameters(params);
    engine.Reset();
    size_t index = 0, notes = 0, overRange = 0;
    uint64_t frame = 0;
    double peak = 0, squares = 0;
    float left[64], right[64], interleaved[128];
    while (frame < totalFrames) {
      while (index < events.size() && events[index].frame == frame) {
        const auto& event = events[index++];
        if (event.type && event.velocity) { engine.NoteOn(event.note, event.velocity / 127.0f); ++notes; }
        else engine.NoteOff(event.note);
      }
      const auto end = std::min({frame + 64, totalFrames, index < events.size() ? events[index].frame : totalFrames});
      const int n = static_cast<int>(end - frame);
      engine.RenderBatch(left, right, nullptr, nullptr, nullptr, nullptr, nullptr, n);
      for (int i = 0; i < n; ++i) {
        if (!std::isfinite(left[i]) || !std::isfinite(right[i])) throw std::runtime_error("Nonfinite engine sample");
        for (int channel = 0; channel < 2; ++channel) {
          const float value = channel ? right[i] : left[i];
          peak = std::max(peak, static_cast<double>(std::abs(value)));
          squares += static_cast<double>(value) * value;
          overRange += std::abs(value) >= 1.0f;
          interleaved[2 * i + channel] = value;
        }
      }
      output.write(reinterpret_cast<const char*>(interleaved), n * 2 * sizeof(float));
      frame = end;
    }
    output.close();
    if (!output) throw std::runtime_error("Failed writing float samples");
    std::cout.precision(12);
    std::cout << "{\"frames\":" << totalFrames << ",\"notes\":" << notes
      << ",\"raw_peak\":" << peak << ",\"raw_rms\":" << std::sqrt(squares / (totalFrames * 2))
      << ",\"nonfinite_samples\":0,\"raw_over_range_samples\":" << overRange << "}\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
