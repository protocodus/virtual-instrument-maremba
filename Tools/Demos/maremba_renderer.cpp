#include "DSP/MarembaEngine.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <sstream>
#include <iomanip>
#include <stdexcept>

#pragma pack(push, 1)
struct WavHeader {
    char riff[4] = {'R', 'I', 'F', 'F'};
    uint32_t chunkSize = 0;
    char wave[4] = {'W', 'A', 'V', 'E'};
    char fmt[4] = {'f', 'm', 't', ' '};
    uint32_t fmtSize = 16;
    uint16_t audioFormat = 1; // PCM
    uint16_t numChannels = 2; // Stereo
    uint32_t sampleRate = 44100;
    uint32_t byteRate = 44100 * 2 * 2;
    uint16_t blockAlign = 4;
    uint16_t bitsPerSample = 16;
    char data[4] = {'d', 'a', 't', 'a'};
    uint32_t dataSize = 0;
};
#pragma pack(pop)

struct MidiEvent {
    double timeSec;
    size_t frame = 0;
    int type; // 1 = note_on, 0 = note_off, 2 = controller (1: mod wheel)
    int note; // controller number for type 2
    int velocity; // controller value for type 2
};

int ParseInt(const char* text) {
    size_t consumed = 0;
    const int value = std::stoi(text, &consumed);
    if (text[consumed] != '\0') throw std::runtime_error("Invalid integer");
    return value;
}

double ParseDouble(const char* text) {
    size_t consumed = 0;
    const double value = std::stod(text, &consumed);
    if (text[consumed] != '\0') throw std::runtime_error("Invalid number");
    return value;
}

std::string JsonString(const std::string& value) {
    std::ostringstream out;
    out << '\"';
    for (unsigned char c : value) {
        if (c == '\"' || c == '\\') out << '\\' << c;
        else if (c < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
        else out << c;
    }
    out << '\"';
    return out.str();
}

int main(int argc, char* argv[]) {
    std::string eventsFile = "";
    std::string outputFile = "";
    int model = 0; // 0: Rosewood, 1: Padauk, 2: Balafon, 3: Kalimba
    int mallet = 1; // 0: Yarn/Flesh, 1: Cord/Natural, 2: Rubber/Nail, 3: Wood/Pick, 4: Shaft/Tap
    int oversampling = 0; // 0: 2x, 1: 4x, 2: 8x
    int polyphony = 2; // 24 voices
    double maxDuration = 60.0;
    bool jsonOutput = false;
    float strikePosition = 0.50f;
    bool controlledStrike = false;
    int tuningScale = 0; // 0: Equal, 1: Bala 7-equal, 2: Timbila, 3: Pentatonic 5-equal, 4: Sambla, 5: Gyil, 6: Valimba
    int noteOffMode = 0; // 0: Ring, 1: Damp, 2: Dead
    float carving = 1.0f;         // 0 uncarved .. 1 as built
    float membraneTension = 0.5f; // 0 slack .. 1 tight

    try {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--events" && i + 1 < argc) eventsFile = argv[++i];
        else if (arg == "--output" && i + 1 < argc) outputFile = argv[++i];
        else if (arg == "--model" && i + 1 < argc) model = ParseInt(argv[++i]);
        else if (arg == "--mallet" && i + 1 < argc) mallet = ParseInt(argv[++i]);
        else if (arg == "--oversampling" && i + 1 < argc) oversampling = ParseInt(argv[++i]);
        else if (arg == "--polyphony" && i + 1 < argc) polyphony = ParseInt(argv[++i]);
        else if (arg == "--max-duration" && i + 1 < argc) maxDuration = ParseDouble(argv[++i]);
        else if (arg == "--strike-position" && i + 1 < argc) strikePosition = static_cast<float>(ParseDouble(argv[++i]));
        else if (arg == "--controlled-strike") controlledStrike = true;
        else if (arg == "--scale" && i + 1 < argc) tuningScale = ParseInt(argv[++i]);
        else if (arg == "--note-off" && i + 1 < argc) noteOffMode = ParseInt(argv[++i]);
        else if (arg == "--carving" && i + 1 < argc) carving = static_cast<float>(ParseDouble(argv[++i]));
        else if (arg == "--tension" && i + 1 < argc) membraneTension = static_cast<float>(ParseDouble(argv[++i]));
        else if (arg == "--json") jsonOutput = true;
        else throw std::runtime_error("Unknown argument or missing value: " + arg);
    }
    if (model < 0 || model > 3 || mallet < 0 || mallet > 5 ||
        oversampling < 0 || oversampling > 2 || polyphony < 0 || polyphony > 2 ||
        tuningScale < 0 || tuningScale > 6 || noteOffMode < 0 || noteOffMode > 2 ||
        !std::isfinite(strikePosition) || strikePosition < 0.0f || strikePosition > 1.0f ||
        !std::isfinite(carving) || carving < 0.0f || carving > 1.0f ||
        !std::isfinite(membraneTension) || membraneTension < 0.0f || membraneTension > 1.0f ||
        !std::isfinite(maxDuration) || maxDuration < 0.01 || maxDuration > 3600.0)
        throw std::runtime_error("Model must be 0..3; mallet 0..5; oversampling/polyphony 0..2; scale 0..6; note-off 0..2; "
                                 "carving/tension 0..1; duration 0.01..3600 seconds");
    } catch (const std::exception& error) {
        std::cerr << "Invalid arguments: " << error.what() << "\n";
        return 1;
    }

    if (eventsFile.empty() || outputFile.empty()) {
        std::cerr << "Usage: maremba_renderer --events <file> --output <file.wav> [--model 0|1|2|3] [--mallet 0..5] [--oversampling 0|1|2] [--scale 0..6] [--note-off 0|1|2] [--carving 0..1] [--tension 0..1] [--max-duration <sec>] [--json]\n";
        return 1;
    }

    // 1. Read events
    std::ifstream inFile(eventsFile);
    if (!inFile.is_open()) {
        std::cerr << "Failed to open events file: " << eventsFile << std::endl;
        return 1;
    }

    std::vector<MidiEvent> events;
    std::string line;
    double lastEventTime = 0.0;
    int totalNoteOns = 0;
    int wheelEvents = 0;

    constexpr double kSampleRate = 44100.0;
    // Reserve a real decay tail inside the duration limit. Short regression
    // probes retain their whole event window; musical demos reserve 3 seconds.
    const double tailSec = maxDuration >= 4.0 ? 3.0 : 0.0;
    const double eventCutoff = maxDuration - tailSec;
    size_t lineNumber = 0;
    while (std::getline(inFile, line)) {
        ++lineNumber;
        const auto comment = line.find('#');
        if (comment != std::string::npos) line.resize(comment);
        std::istringstream row(line);
        row >> std::ws;
        if (row.eof()) continue;
        MidiEvent ev{};
        std::string extra;
        if (!(row >> ev.timeSec >> ev.type >> ev.note >> ev.velocity) || (row >> extra) ||
            !std::isfinite(ev.timeSec) || ev.timeSec < 0.0 ||
            (ev.type != 0 && ev.type != 1 && ev.type != 2) || ev.note < 0 || ev.note > 127 ||
            (ev.type == 2 && ev.note != 1) || ev.velocity < 0 || ev.velocity > 127) {
            std::cerr << "Invalid event at line " << lineNumber << "\n";
            return 1;
        }
        if (ev.timeSec < eventCutoff) {
            ev.frame = static_cast<size_t>(std::llround(ev.timeSec * kSampleRate));
            // A time just below the endpoint can round onto its exclusive frame.
            if (ev.frame >= static_cast<size_t>(maxDuration * kSampleRate)) continue;
            if (ev.type == 1 && ev.velocity == 0) ev.type = 0;
            events.push_back(ev);
            lastEventTime = std::max(lastEventTime, ev.timeSec);
            if (ev.type == 1) ++totalNoteOns;
            if (ev.type == 2) ++wheelEvents;
        }
    }
    if (totalNoteOns == 0) {
        std::cerr << "No audible note-on events inside render window\n";
        return 1;
    }
    std::stable_sort(events.begin(), events.end(), [](const MidiEvent& a, const MidiEvent& b) {
        return a.frame < b.frame;
    });
    const double totalAudioDuration = std::min(maxDuration, lastEventTime + 3.0);

    // 2. Initialize Physical Modeling Engine
    maremba::MarembaEngine engine(kSampleRate);

    maremba::EngineParameters params;
    params.model = model;
    params.malletType = mallet;
    params.oversampling = oversampling;
    params.polyphony = polyphony;

    if (model == 0) {
        // Imperial Rosewood 5.0 (Concert Grand Hall)
        params.closeLevel = 0.85f;
        params.farLevel = 0.50f;
        params.piezoLevel = 0.15f;
        params.stereoWidth = 1.05f;
        params.sympathetic = 0.20f;
        params.bodyBloom = 0.25f;
        params.pitchGlide = 0.15f;
        params.decay = 1.00f;
        params.malletHardness = 0.45f;
        params.warmth = 0.05f;
    } else if (model == 1) {
        // Mayan Padauk 4.3 (Artisan Wood Bark)
        params.closeLevel = 0.85f;
        params.farLevel = 0.40f;
        params.piezoLevel = 0.20f;
        params.stereoWidth = 1.00f;
        params.sympathetic = 0.20f;
        params.bodyBloom = 0.25f;
        params.pitchGlide = 0.20f;
        params.decay = 1.00f;
        params.malletHardness = 0.50f;
        params.warmth = 0.05f;
    } else if (model == 2) {
        // Balafon Ancestral (Calabash Resonance)
        params.closeLevel = 0.85f;
        params.farLevel = 0.40f;
        params.piezoLevel = 0.25f;
        params.stereoWidth = 1.00f;
        params.artifacts = 0.30f;
        params.sympathetic = 0.20f;
        params.bodyBloom = 0.25f;
        params.pitchGlide = 0.20f;
        params.decay = 0.95f;
        params.malletHardness = 0.55f;
        params.warmth = 0.05f;
    } else {
        // Kalimba Artisan 17-Key (African Thumb Piano - Cantilever Chime)
        params.closeLevel = 0.85f;
        params.farLevel = 0.35f;
        params.piezoLevel = 0.20f;
        params.stereoWidth = 1.00f;
        params.artifacts = 0.15f;
        params.sympathetic = 0.20f;
        params.bodyBloom = 0.20f;
        params.pitchGlide = 0.05f;
        params.decay = 1.10f;
        params.malletHardness = 0.65f;
        params.warmth = 0.0f;
    }

    // All demo presets use no membrane buzz and only a trace of preamp drive.
    // Set these explicitly after voicing so engine defaults cannot add buzz.
    params.buzzAmount = 0.0f;
    params.preampDrive = 0.01f;
    params.strikePosition = strikePosition;
    params.tuningScale = tuningScale;
    params.noteOffMode = noteOffMode;
    params.carving = carving;
    params.membraneTension = membraneTension;
    params.modWheel = 0.0f;
    if (controlledStrike) {
        params.strikeJitter = 0.0f;
        params.artifacts = 0.0f; // Optional artifact contribution is off; intrinsic touch variation remains.
    }
    params.compAmount = 0.0f; // Pure uncompressed natural acoustic dynamics
    params.compAttack = 10.0f;
    params.compRelease = 120.0f;
    params.volume = 0.85f;

    engine.SetParameters(params);
    engine.Reset();

    // 3. Render audio loop with high-precision benchmarking
    size_t totalFrames = static_cast<size_t>(totalAudioDuration * kSampleRate);
    std::vector<float> audioL(totalFrames, 0.0f);
    std::vector<float> audioR(totalFrames, 0.0f);

    constexpr int kBatchSize = 64;
    float batchL[kBatchSize];
    float batchR[kBatchSize];

    size_t eventIdx = 0;
    size_t framePos = 0;

    auto tStart = std::chrono::steady_clock::now();

    while (framePos < totalFrames) {
        const size_t batchEnd = std::min(framePos + kBatchSize, totalFrames);
        engine.SetParameters(params);
        while (framePos < batchEnd) {
            while (eventIdx < events.size() && events[eventIdx].frame <= framePos) {
                const auto& ev = events[eventIdx++];
                if (ev.type == 1) engine.NoteOn(ev.note, static_cast<float>(ev.velocity) / 127.0f);
                else if (ev.type == 0) engine.NoteOff(ev.note);
                else {
                    // Mod wheel: rolls on the mallet models, hole cover on the Kalimba.
                    params.modWheel = static_cast<float>(ev.velocity) / 127.0f;
                    engine.SetParameters(params);
                }
            }
            // Split the native block at each exact event frame, as the wrapper
            // does. Never pre-trigger a note at the start of its containing block.
            const size_t segmentEnd = eventIdx < events.size()
                ? std::min(batchEnd, events[eventIdx].frame) : batchEnd;
            const int count = static_cast<int>(segmentEnd - framePos);
            engine.RenderBatch(batchL, batchR, nullptr, nullptr, nullptr, nullptr, nullptr, count);
            for (int i = 0; i < count; ++i) {
                audioL[framePos + i] = batchL[i];
                audioR[framePos + i] = batchR[i];
            }
            framePos = segmentEnd;
        }
    }

    auto tEnd = std::chrono::steady_clock::now();
    double renderTimeMs = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
    double renderTimeSec = renderTimeMs / 1000.0;
    double realTimeFactor = (renderTimeSec > 0.0001) ? (totalAudioDuration / renderTimeSec) : 9999.9;

    // 4. Analyze Audio Metrics
    float maxAbs = 0.0f;
    float rawEnginePeak = 0.0f;
    double sumSq = 0.0;
    size_t rawOverRangeSamples = 0;
    const size_t fadeFrames = std::min(totalFrames, static_cast<size_t>(kSampleRate * 0.01));
    for (size_t i = 0; i < totalFrames; ++i) {
        if (!std::isfinite(audioL[i]) || !std::isfinite(audioR[i])) {
            std::cerr << "Non-finite engine output at frame " << i << "\n";
            return 1;
        }
        rawEnginePeak = std::max(rawEnginePeak, std::max(std::abs(audioL[i]), std::abs(audioR[i])));
        rawOverRangeSamples += std::abs(audioL[i]) >= 1.0f;
        rawOverRangeSamples += std::abs(audioR[i]) >= 1.0f;
        // A short endpoint fade avoids a discontinuity if a resonator still rings.
        if (i >= totalFrames - fadeFrames) {
            const float fade = static_cast<float>(totalFrames - 1 - i) / static_cast<float>(fadeFrames - 1);
            audioL[i] *= fade;
            audioR[i] *= fade;
        }
        const float sL = audioL[i];
        const float sR = audioR[i];
        maxAbs = std::max(maxAbs, std::max(std::abs(sL), std::abs(sR)));
        sumSq += sL * sL + sR * sR;
    }

    // Preserve dynamics, but leave -1.5 dBFS headroom for AAC reconstruction peaks.
    // This is a disclosed constant gain reduction, never compression or boosting.
    float normGain = 1.0f;
    if (maxAbs > 0.841395f) {
        normGain = 0.841395f / maxAbs;
    }

    float peakDb = (maxAbs > 1e-5f) ? 20.0f * std::log10(maxAbs * normGain) : -96.0f;
    float rms = static_cast<float>(std::sqrt(sumSq / (totalFrames * 2.0))) * normGain;
    float rmsDb = (rms > 1e-5f) ? 20.0f * std::log10(rms) : -96.0f;

    // 5. Write 16-bit Stereo PCM WAV File
    std::ofstream outFile(outputFile, std::ios::binary);
    if (!outFile.is_open()) {
        std::cerr << "Failed to open output WAV file: " << outputFile << std::endl;
        return 1;
    }

    WavHeader hdr;
    hdr.sampleRate = static_cast<uint32_t>(kSampleRate);
    hdr.byteRate = hdr.sampleRate * hdr.numChannels * (hdr.bitsPerSample / 8);
    hdr.dataSize = static_cast<uint32_t>(totalFrames * hdr.numChannels * sizeof(int16_t));
    hdr.chunkSize = 36 + hdr.dataSize;

    outFile.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));

    std::vector<int16_t> pcmBuffer(totalFrames * 2);
    for (size_t i = 0; i < totalFrames; ++i) {
        float l = std::clamp(audioL[i] * normGain, -1.0f, 1.0f);
        float r = std::clamp(audioR[i] * normGain, -1.0f, 1.0f);
        pcmBuffer[i * 2 + 0] = static_cast<int16_t>(l * 32767.0f);
        pcmBuffer[i * 2 + 1] = static_cast<int16_t>(r * 32767.0f);
    }

    outFile.write(reinterpret_cast<const char*>(pcmBuffer.data()), pcmBuffer.size() * sizeof(int16_t));
    outFile.close();
    if (!outFile) { std::cerr << "Failed writing WAV data\n"; return 1; }

    const char* modelNames[4] = {"Imperial Rosewood 5.0", "Mayan Padauk 4.3", "Balafon Ancestral", "Kalimba Artisan 17-Key"};
    const char* osNames[3] = {"2x Studio", "4x High-Res", "8x Archival"};
    const char* marimbaMalletNames[4] = {"Soft Wool Yarn", "Medium Concert Cord", "Hard Rubber", "Wood Baton"};
    const char* kalimbaThumbNames[4] = {"Thumb Flesh Pad", "Natural Thumb", "Thumbnail Snap", "Thumb Pick"};
    const char* strikerName = (model == 3) ? kalimbaThumbNames[std::clamp(mallet, 0, 3)] : marimbaMalletNames[std::clamp(mallet, 0, 3)];

    if (jsonOutput) {
        std::cout << std::setprecision(10) << "{\n"
                  << "  \"model\": \"" << modelNames[model] << "\",\n"
                  << "  \"model_id\": " << model << ",\n"
                  << "  \"striker\": \"" << strikerName << "\",\n"
                  << "  \"striker_id\": " << mallet << ",\n"
                  << "  \"oversampling\": \"" << osNames[oversampling] << "\",\n"
                  << "  \"audio_duration_sec\": " << totalAudioDuration << ",\n"
                  << "  \"render_time_ms\": " << renderTimeMs << ",\n"
                  << "  \"real_time_factor\": " << realTimeFactor << ",\n"
                  << "  \"throughput_samples_per_sec\": " << static_cast<uint64_t>(totalFrames / renderTimeSec) << ",\n"
                  << "  \"peak_db\": " << peakDb << ",\n"
                  << "  \"rms_db\": " << rmsDb << ",\n"
                  << "  \"sample_rate\": " << kSampleRate << ",\n"
                  << "  \"block_size\": " << kBatchSize << ",\n"
                  << "  \"polyphony\": " << (polyphony == 0 ? 8 : polyphony == 1 ? 16 : 24) << ",\n"
                  << "  \"raw_peak\": " << rawEnginePeak << ",\n"
                  << "  \"attenuation_gain\": " << normGain << ",\n"
                  << "  \"raw_over_range_samples\": " << rawOverRangeSamples << ",\n"
                  << "  \"non_finite_samples\": 0,\n"
                  << "  \"notes_processed\": " << totalNoteOns << ",\n"
                  << "  \"strike_position\": " << strikePosition << ",\n"
                  << "  \"mallet_hardness\": " << params.malletHardness << ",\n"
                  << "  \"strike_jitter\": " << params.strikeJitter << ",\n"
                  << "  \"artifacts\": " << params.artifacts << ",\n"
                  << "  \"buzz_amount\": " << params.buzzAmount << ",\n"
                  << "  \"preamp_drive\": " << params.preampDrive << ",\n"
                  << "  \"tuning_scale\": " << tuningScale << ",\n"
                  << "  \"note_off_mode\": " << noteOffMode << ",\n"
                  << "  \"carving\": " << carving << ",\n"
                  << "  \"membrane_tension\": " << membraneTension << ",\n"
                  << "  \"mod_wheel_events\": " << wheelEvents << ",\n"
                  << "  \"controlled_strike\": " << (controlledStrike ? "true" : "false") << ",\n"
                  << "  \"output_file\": " << JsonString(outputFile) << "\n"
                  << "}\n";
    } else {
        std::cout << "Rendered " << outputFile << "\n"
                  << "  Model:       " << modelNames[model] << "\n"
                  << "  Striker:     " << strikerName << "\n"
                  << "  Oversample:  " << osNames[oversampling] << "\n"
                  << "  Audio Time:  " << totalAudioDuration << " s\n"
                  << "  Render Time: " << renderTimeMs << " ms\n"
                  << "  Speedup:     " << realTimeFactor << "x real-time\n"
                  << "  Peak / RMS:  " << peakDb << " dBFS / " << rmsDb << " dBFS\n"
                  << "  Notes:       " << totalNoteOns << "\n";
    }

    return 0;
}
