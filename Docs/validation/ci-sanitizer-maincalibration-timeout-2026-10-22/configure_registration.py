import json,pathlib,subprocess,sys
repo=pathlib.Path('/Users/vojta/Dev/reason-rack-extension/maremba-rms-reference-20261021-instrument')
evidence=repo/'Docs/validation/ci-sanitizer-maincalibration-timeout-2026-10-22'
phase=sys.argv[1]
mode=sys.argv[2]
build=pathlib.Path('/tmp/maremba-ci-timeout-config-20261022')/mode.lower()
cmd=['cmake','-S',str(repo),'-B',str(build),'-G','Unix Makefiles','-DCMAKE_BUILD_TYPE=RelWithDebInfo','-DCMAKE_C_COMPILER=clang','-DCMAKE_CXX_COMPILER=clang++','-DMAREMBA_BUILD_PLUGIN=OFF','-DMAREMBA_BUILD_TOOLS=OFF','-DMAREMBA_BUILD_UNIVERSAL=OFF','-DMAREMBA_ENABLE_SANITIZERS='+mode,'-DCMAKE_DISABLE_FIND_PACKAGE_Python3=TRUE','-DBUILD_TESTING=ON']
show=['ctest','--test-dir',str(build),'--show-only=json-v1']
(evidence/(phase+'-'+mode.lower()+'-commands.json')).write_text(json.dumps([cmd,show],indent=2)+'\n')
with (evidence/(phase+'-'+mode.lower()+'-configure.log')).open('w') as output:
    result=subprocess.run(cmd,stdout=output,stderr=subprocess.STDOUT)
if result.returncode: raise SystemExit(result.returncode)
shown=subprocess.run(show,capture_output=True,text=True)
(evidence/(phase+'-'+mode.lower()+'-ctest.json')).write_text(shown.stdout)
(evidence/(phase+'-'+mode.lower()+'-ctest.stderr')).write_text(shown.stderr)
(evidence/(phase+'-'+mode.lower()+'-CTestTestfile.cmake')).write_bytes((build/'CTestTestfile.cmake').read_bytes())
if shown.returncode: raise SystemExit(shown.returncode)
print(json.dumps({'phase':phase,'sanitizers':mode,'configure_exit':result.returncode,'ctest_show_only_exit':shown.returncode,'test_count':len(json.loads(shown.stdout)['tests'])}))
