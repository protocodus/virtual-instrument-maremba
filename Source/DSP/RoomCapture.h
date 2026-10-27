#pragma once

#include "AcousticRoom.h"
#include "CaptureGeometry.h"
#include "RoomMicrophone.h"
#include "EarlyReflectionScatter.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace maremba {

// A synthetic recording room, not a measured impulse response. Each input is
// the emitted acoustic pressure of one compact source, referenced to 1 metre.
// Two omnidirectional pressure microphones receive its causal direct wave and
// six first-order wall images. The late diffuse field is a statistical FDN
// approximation, with loss/time/energy derived from the same room and walls.
// It does not reproduce higher-order specular paths, room eigenmodes, source
// directivity or diffraction. The declared pressure-condenser receiver is
// applied after the entire pressure sum. No user capture coefficient belongs here.
class RoomCapture {
public:
    static constexpr int kMaxSources = 520;
    static constexpr int kPaths = 7; // direct, -X,+X,-Y,+Y,floor,ceiling
    static constexpr double kReferenceDistance = 1.0;
    struct Path {
        double distance = 0.0, seconds = 0.0, gain = 0.0;
        int wall = -1;
    };
    using Paths = std::array<Path, kPaths>;

    static Paths Geometry(int type, CapturePoint source, int channel) {
        const auto room = RoomDefinition(std::clamp(type, 0, 2));
        const CapturePoint mic = RoomMicrophone(channel);
        Paths result{};
        const double d = CaptureDistance(source, mic);
        result[0] = {d, d / kCaptureSpeedOfSound, kReferenceDistance / std::max(d, .01), -1};
        for (int wall = 0; wall < 6; ++wall) {
            CapturePoint image = source;
            const int axis = wall / 2;
            const double position = axis == 0 ? source.x : axis == 1 ? source.y : source.z;
            const double origin = axis == 0 ? room.origin.x : axis == 1 ? room.origin.y : room.origin.z;
            const double size = axis == 0 ? room.dimensions.x : axis == 1 ? room.dimensions.y : room.dimensions.z;
            const double reflected = wall % 2 == 0 ? -position - 2.0 * origin
                                                   : 2.0 * (size - origin) - position;
            if (axis == 0) image.x = reflected;
            else if (axis == 1) image.y = reflected;
            else image.z = reflected;
            const double distance = CaptureDistance(image, mic);
            result[static_cast<size_t>(wall + 1)] = {
                distance, distance / kCaptureSpeedOfSound,
                kReferenceDistance / std::max(distance, .01), wall};
        }
        return result;
    }

    // Construction only. All sources retain stable slots, including voices
    // ringing from another model. No memory is resized by Process/Reset/rate
    // changes or room selection. The declared source catalog sizes the history.
    void Allocate(double maxRate, int maxSources = kMaxSources) {
        mMaxRate = std::isfinite(maxRate) ? std::max(8000.0, maxRate) : 192000.0;
        mCapacity = std::clamp(maxSources, 1, kMaxSources);
        mHistorySeconds = .001;
        for (int type = 0; type < 3; ++type) {
            auto& room = mRoom[static_cast<size_t>(type)];
            room.voicing = Voicing(type);
            for (size_t branch = 0; branch < room.tail.size(); ++branch) {
                auto& tail = room.tail[branch];
                tail.memory.assign(tail.Layout(Voicing(type, branch), mMaxRate), 0.0f);
            }
            size_t scatterSamples=0;
            for(int channel=0;channel<2;++channel)for(int wall=0;wall<6;++wall){
                const size_t i=static_cast<size_t>(channel*6+wall);
                room.scatterOffset[i]=scatterSamples;
                scatterSamples+=room.scatter[i].Layout(ScatteringBudget(type,wall,channel),mMaxRate);
            }
            room.scatterMemory.assign(scatterSamples,0.f);
            for (int id = 0; id < kCaptureSourceCount; ++id) {
                const auto source = CaptureSourcePosition(id);
                for (int channel = 0; channel < 2; ++channel) {
                    for (const auto& path : Geometry(type, source, channel))
                        mHistorySeconds = std::max(mHistorySeconds, path.seconds);
                    mHistorySeconds = std::max(mHistorySeconds, LateFeedSeconds(type, source, channel));
                }
            }
        }
        mHistorySeconds += .002; // interpolation/bounds margin, not acoustic delay
        mStride = static_cast<int>(std::ceil(mHistorySeconds * mMaxRate)) + 4;
        mHistory.assign(static_cast<size_t>(mStride) * static_cast<size_t>(mCapacity), 0.0f);
        mSource.resize(static_cast<size_t>(mCapacity));
        SetSampleRate(std::min(48000.0, mMaxRate));
    }

    // Configuration, never per audio sample. False rejects an invalid slot or
    // a position outside the allocated acoustic envelope rather than clipping
    // its propagation delay. The engine configures the entire catalog once.
    bool AddSource(int id, CapturePoint position) {
        if (id < 0 || id >= mCapacity || !std::isfinite(position.x)
                || !std::isfinite(position.y) || !std::isfinite(position.z)) return false;
        for (int type = 0; type < 3; ++type) {
            const auto room = RoomDefinition(type);
            if (position.x+room.origin.x <= 0.0 || position.x+room.origin.x >= room.dimensions.x
                    || position.y+room.origin.y <= 0.0 || position.y+room.origin.y >= room.dimensions.y
                    || position.z+room.origin.z <= 0.0 || position.z+room.origin.z >= room.dimensions.z) return false;
        }
        for (int type = 0; type < 3; ++type) for (int channel = 0; channel < 2; ++channel) {
            if (CaptureDistance(position, RoomMicrophone(channel)) < .01) return false;
            for (const auto& path : Geometry(type, position, channel))
                if (path.seconds >= mHistorySeconds) return false;
            if (LateFeedSeconds(type, position, channel) >= mHistorySeconds) return false;
        }
        auto& source = mSource[static_cast<size_t>(id)];
        source.position = position;
        source.configured = true;
        ConfigureSource(source);
        return true;
    }

    void SetSampleRate(double rate) {
        mRate = std::isfinite(rate) ? std::clamp(rate, 8000.0, mMaxRate) : std::min(48000.0, mMaxRate);
        // The first slot of each source's stride is the guard before its ring
        // (History). The stride is this span at mMaxRate plus one, so the
        // clamp never binds; it only states that the guard is never in it.
        mSpan = std::min(mStride - 1, static_cast<int>(std::ceil(mHistorySeconds * mRate)) + 3);
        mFadeStep = static_cast<float>(1.0 / (.05 * mRate));
        for (auto& microphone : mMicrophone) microphone.SetSampleRate(mRate);
        for (int type = 0; type < 3; ++type) {
            auto& room = mRoom[static_cast<size_t>(type)];
            for (int wall = 0; wall < 6; ++wall)
                room.wall[static_cast<size_t>(wall)] = ReflectionCoefficients(type, wall, mRate);
            for(int channel=0;channel<2;++channel)for(int wall=0;wall<6;++wall)
                room.scatter[static_cast<size_t>(channel*6+wall)].Configure(ScatteringBudget(type,wall,channel),mRate);
            room.voicing = Voicing(type);
            room.voicing.decay = Decay(type, mRate);
            for (size_t branch = 0; branch < room.tail.size(); ++branch) {
                // The diffuse fields have different fixed path lengths, but
                // share one subtle scattering walk for this room. No tap moves.
                const uint32_t seed = 0x726f6f6du ^ (0x9e3779b9u * static_cast<uint32_t>(1 + type));
                auto field = Voicing(type, branch);
                field.decay = room.voicing.decay;
                room.tail[branch].SetSampleRate(field, mRate, true, seed);
            }
            // The image density at late times is4*pi*c*r_ref^2/V. Integrate its
            // exponentially decaying energy beyond two mean free paths. The
            // FDN normalization is the diffuse (uncorrelated-loop) estimate,
            // not a claim of exact pressure or a fitted historical loudness.
            const auto def = RoomDefinition(type);
            const double volume = def.dimensions.x * def.dimensions.y * def.dimensions.z;
            const double kappa = 6.0 * std::log(10.0) / room.voicing.decay.t500;
            double kernelEnergy = 0.0;
            for (size_t branch = 0; branch < room.tail.size(); ++branch) {
                double meanEnergy = 0.0;
                for (double seconds : Voicing(type, branch).lines)
                    meanEnergy += std::exp(-kappa * seconds) / 12.0;
                // The 1/sqrt(2) readout sums decorrelated pressure fields, so
                // their expected energies average rather than add coherently.
                kernelEnergy += .5 * meanEnergy / std::max(1.0 - meanEnergy, 1.e-9);
            }
            const double diffuseEnergy = 4.0 * kPiD * kCaptureSpeedOfSound * kReferenceDistance * kReferenceDistance
                / (volume * kappa) * std::exp(-kappa * 2.0 * MeanFlightSeconds(type));
            room.tailGain = static_cast<float>(std::sqrt(diffuseEnergy / kernelEnergy));
        }
        for (auto& source : mSource) if (source.configured) ConfigureSource(source);
        Reset();
    }

    void SetType(int type) {
        mWanted = std::clamp(type, 0, 2);
        if (mPristine) { mActive = mNext = mWanted; mFade = 1.0f; }
    }
    void Reset(bool resetMotion = true) {
        std::fill(mHistory.begin(), mHistory.end(), 0.0f);
        for (auto& source : mSource) source.remaining = 0;
        mActiveSourceCount = 0; mSourceQueued.fill(false);
        for (int type = 0; type < 3; ++type) ResetRoom(type, resetMotion);
        for (auto& microphone : mMicrophone) microphone.Reset();
        mWrite = 0; mActive = mNext = mWanted; mFade = 1.0f; mPristine = true;
    }

    // Sorted IDs can narrow input discovery to known emitters. Existing image
    // and diffuse tails remain independent of this list and keep rendering.
    void Process(const float* input, int count, float& left, float& right,
                 const int* sourceIds = nullptr, int sourceCount = 0) {
        ProcessPressure(input,count,left,right,sourceIds,sourceCount);
        left=mMicrophone[0].Process(left);right=mMicrophone[1].Process(right);
    }

private:
    // The independent geometry contract observes acoustic pressure before the
    // receiver; the production API always includes its mechanical/electrical
    // response. Capsule group delay is separate from source flight time.
    friend struct RoomCaptureTestAccess;
    void ProcessPressure(const float* input, int count, float& left, float& right,
                         const int* sourceIds = nullptr, int sourceCount = 0) {
        left = right = 0.0f;
        if (mHistory.empty() || mSpan <= 1) return;
        count = input ? std::clamp(count, 0, mCapacity) : 0;
        if (mFade >= 1.0f && mWanted != mActive) {
            mNext = mWanted; ResetRoom(mNext, false); mFade = 0.0f;
        }
        float direct[2] = {}, early[2][2][6] = {}, lateInput[2][2] = {};
        // Scan the contiguous excitation buffer, not515 large path structs.
        // Keep source IDs sorted so summation order remains exactly unchanged.
        bool added = false;
        const int candidates = sourceIds ? std::clamp(sourceCount, 0, count) : count;
        for (int candidate = 0; candidate < candidates; ++candidate) {
            const int id = sourceIds ? sourceIds[candidate] : candidate;
            if (id < 0 || id >= count) continue;
            // A queued source is read below; only a new one needs its input.
            if (mSourceQueued[static_cast<size_t>(id)]) continue;
            const float x = maremba::FlushTiny(RoomExcitation(input[id]));
            if (x == 0.0f) continue;
            if (!mSource[static_cast<size_t>(id)].configured) continue;
            mActiveSource[static_cast<size_t>(mActiveSourceCount++)] = id;
            mSourceQueued[static_cast<size_t>(id)] = true; added = true;
        }
        if (added) std::sort(mActiveSource.begin(), mActiveSource.begin()+mActiveSourceCount);
        int retained = 0;
        for (int index = 0; index < mActiveSourceCount; ++index) {
            const int id = mActiveSource[static_cast<size_t>(index)];
            auto& source = mSource[static_cast<size_t>(id)];
            const float x = id < count ? maremba::FlushTiny(RoomExcitation(input[id])) : 0.0f;
            if (x != 0.0f) { source.remaining = mSpan + 1; mPristine = false; }
            float* history = History(id);
            history[mWrite] = x;
            if (mWrite == mSpan - 1) history[-1] = x;
            for (int channel = 0; channel < 2; ++channel)
                direct[channel] += Read(history, source.direct[static_cast<size_t>(channel)]);
            ProcessSource(source, history, mActive, early[0], lateInput[0]);
            if (mFade < 1.0f) ProcessSource(source, history, mNext, early[1], lateInput[1]);
            if (--source.remaining > 0) mActiveSource[static_cast<size_t>(retained++)] = id;
            else mSourceQueued[static_cast<size_t>(id)] = false;
        }
        mActiveSourceCount = retained;
        float wet[2][2]{};
        ProcessTail(mActive, early[0], lateInput[0], wet[0]);
        if (mFade < 1.0f) {
            ProcessTail(mNext, early[1], lateInput[1], wet[1]);
            for (int channel = 0; channel < 2; ++channel)
                wet[0][channel] += mFade * (wet[1][channel] - wet[0][channel]);
            mFade = std::min(1.0f, mFade + mFadeStep);
            if (mFade >= 1.0f) mActive = mNext;
        }
        left = direct[0] + wet[0][0]; right = direct[1] + wet[0][1];
        if (++mWrite == mSpan) mWrite = 0;
    }

public:
    static double MeanFlightSeconds(int type) {
        const auto d = RoomDefinition(type).dimensions;
        const double volume = d.x * d.y * d.z;
        const double area = 2.0 * (d.x*d.y + d.x*d.z + d.y*d.z);
        return 4.0 * volume / (area * kCaptureSpeedOfSound);
    }
    static DecayProfile Decay(int type, double rate) {
        const auto d = RoomDefinition(type).dimensions;
        const double area[6] = {d.y*d.z,d.y*d.z,d.x*d.z,d.x*d.z,d.x*d.y,d.x*d.y};
        const double total = 2.0 * (d.x*d.y+d.x*d.z+d.y*d.z), volume = d.x*d.y*d.z;
        std::array<double,4> times{};
        constexpr double frequencies[4] = {125.0,500.0,4000.0,10000.0};
        for (int band = 0; band < 4; ++band) {
            double reflectedPower = 0.0;
            for (int wall = 0; wall < 6; ++wall)
                reflectedPower += area[wall] * ReflectionPower(type, wall, frequencies[band], rate) / total;
            times[static_cast<size_t>(band)] = -24.0 * std::log(10.0) * volume
                / (kCaptureSpeedOfSound * total * std::log(std::clamp(reflectedPower, .001, .9999)));
        }
        return {times[0],times[1],times[2],times[3]};
    }
    static double ReflectionPower(int type, int wall, double frequency, double rate) {
        const auto coefficients = ReflectionCoefficients(type, wall, rate);
        const double c = std::cos(kTwoPiD * std::min(frequency,.45*rate) / rate);
        double power = coefficients.gain * coefficients.gain;
        for (const auto& s : coefficients.shelf)
            power *= (s.b0*s.b0+s.b1*s.b1+2.0*s.b0*s.b1*c)
                   / (1.0+s.a1*s.a1+2.0*s.a1*c);
        return power;
    }
    void FlushTiny() {
        // Early-scattering delay buffers are flushed on every write, like
        // FDN delay memory; their only additional state is integer indices.
        for (auto& microphone : mMicrophone) microphone.FlushTiny();
        for (auto& room : mRoom) for (auto& tail : room.tail) tail.FlushStates();
        for (auto& room : mRoom)
            for (auto& channel : room.filter) for (auto& filter : channel)
                for (double& state : filter.state) if (std::abs(state) < 1.e-24) state=0.0;
    }
    // Diagnostic only: scans allocated history. Private FDN filter states are
    // flushed every Process frame; its public delay storage is checked here.
    bool HasNoSubnormalState() const {
        for (const auto& microphone : mMicrophone) if (!microphone.HasNoSubnormalState()) return false;
        const auto normal=[](auto value){return std::fpclassify(value)!=FP_SUBNORMAL;};
        for (float value : mHistory) if (!normal(value)) return false;
        for (const auto& room : mRoom) for (const auto& tail : room.tail)
            for (float value : tail.memory) if (!normal(value)) return false;
        for(const auto& room:mRoom)for(float value:room.scatterMemory)if(!normal(value))return false;
        for (const auto& room : mRoom)
            for (const auto& channel : room.filter) for (const auto& filter : channel)
                for (double value : filter.state) if (!normal(value)) return false;
        return true;
    }
    size_t AllocatedSamples() const { return mHistory.size(); }
    double SampleRate() const { return mRate; }
    double MaximumHistorySeconds() const { return mHistorySeconds; }

private:
    struct Shelf { double b0=1.0,b1=0.0,a1=0.0; };
    struct WallCoefficients { double gain=1.0; std::array<Shelf,3> shelf{}; };
    struct WallState {
        std::array<double,3> state{};
        float Process(float x, const WallCoefficients& c) {
            double y = x * c.gain;
            for (size_t i=0;i<state.size();++i) {
                const double next=c.shelf[i].b0*y+state[i];
                state[i]=c.shelf[i].b1*y-c.shelf[i].a1*next;
                if (std::abs(state[i])<1.e-24) state[i]=0.0;
                y=next;
            }
            return static_cast<float>(y);
        }
    };
    struct Tap { int delay=1; float fraction=0.0f,gain=0.0f; };
    struct SourceRoom {
        std::array<std::array<Tap,6>,2> image{};
        std::array<Tap,2> late{};
    };
    struct Source {
        CapturePoint position{};
        bool configured=false;
        int remaining=0;
        std::array<Tap,2> direct{};
        std::array<SourceRoom,3> room{};
    };
    struct Room {
        NetworkVoicing voicing{};
        std::array<WallCoefficients,6> wall{};
        // All paths reflected by one wall have the same linear material filter.
        // Sum their individually delayed/attenuated pressures first; filtering
        // once commutes with that sum and preserves a wall tail after the last
        // source history empties.
        std::array<std::array<WallState,6>,2> filter{};
        // Two independently timed diffuse fields, excited by the two retained
        // microphone flight paths. Identical FDN poles would duplicate a sparse
        // set of resonances instead of increasing the diffuse modal density.
        std::array<FeedbackNetwork,2> tail;
        std::array<EarlyReflectionScatter,12> scatter;
        std::array<size_t,12> scatterOffset{};
        std::vector<float> scatterMemory;
        float tailGain=1.0f;
        int flush=0;
    };

    static WallCoefficients ReflectionCoefficients(int type, int wall, double rate) {
        // Four broad bands of energy absorption, voiced reference materials.
        // Stronger low/mid absorption prevents struck-bar fundamentals from
        // pooling in the late field. A gentler upper-mid loss retains the airy
        // reflections while the top octave still drains faster. These same
        // materials set first-order reflections, diffuse decay and energy.
        // Floor is less absorbent; ceiling more absorbent than the wall panels.
        constexpr double absorption[3][4]={{.23,.17,.19,.27},{.30,.23,.26,.40},{.19,.135,.155,.225}};
        const int t=std::clamp(type,0,2), w=std::clamp(wall,0,5);
        const double factor=w==4?.80:w==5?1.20:1.0;
        double gain[4];
        for(int i=0;i<4;++i)gain[i]=std::sqrt(1.0-std::clamp(absorption[t][i]*factor,.001,.95));
        WallCoefficients result;result.gain=gain[1];
        const auto lowShelf=[&](double hz,double dc) {
            const double pole=2.0*rate*std::tan(kPiD*std::min(hz,.4*rate)/rate),k=2.0*rate;
            return Shelf{(k+pole*dc)/(k+pole),(pole*dc-k)/(k+pole),(pole-k)/(k+pole)};
        };
        const auto highShelf=[&](double hz,double high) {
            const double pole=2.0*rate*std::tan(kPiD*std::min(hz,.4*rate)/rate),k=2.0*rate;
            return Shelf{(k*high+pole)/(k+pole),(pole-k*high)/(k+pole),(pole-k)/(k+pole)};
        };
        result.shelf={lowShelf(220.0,gain[0]/gain[1]),highShelf(2200.0,gain[2]/gain[1]),highShelf(7500.0,gain[3]/gain[2])};
        return result;
    }
    static double ScatteringBudget(int type,int wall,int channel){
        // Modest5% object/geometry deviation from an empty shoebox. A shared
        // centre-source wall flight is a declared low-cost approximation:
        // each actual image path retains its individual onset and attenuation.
        return .05*Geometry(type,{0.,0.,0.},channel)[static_cast<size_t>(wall+1)].seconds;
    }
    static NetworkVoicing Voicing(int type, size_t branch = 0) {
        // Statistical diffuse paths span several wall flights in the smaller
        // rooms. More total delay memory raises modal density without adding
        // feedback lines; short input diffusers retain the initial buildup.
        constexpr double ratios[12]={.43,.53,.61,.71,.79,.89,1.01,1.13,1.27,1.39,1.53,1.71};
        double mean=0.0;for(double r:ratios)mean+=r/12.0;
        NetworkVoicing result{};const double flight=MeanFlightSeconds(type);
        // Wood needs the densest modal field around sustained bar pitches;
        // Chamber keeps shorter paths for a dense onset in its larger volume.
        // An incommensurate second path ensemble increases modal density using
        // the existing second bank. This is statistical diffusion, not a claim
        // that the twelve lines are measured room eigenmodes. The first field
        // retains the declared late onset; the longer field arrives after it.
        constexpr double secondFieldScale=1.4142135623730951;
        const double loopFlights=type==2?1.0:type==1?2.5:3.5;
        for(size_t i=0;i<result.lines.size();++i)result.lines[i]=loopFlights*flight*ratios[i]/mean;
        if(branch!=0)for(double& seconds:result.lines)seconds*=secondFieldScale;
        result.diffusers={flight*.131,flight*.217,flight*.307};
        result.diffusion=.61;result.decay={1.0,1.0,.7,.4};
        result.loopDiffusion=true;
        return result;
    }
    static double LateFeedSeconds(int type,CapturePoint source,int channel) {
        const auto voicing=Voicing(type);
        const double minimum=*std::min_element(voicing.lines.begin(),voicing.lines.end());
        return CaptureDistance(source,RoomMicrophone(channel))/kCaptureSpeedOfSound
             +2.0*MeanFlightSeconds(type)-minimum;
    }
    Tap MakeTap(double seconds,double gain) const {
        const double samples=seconds*mRate;
        return {std::max(1,static_cast<int>(std::floor(samples))),static_cast<float>(samples-std::floor(samples)),static_cast<float>(gain)};
    }
    void ConfigureSource(Source& source) {
        for(int type=0;type<3;++type)for(int channel=0;channel<2;++channel) {
            const auto geometry=Geometry(type,source.position,channel);
            source.direct[static_cast<size_t>(channel)]=MakeTap(geometry[0].seconds,geometry[0].gain);
            auto& room=source.room[static_cast<size_t>(type)];
            for(int wall=0;wall<6;++wall)
                room.image[static_cast<size_t>(channel)][static_cast<size_t>(wall)]
                    =MakeTap(geometry[static_cast<size_t>(wall+1)].seconds,geometry[static_cast<size_t>(wall+1)].gain);
            room.late[static_cast<size_t>(channel)]=MakeTap(LateFeedSeconds(type,source.position,channel),1.0);
        }
    }
    // Each source's ring is preceded by one guard slot that always holds the
    // ring's last sample (ProcessPressure writes both), so the older of the
    // two interpolated samples needs no second wrap: index -1 reads exactly
    // the value index mSpan - 1 holds.
    float* History(int id) {
        return mHistory.data() + static_cast<size_t>(id) * static_cast<size_t>(mStride) + 1;
    }
    float Read(const float* history,const Tap& tap) const {
        int a=mWrite-tap.delay;if(a<0)a+=mSpan;
        return (history[a]+tap.fraction*(history[a-1]-history[a]))*tap.gain;
    }
    void ProcessSource(Source& source,const float* history,int type,float early[2][6],float* late) {
        auto& paths=source.room[static_cast<size_t>(type)];
        for(int channel=0;channel<2;++channel) {
            for(int wall=0;wall<6;++wall)
                early[channel][wall]+=Read(history,paths.image[static_cast<size_t>(channel)][static_cast<size_t>(wall)]);
            late[channel]+=Read(history,paths.late[static_cast<size_t>(channel)]);
        }
    }
    void ProcessTail(int type,const float early[2][6],const float* input,float* out) {
        auto& room=mRoom[static_cast<size_t>(type)];
        float firstL=0,firstR=0,secondL=0,secondR=0;
        room.tail[0].Process(input[0],firstL,firstR);
        room.tail[1].Process(input[1],secondL,secondR,
                             room.tail[0].ScatteringCoefficients());
        float reflected[2]={};
        for(int channel=0;channel<2;++channel) for(int wall=0;wall<6;++wall){
            const size_t i=static_cast<size_t>(channel*6+wall);
            const float filtered=room.filter[static_cast<size_t>(channel)][static_cast<size_t>(wall)].Process(
                early[channel][wall],room.wall[static_cast<size_t>(wall)]);
            reflected[channel]+=room.scatter[i].Process(filtered,room.scatterMemory.data()+room.scatterOffset[i]);
        }
        constexpr float fieldReadout=.7071067811865475f;
        out[0]=reflected[0]+room.tailGain*fieldReadout*(firstL+secondL);
        out[1]=reflected[1]+room.tailGain*fieldReadout*(firstR+secondR);
        for(auto& tail:room.tail)tail.FlushStates();
    }
    void ResetRoom(int type, bool resetMotion = true) {
        auto& room=mRoom[static_cast<size_t>(type)];
        for(auto& tail:room.tail)tail.Reset(resetMotion);
        room.flush=0;
        if(!room.scatterMemory.empty())for(size_t i=0;i<room.scatter.size();++i)
            room.scatter[i].Reset(room.scatterMemory.data()+room.scatterOffset[i]);
        for(auto& channel:room.filter)for(auto& filter:channel)filter.state.fill(0.0);
    }

    std::vector<float> mHistory;
    std::vector<Source> mSource;
    std::array<int,kMaxSources> mActiveSource{};
    std::array<bool,kMaxSources> mSourceQueued{};
    int mActiveSourceCount=0;
    std::array<Room,3> mRoom;
    std::array<RoomPressureMicrophone,2> mMicrophone;
    double mMaxRate=192000.0,mRate=48000.0,mHistorySeconds=.08;
    int mCapacity=0,mStride=0,mSpan=0,mWrite=0;
    int mWanted=0,mActive=0,mNext=0;
    float mFade=1.0f,mFadeStep=1.0f;
    bool mPristine=true;
};
} // namespace maremba
