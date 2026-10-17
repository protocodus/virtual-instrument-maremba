#!/usr/bin/env python3
"""Compare fresh paired raw engine streams with original OFF/ON deliveries."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path


def sha(path):
    digest=hashlib.sha256()
    with Path(path).open('rb') as source:
        for chunk in iter(lambda:source.read(1024*1024),b''):digest.update(chunk)
    return digest.hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input',type=Path,required=True)
    parser.add_argument('--off-reference',type=Path,required=True)
    parser.add_argument('--on-reference',type=Path,required=True)
    args=parser.parse_args();root=args.input.expanduser().resolve()
    manifest=json.loads((root/'SESSION_MANIFEST.json').read_text())
    references={state:path.expanduser().resolve() for state,path in ((0,args.off_reference),(1,args.on_reference))}
    prior={state:json.loads((path/'SESSION_MANIFEST.json').read_text()) for state,path in references.items()}
    assert len(manifest['tracks'])==12 and manifest['pair_count']==6 and manifest['gentle_widen']==0
    rows=[]
    for track in manifest['tracks']:
        state,base=track['polarity'],track['performance_slug'];reference=references[state]
        original=next(t for t in prior[state]['tracks'] if t['slug']==base)
        current_raw=root/'.build'/f"{track['slug']}.f32";original_raw=reference/'.build'/f'{base}.f32'
        assert track['raw_engine']['gentle_widen']==0
        assert track['raw_engine']['frames']==original['raw_engine']['frames']
        assert track['raw_engine']['sample_rate']==original['raw_engine']['sample_rate']==192000
        assert track['raw_engine']['oversampling_factor']==original['raw_engine']['oversampling_factor']==8
        assert current_raw.stat().st_size==original_raw.stat().st_size==track['raw_engine']['frames']*8
        current_hash,original_hash=sha(current_raw),sha(original_raw)
        assert current_hash==track['raw_sha256']==original_hash,track['slug']
        if 'raw_sha256' in original:assert original_hash==original['raw_sha256']
        music={}
        for suffix in ('.mid','.events'):
            current=root/'Performance'/f'{base}{suffix}';old=reference/'Performance'/f'{base}{suffix}'
            assert current.read_bytes()==old.read_bytes(),current.name
            music[current.name]=sha(current)
        row=dict(slug=track['slug'],original_song_number=track['original_song_number'],title=track['base_title'],
                 invert_piezo=state,gentle_widen=0,reference_delivery=reference.name,frames=track['raw_engine']['frames'],
                 bytes=current_raw.stat().st_size,sha256=current_hash,reference_sha256=original_hash,
                 byte_identical=True,original_performance_sha256=music)
        rows.append(row);print(f"PASS {track['slug']}: complete raw stream and original score byte-identical",flush=True)
    report=dict(status='PASS',scope='Main stereo raw engine streams, before shared delivery gain/resampling/dither',
                sample_rate=192000,oversampling_factor=8,gentle_widen=0,variant_count=12,
                current_manifest_sha256=sha(root/'SESSION_MANIFEST.json'),
                reference_manifests={references[state].name:sha(references[state]/'SESSION_MANIFEST.json') for state in (0,1)},
                method='SHA256 calculated fresh from all 24 complete raw files; sizes/frames/rate/quality and original MIDI/events also checked',
                tracks=rows)
    (root/'RAW_RESTORATION.json').write_text(json.dumps(report,indent=2)+'\n')
    lines=['# Natural stereo raw restoration','',
           'All **12 fresh raw engine renders** (six original songs, Invert Piezo off/on, Gentle Widen off) are byte-identical to their corresponding original natural-stereo deliveries. The complete 192 kHz / 8× Main streams retain the original sound, placement and tails before mastering.','',
           '| Song | OFF / ON raw equality | Frames per state |','|---|---|---:|']
    for row in rows[::2]:lines.append(f"| {row['original_song_number']:02d} {row['title']} | Both byte-identical | {row['frames']:,} |")
    lines += ['', 'The verifier calculates SHA256 fresh from all 24 complete `.f32` files, checks sizes/rate/quality and confirms that original MIDI/event bytes are unchanged. `RAW_RESTORATION.json` records every full hash and the reference manifest hashes.', '',
              'References: original OFF delivery `PresetSessions-20261013` and original inversion-ON delivery `PresetSessions-20261013-InvertPiezo`. This checks the offline Main source render; compatible-host plugin behavior and subjective listening are separate evidence.']
    (root/'RAW_RESTORATION.md').write_text('\n'.join(lines)+'\n')

if __name__=='__main__':main()
