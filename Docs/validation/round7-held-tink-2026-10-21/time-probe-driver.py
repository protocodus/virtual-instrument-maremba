from pathlib import Path
import subprocess,json,hashlib,datetime,time,concurrent.futures,shutil
repo=Path('/Users/vojta/Dev/reason-rack-extension/maremba-rms-reference-20261021-instrument')
base=Path('/tmp/maremba-rms-hand-damp-20261021/round7-tink-time-probe');base.mkdir(exist_ok=True)
source=repo/'Tests/Round7BalafonKalimbaTest.cpp';shutil.copy2(source,base/source.name)
code=r'''#define main UnusedOriginalRound7Main
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
'''
(base/'probe.cpp').write_text(code)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
receipt={'created_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'scope':'One natural-thumb F#4 fixture at48kHz; unchanged original Round7 Render/PeakLine helpers; no full original main or production edits.','original_test_sha256':sha(source),'probe_sha256':sha(base/'probe.cpp'),'variants':{}}
def run(name):
 src=Path('/tmp/maremba-rms-hand-damp-20261021/repluck-diagnostic')/name/'Source'
 cmd=['clang++','-std=c++17','-O3','-Wall','-Wextra','-Werror','-I'+str(src),'-I'+str(src/'DSP'),'-I'+str(base),str(base/'probe.cpp'),str(src/'DSP/MarembaEngine.cpp'),str(src/'DSP/MarembaVoice.cpp'),'-o',str(base/(name+'-probe'))]
 start=datetime.datetime.now(datetime.timezone.utc).isoformat();timer=time.monotonic()
 with (base/(name+'-compile.log')).open('w') as out:r=subprocess.run(cmd,stdout=out,stderr=subprocess.STDOUT)
 rec={'command':cmd,'started_utc':start,'compile_seconds':time.monotonic()-timer,'compile_exit_code':r.returncode,'voice_sha256':sha(src/'DSP/MarembaVoice.cpp')}
 if r.returncode==0:
  rec['binary_sha256']=sha(base/(name+'-probe'))
  with (base/(name+'-result.txt')).open('w') as out:r=subprocess.run([str(base/(name+'-probe'))],stdout=out,stderr=subprocess.STDOUT)
  rec['probe_exit_code']=r.returncode
  rec['observations']={k:float(v) for l in (base/(name+'-result.txt')).read_text().splitlines() for k,v in [l.split('=',1)]}
 return name,rec
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
 for name,r in pool.map(run,('before','held')):
  receipt['variants'][name]=r
  print(name,json.dumps(r),flush=True)
receipt['current_original_test_unchanged']=sha(source)==receipt['original_test_sha256']
(base/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
assert receipt['current_original_test_unchanged']
assert all(r['compile_exit_code']==0 and r['probe_exit_code']==0 for r in receipt['variants'].values())
