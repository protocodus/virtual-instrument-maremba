import hashlib,json,pathlib,re,shlex,subprocess
repo=pathlib.Path(__file__).resolve().parents[3]
evidence=pathlib.Path(__file__).resolve().parent

def snapshot(phase, mode):
    document=json.loads((evidence/(phase+'-'+mode+'-ctest.json')).read_text())
    properties={test['name']:{p['name']:p['value'] for p in test['properties']} for test in document['tests']}
    commands={name:shlex.split(args) for name,args in re.findall(r'^add_test\(\[=\[(.*?)\]=\] (.*)\)$',(evidence/(phase+'-'+mode+'-CTestTestfile.cmake')).read_text(),re.M)}
    assert set(commands)==set(properties)
    return {name:{'command':commands[name],'properties':properties[name]} for name in properties}

result={'kind':'configure-only; no contract executable built or run','modes':{}}
for mode in ('on','off'):
    before=snapshot('before',mode)
    after=snapshot('after',mode)
    assert set(before)==set(after)
    changes=[{'test':name,'before':before[name],'after':after[name]} for name in before if before[name]!=after[name]]
    expected=['Maremba.MainCalibration96000'] if mode=='on' else []
    assert [item['test'] for item in changes]==expected
    if mode=='on':
        expected_entry=json.loads(json.dumps(before[expected[0]]))
        expected_entry['properties']['TIMEOUT']=3600.0
        assert after[expected[0]]==expected_entry
    calibration=[]
    for rate in (44100,48000,96000):
        name='Maremba.MainCalibration'+str(rate)
        entry=after[name]
        assert pathlib.Path(entry['command'][0]).name=='MarembaMainCalibrationTests'
        assert entry['command'][1:]==['--rate',str(rate),'--stress']
        assert entry['properties']['LABELS']==['unit']
        timeout=3600.0 if mode=='on' and rate==96000 else 1800.0
        assert entry['properties']['TIMEOUT']==timeout
        calibration.append({'name':name,**entry})
    result['modes'][mode]={'test_count':len(after),'changed_test_properties':changes,'main_calibration':calibration}

subprocess.run(['git','diff','--exit-code','HEAD','--','Source','Tests','.github/workflows/ci.yml'],cwd=repo,check=True,capture_output=True)
assert not subprocess.check_output(['git','ls-files','--others','--exclude-standard','--','Source','Tests'],cwd=repo,text=True).strip()
pre=json.loads((evidence/'pre-change.json').read_text())
result['source_provenance']={'head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=repo,text=True).strip(),'source_tree':subprocess.check_output(['git','rev-parse','HEAD:Source'],cwd=repo,text=True).strip(),'tests_tree':subprocess.check_output(['git','rev-parse','HEAD:Tests'],cwd=repo,text=True).strip(),'source_tests_workflow_unchanged_from_head':True,'cmake_before_sha256':pre['cmake_before_sha256'],'cmake_after_sha256':hashlib.sha256((repo/'CMakeLists.txt').read_bytes()).hexdigest()}
assert result['source_provenance']['source_tree']==pre['source_tree']
assert result['source_provenance']['tests_tree']==pre['tests_tree']
assert hashlib.sha256((evidence/'job.log').read_bytes()).hexdigest()==json.loads((evidence/'receipt.json').read_text())['raw_log_sha256']
result['retained_failed_job_log_sha256']=hashlib.sha256((evidence/'job.log').read_bytes()).hexdigest()
(evidence/'registration-verification.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({'result':'PASS configure-only registration comparison','on_test_count':result['modes']['on']['test_count'],'off_test_count':result['modes']['off']['test_count'],'only_change':'sanitized Maremba.MainCalibration96000 TIMEOUT 1800 -> 3600','source_tree':result['source_provenance']['source_tree']}))
