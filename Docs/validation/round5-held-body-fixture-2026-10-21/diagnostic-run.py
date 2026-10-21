from pathlib import Path
import subprocess,json,concurrent.futures,hashlib,time,datetime
base=Path('/tmp/maremba-rms-hand-damp-20261021/repluck-diagnostic')
def run(name):
 d=base/name
 for n in ('compile.log','run.json'):
  f=d/n
  if f.exists():f.rename(f.with_name('compile-attempt-1-'+n))
 command=['clang++','-std=c++17','-O3','-Wall','-Wextra','-Werror','-I'+str(d/'Source'),'-I'+str(d/'Source/DSP'),'-I'+str(d/'Tests'),str(base/'probe.cpp'),str(d/'Source/DSP/MarembaEngine.cpp'),str(d/'Source/DSP/MarembaVoice.cpp'),'-o',str(d/'probe')]
 start=datetime.datetime.now(datetime.timezone.utc).isoformat();timer=time.monotonic()
 with (d/'compile.log').open('w') as out:r=subprocess.run(command,stdout=out,stderr=subprocess.STDOUT)
 receipt={'command':command,'started_utc':start,'compile_exit_code':r.returncode,'compile_seconds':time.monotonic()-timer}
 if r.returncode==0:
  receipt['binary_sha256']=hashlib.sha256((d/'probe').read_bytes()).hexdigest()
  with (d/'result.txt').open('w') as out:r=subprocess.run([str(d/'probe')],stdout=out,stderr=subprocess.STDOUT)
  receipt['probe_exit_code']=r.returncode
 (d/'run.json').write_text(json.dumps(receipt,indent=2)+'\n')
 return name,receipt
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
 for name,r in pool.map(run,('before','held')):
  print(name,json.dumps(r),flush=True)
  print((base/name/('result.txt' if r['compile_exit_code']==0 else 'compile.log')).read_text(),flush=True)
