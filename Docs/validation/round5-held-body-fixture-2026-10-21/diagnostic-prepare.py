from pathlib import Path
import subprocess,tarfile,io,json,hashlib,datetime
repo=Path('/Users/vojta/Dev/reason-rack-extension/maremba-rms-reference-20261021-instrument')
base=Path('/tmp/maremba-rms-hand-damp-20261021/repluck-diagnostic');base.mkdir(exist_ok=True)
commits={'before':'26debb941face41a036a2f0153f23a28c6a10237','held':'4389cd4fa6d693fb263eb90c7b7ac47e98bab41b'}
receipt={'created_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'variants':{},'scope':'One bounded scratch investigation, original Round5 helpers with renamed unused main; no production edits; all inputs exported from committed trees.'}
for name,commit in commits.items():
 dst=base/name;dst.mkdir(exist_ok=True)
 archive=subprocess.check_output(['git','archive',commit,'Source','Tests/Round5ResponseTest.cpp'],cwd=repo)
 with tarfile.open(fileobj=io.BytesIO(archive)) as tar:tar.extractall(dst)
 inputs={str(p.relative_to(dst)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(dst.rglob('*')) if p.is_file()}
 path=dst/'Tests/Round5ResponseTest.cpp';source=path.read_text();(dst/'Tests/Round5ResponseTest-original.cpp').write_text(source)
 source=source.replace('    static int Hand(','''    static void CopyStrikeRandom(MarembaEngine& dest, const MarembaEngine& src) { dest.mPrng = src.mPrng; }
    static MarembaVoice& Voice(MarembaEngine& e, int note) {
        for (auto& v : e.mVoices) if (v.IsLive() && v.GetNoteNumber() == note) return v;
        std::abort();
    }
    static int Hand(''')
 source=source.replace('    static double ModeT60(','''    static void ZeroPitched(MarembaVoice& v) {
        for (int m = 0; m < v.mNumModes; ++m) v.mModes[m].y1 = v.mModes[m].y2 = 0;
    }
    static double PitchedEnergy(const MarembaVoice& v) {
        double e = 0; for (int m = 0; m < v.mNumModes; ++m) e += MarembaVoice::ModeEnergy(v.mModes[m]); return e;
    }
    static double DifferenceEnergy(const MarembaVoice& a, const MarembaVoice& b) {
        double e = 0;
        for (int m = 0; m < a.mNumModes; ++m) {
            auto d = a.mModes[m]; d.y1 -= b.mModes[m].y1; d.y2 -= b.mModes[m].y2;
            e += MarembaVoice::ModeEnergy(d);
        }
        return e;
    }
    static double ThumbResidual(const MarembaVoice& v) { return v.mThumbResidual; }
    static double PreloadOffset(const MarembaVoice& v) { return v.mPreloadOffset; }
    static double ModeT60(''')
 path.write_text(source)
 receipt['variants'][name]={'commit':commit,'exported_inputs_sha256':inputs,'accessors_only_test_copy_sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
(base/'inputs.json').write_text(json.dumps(receipt,indent=2)+'\n')
