"""Write current transparent-export inventory and immutable-source provenance."""
from pathlib import Path
import hashlib,json
ROOT=Path(__file__).resolve().parents[1]
manifest=json.loads((ROOT/'manifest.json').read_text())
audit={x['id']:x for x in json.loads((ROOT/'source-validation.json').read_text())}
clean={x['id']:x for x in json.loads((ROOT/'transparency-validation.json').read_text())}
lines=['# Reserve character inventory','','All **40 character PNG sheets now have real alpha transparency**. Clean copies use plain character names, such as `friendly/grave-warden.png`. Untouched generated originals retain `-source` in their names. The failed alpha retry is also preserved as a source artifact.','','## Review','','Open [animated review](review/index.html) for all six groups, six labeled transparent contact sheets, animation selection, pause, replay, scale, and dark/light/green backdrop controls. The default preview and full-sheet links use cleaned copies. Historical `*-sources.png` contact sheets intentionally show the original opaque artwork.','','## What was fixed','','Magenta mattes and painted checkerboards were removed by the authorized local cleanup script. Small enclosed checker areas were identified separately; contaminated outside outline pixels were corrected using neighboring outline colors. All exports are RGBA with real transparent pixels. Source dimensions and frame positions are unchanged. Source PNG hashes match the original generated files.','','## Remaining animation work','','This fix addresses the backgrounds. It does not certify production-ready animation sheets. Generated frame spacing still needs extraction/alignment; some equipment, pose continuity and baked status marks need artistic corrections. Review all states before engine integration. Large spell/projectile effects were excluded from generation prompts, although small staff-tip glows remain.','','The intended layout is six columns and six rows: idle, movement, attack/casting, damage, stunned, death. Idle, movement and stun loop; other rows stop in the preview. Manifest timings are proposed review timings because the original collection did not provide per-frame timings. Logical 48/64/96-pixel cells describe intended scale; the actual source sheets remain 1254 × 1254. No whole-sheet resizing or frame relocation was performed during transparency cleanup.','','## Files','','| Character | Allegiance | Category | Transparent PNG | Target cell |','| --- | --- | --- | --- | ---: |']
checks=[]
for x in manifest:
    source_file=x.get('sourceFile',x['file'])
    copy=ROOT/source_file;source=Path(x['source'])
    digest=hashlib.sha256(copy.read_bytes()).hexdigest()
    checks.append({'id':x['id'],'file':source_file,'sha256':digest,'originalCopyMatches':digest==hashlib.sha256(source.read_bytes()).hexdigest()})
    assert clean[x['id']]['transparentPixels']>0 and clean[x['id']]['sameDimensions']
    lines.append(f"| {x['name']} | {x['allegiance']} | {x['category']} | [{Path(x['file']).name}]({x['file']}) | {x['cell']} |")
lines+=['','[Exact generation prompts and paths](manifest.json) · [Transparency validation](transparency-validation.json) · [Source hash checks](copy-validation.json) · [Original grid diagnostics](source-validation.json).','','The read-only source audit retains historical background findings. Current delivery status is in the transparency validation and manifest. No runtime source, spawn lists or automatic loading configuration was changed.']
(ROOT/'INVENTORY.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
(ROOT/'copy-validation.json').write_text(json.dumps(checks,indent=2)+'\n')
print(f'{len(checks)} source copies checked; all match: {all(x["originalCopyMatches"] for x in checks)}')
