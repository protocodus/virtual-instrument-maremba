from pathlib import Path
import subprocess,json,hashlib,datetime,time,concurrent.futures,tarfile,io
repo=Path('/Users/vojta/Dev/reason-rack-extension/maremba-rms-reference-20261021-instrument')
base=Path('/tmp/maremba-rms-hand-damp-20261021/variance-age-probe');base.mkdir(exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
commits={'before':'26debb941face41a036a2f0153f23a28c6a10237','held':'80e52773a21046a8d72258ffa98234fa2241bc60'}
watched={str(p.relative_to(repo)):sha(p) for p in sorted((repo/'Source/DSP').rglob('*')) if p.is_file()}
watched['Tests/StrikeVarianceTest.cpp']=sha(repo/'Tests/StrikeVarianceTest.cpp')
receipt={'created_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'scope':'Only Kalimba amounts 0 and 5, uninterrupted48-hit sequences, original fixture helpers. Each rendered clip reused for both observation ages; gain-only controls require no additional renders.','watched_before':watched,'variants':{}}
for name,commit in commits.items():
 d=base/name;d.mkdir(exist_ok=True)
 archive=subprocess.check_output(['git','archive',commit,'Source/DSP','Tests/StrikeVarianceTest.cpp'],cwd=repo)
 with tarfile.open(fileobj=io.BytesIO(archive)) as tar:tar.extractall(d)
 receipt['variants'][name]={'commit':commit,'inputs_sha256':{str(p.relative_to(d)):sha(p) for p in sorted(d.rglob('*')) if p.is_file()}}
 assert (d/'Tests/StrikeVarianceTest.cpp').read_bytes()==(repo/'Tests/StrikeVarianceTest.cpp').read_bytes()
code=r'''#define main UnusedOriginalStrikeVarianceMain
#include "StrikeVarianceTest.cpp"
#undef main
#include <iomanip>

double WindowResidual(const std::vector<float>& a,const std::vector<float>& b,int channels,double scale) {
    double aa=0,ab=0,bb=0;
    for(size_t i=static_cast<size_t>(48000*.04*scale*channels);i<static_cast<size_t>(48000*.16*scale*channels);++i) {
        aa+=double(a[i])*a[i];ab+=double(a[i])*b[i];bb+=double(b[i])*b[i];
    }
    return aa>0&&bb>0?std::sqrt(std::max(0.,1-ab*ab/(aa*bb))):0;
}
double Median(const Hits& hits,int channels,double scale) {
    std::vector<double> values;
    for(size_t h=1;h<hits.size();++h)values.push_back(WindowResidual(hits[h-1],hits[h],channels,scale));
    std::sort(values.begin(),values.end());return values[values.size()/2];
}
int main() {
    std::cout<<std::setprecision(17);
    maremba::EngineParameters p;
    p.model=3;p.malletHardness=.5f;p.strikePosition=.45f;p.decay=.4f;
    p.artifacts=p.detune=p.buzzAmount=p.sympathetic=p.bodyBloom=0;
    p.farLevel=p.piezoLevel=p.preampDrive=p.compAmount=p.warmth=0;
    p.velocityToHardness=p.velocityToPosition=p.velocityToDecay=0;
    maremba::MarembaEngine engine(48000);
    for(float amount:{0.f,5.f}) {
        p.strikeJitter=amount;engine.SetParameters(p);engine.Reset();
        const auto hits=render(engine);
        check(hits.physical.size()==48&&hits.closeStereo.size()==48,"probe lost48-hit sequence");
        for(double scale:{1.,.7}) {
            Hits physicalGain,stereoGain;
            for(size_t h=0;h<48;++h) {
                physicalGain.push_back(hits.physical.front());stereoGain.push_back(hits.closeStereo.front());
                const float gain=.8f+.4f*static_cast<float>(h)/47;
                for(float& v:physicalGain.back())v*=gain;
                for(float& v:stereoGain.back())v*=gain;
            }
            const double physical=Median(hits.physical,1,scale),stereo=Median(hits.closeStereo,2,scale);
            const double physicalNegative=Median(physicalGain,1,scale),stereoNegative=Median(stereoGain,2,scale);
            check(physicalNegative<1.e-6&&stereoNegative<1.e-6,"gain-only control was mistaken for timbre");
            std::cout<<"amount="<<amount<<",scale="<<scale<<",physical_median="<<physical<<",stereo_median="<<stereo
                     <<",gain_only_physical="<<physicalNegative<<",gain_only_stereo="<<stereoNegative
                     <<",begin_frame="<<static_cast<size_t>(48000*.04*scale)<<",end_frame="<<static_cast<size_t>(48000*.16*scale)<<'\n'<<std::flush;
        }
    }
    std::cout<<"failures="<<failures<<",drained_hits="<<drainedHits<<",longest_voice_damp_seconds="<<longestVoiceDampFrames/48000.
             <<",longest_engine_drain_seconds="<<longestEngineDrainFrames/48000.<<'\n';
    return failures?1:0;
}
'''
(base/'probe.cpp').write_text(code)
receipt['probe_sha256']=sha(base/'probe.cpp')
def run(name):
 d=base/name
 cmd=['clang++','-std=c++17','-O3','-Wall','-Wextra','-Werror','-I'+str(d/'Source'),'-I'+str(d/'Source/DSP'),'-I'+str(d/'Tests'),str(base/'probe.cpp'),str(d/'Source/DSP/MarembaEngine.cpp'),str(d/'Source/DSP/MarembaVoice.cpp'),'-o',str(d/'probe')]
 start=datetime.datetime.now(datetime.timezone.utc).isoformat();timer=time.monotonic()
 with (d/'compile.log').open('w') as out:r=subprocess.run(cmd,stdout=out,stderr=subprocess.STDOUT)
 result={'command':cmd,'started_utc':start,'compile_exit_code':r.returncode,'compile_seconds':time.monotonic()-timer}
 if r.returncode==0:
  result['binary_sha256']=sha(d/'probe')
  with (d/'result.txt').open('w') as out:r=subprocess.run([str(d/'probe')],stdout=out,stderr=subprocess.STDOUT)
  result['probe_exit_code']=r.returncode
  result['raw_result_sha256']=sha(d/'result.txt')
  print(name,(d/'result.txt').read_text(),flush=True)
 else:print(name,(d/'compile.log').read_text(),flush=True)
 return name,result
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
 for name,result in pool.map(run,('before','held')):receipt['variants'][name]['run']=result
receipt['watched_after']={p:sha(repo/p) for p in watched}
receipt['watched_unchanged']=receipt['watched_before']==receipt['watched_after']
(base/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
assert receipt['watched_unchanged']
assert all(v['run']['compile_exit_code']==0 and v['run'].get('probe_exit_code')==0 for v in receipt['variants'].values())
