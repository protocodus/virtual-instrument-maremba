#define main UnusedOriginalRound7Main
#include "Round7BalafonKalimbaTest.cpp"
#undef main
#include <iomanip>
int main() {
    std::cout << std::setprecision(17);
    const auto o = Render(Kalimba(1), {{0.0, {66, 0.8f}}}, .4);
    std::vector<double> m(o.l.size());
    double peak = 0;
    for (size_t i = 0; i < m.size(); ++i) { m[i] = o.l[i]+o.r[i]; peak=std::max({peak,std::abs(o.l[i]),std::abs(o.r[i])}); }
    const double f1=440*std::exp2((66-69)/12.0);
    const double ratio=maremba::GetKalimbaMount(f1).ratio[1];
    const auto measure=[&](const char* name, double a, double b) {
        std::cout << name << "_begin_seconds=" << a << '\n' << name << "_end_seconds=" << b << '\n';
        std::cout << name << "_begin_frame=" << static_cast<size_t>(a*kRate) << '\n' << name << "_end_frame=" << static_cast<size_t>(b*kRate) << '\n';
        std::cout << name << "_tink_db=" << 20*std::log10(PeakLine(m,ratio*f1,a,b)/PeakLine(m,f1,a,b)) << '\n';
    };
    measure("historical_recorded",.08,.12);
    measure("normalized_recorded",.7*.08,.7*.12);
    const double output=maremba::OutputHeadroom::LatencyFrames(kRate)/kRate;
    const double flight=maremba::CaptureDistance(maremba::CaptureSourcePosition(maremba::EMarimbaModel::KalimbaArtisan,66),maremba::CloseHeadCentre())/maremba::kCaptureSpeedOfSound;
    const double board=maremba::CaptureDistance(maremba::KalimbaBoardSourcePosition(),maremba::CloseHeadCentre())/maremba::kCaptureSpeedOfSound;
    const double hole=maremba::CaptureDistance(maremba::KalimbaHoleSourcePosition(),maremba::CloseHeadCentre())/maremba::kCaptureSpeedOfSound;
    std::cout << "output_delay_seconds=" << output << '\n' << "tine_flight_seconds=" << flight << '\n' << "board_flight_seconds=" << board << '\n' << "hole_flight_seconds=" << hole << '\n';
    std::cout << "close_stem_peak=" << peak << '\n';
    measure("output_delay_only_adjusted",output+.7*(.08-output),output+.7*(.12-output));
    const double tineDelay=output+flight;
    measure("tine_delay_adjusted_illustration",tineDelay+.7*(.08-tineDelay),tineDelay+.7*(.12-tineDelay));
    return 0;
}
