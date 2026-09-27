#pragma once

#include "MarimbaModel.h"
#include "KalimbaBody.h"
#include <algorithm>
#include <cmath>

namespace maremba {

// A declared virtual recording scene, in metres. X runs from bass to treble,
// Y points away from the player, and Z points up. The instrument's centre is
// the origin. These are design dimensions, not a survey of a named instrument.
struct CapturePoint { double x, y, z; };
constexpr double kCaptureSpeedOfSound = 343.0; // approximately 20 degrees C
constexpr int kCaptureNotesPerModel = 128;
constexpr int kCaptureFrameSource = 512;
constexpr int kCaptureBoardSource = 513;
constexpr int kCaptureHoleSource = 514;
constexpr int kCaptureSourceCount = 515;

inline double CaptureDistance(CapturePoint a, CapturePoint b) {
    const double x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
    return std::sqrt(x*x + y*y + z*z);
}
constexpr CapturePoint CloseHeadCentre() { return {0.0, -.45, .40}; }
// Listener faces +Y; anatomical left is -X. This is the player's perspective.
constexpr CapturePoint CloseEar(int channel) { return {channel == 0 ? -.09 : .09, -.45, .40}; }
constexpr CapturePoint RoomMicrophone(int channel) { return {channel == 0 ? -.30 : .30, 2.00, .90}; }
constexpr CapturePoint FrameSourcePosition() { return {0.0, 0.0, -.12}; }
constexpr CapturePoint KalimbaBoardSourcePosition() { return {0.0, 0.0, -.025}; }
constexpr CapturePoint KalimbaHoleSourcePosition() { return {0.0, -.025, -.03}; }

inline bool CaptureNaturalKey(int note) {
    const int pc = note % 12;
    return pc != 1 && pc != 3 && pc != 6 && pc != 8 && pc != 10;
}

// Read only face width, without rebuilding every modal shape. The spacing is
// the width of actual generated keys plus a 5 mm gap, in two keyboard rows.
inline double CaptureBarWidth(EMarimbaModel model, int note) {
    const auto* a = BarAnchors(model);
    const double at = (std::clamp(static_cast<double>(note), a[0].midi, a[kBarAnchors-1].midi)
                      - a[0].midi) / (a[1].midi - a[0].midi);
    const int i = std::clamp(static_cast<int>(at), 0, kBarAnchors-2);
    const double t = std::clamp(at-i, 0.0, 1.0);
    return a[i].widthM + t * (a[i+1].widthM-a[i].widthM);
}

inline CapturePoint CaptureSourcePosition(EMarimbaModel model, int midiNote) {
    if (model == EMarimbaModel::KalimbaArtisan) {
        // Seventeen 4.5 mm tines across a 150 mm board: 8 mm centres.
        return {KalimbaTineSide(midiNote) * .064, 0.0, .012};
    }
    // Notes outside this five-octave rack transpose its end keys. They do not
    // move an imaginary microphone, or extrapolate a many-metre keyboard.
    const int note = std::clamp(midiNote, 36, 96);
    double centres[61]{};
    double end = 0.0;
    for (int n = 36; n <= 96; ++n) {
        if (!CaptureNaturalKey(n)) continue;
        const double w = CaptureBarWidth(model, n);
        centres[n-36] = end + .5*w;
        end += w + .005;
    }
    const double span = end - .005;
    for (int n = 37; n < 96; ++n)
        if (!CaptureNaturalKey(n)) centres[n-36] = .5*(centres[n-37]+centres[n-35]);
    const bool natural = CaptureNaturalKey(note);
    return {centres[note-36] - .5*span, natural ? -.025 : .135, natural ? 0.0 : .025};
}

inline int CaptureSourceIndex(EMarimbaModel model, int note) {
    return std::clamp(static_cast<int>(model), 0, 3) * kCaptureNotesPerModel + std::clamp(note, 0, 127);
}
inline CapturePoint CaptureSourcePosition(int index) {
    if (index == kCaptureFrameSource) return FrameSourcePosition();
    if (index == kCaptureBoardSource) return KalimbaBoardSourcePosition();
    if (index == kCaptureHoleSource) return KalimbaHoleSourcePosition();
    index = std::clamp(index, 0, 511);
    return CaptureSourcePosition(static_cast<EMarimbaModel>(index/128), index%128);
}

struct CaptureRoomDefinition { CapturePoint dimensions, origin; };
// The microphones keep their local pose when the room changes. Origin places
// the instrument inside each rectangular reference room (floor at world Z=0).
constexpr CaptureRoomDefinition RoomDefinition(int type) {
    return type == 1 ? CaptureRoomDefinition{{8.0,6.0,3.4},{4.0,2.2,1.0}}
         : type == 2 ? CaptureRoomDefinition{{18.0,12.0,7.0},{9.0,4.0,1.0}}
                     : CaptureRoomDefinition{{6.0,4.5,3.0},{3.0,1.7,1.0}};
}

} // namespace maremba
