"""Remove generated mattes non-destructively; keep source frame coordinates intact."""
from pathlib import Path
import json, hashlib
import numpy as np
from PIL import Image, ImageFilter, ImageDraw, ImageFont

ROOT=Path(__file__).resolve().parents[1]

def components(mask):
    # Run-length connected components: avoids dependencies and millions of Python pixel visits.
    parent=[]; runs=[]; previous=[]
    def find(i):
        while parent[i]!=i:
            parent[i]=parent[parent[i]]; i=parent[i]
        return i
    for y,row in enumerate(mask):
        edges=np.diff(np.r_[False,row,False].astype(np.int8))
        starts=np.flatnonzero(edges==1); ends=np.flatnonzero(edges==-1)
        current=[]; p=0
        for x0,x1 in zip(starts,ends):
            i=len(parent);parent.append(i)
            while p<len(previous) and previous[p][1]<=x0:p+=1
            q=p
            while q<len(previous) and previous[q][0]<x1:
                a,b=find(i),find(previous[q][2]);parent[a]=b;q+=1
            current.append((int(x0),int(x1),i));runs.append((y,int(x0),int(x1),i))
        previous=current
    groups={}
    for y,x0,x1,i in runs:groups.setdefault(find(i),[]).append((y,x0,x1))
    return groups.values()

def clean(im,kind):
    rgba=np.array(im.convert('RGBA'));rgb=rgba[:,:,:3].astype(np.int16)
    if kind=='magenta':
        remove=(rgb[:,:,0]>175)&(rgb[:,:,2]>175)&(rgb[:,:,1]<110)&(np.abs(rgb[:,:,0]-rgb[:,:,2])<70)
    else:
        neutral=(rgb.max(2)-rgb.min(2)<19)&(rgb.min(2)>160)
        remove=np.zeros(neutral.shape,bool)
        for runs in components(neutral):
            area=sum(b-a for y,a,b in runs)
            touches=any(y in (0,im.height-1) or a==0 or b==im.width for y,a,b in runs)
            vals=np.concatenate([rgb[y,a:b] for y,a,b in runs])
            bright=int((vals.min(1)>238).sum());gray=int(((vals.min(1)>175)&(vals.max(1)<232)).sum())
            exact=float((vals.max(1)-vals.min(1)<5).mean())
            # Internal openings qualify only if they contain both checker tones and neutral pixels.
            checker=area>=12 and bright>=max(2,area*.04) and gray>=max(2,area*.04) and exact>.7
            if touches or area>2500 or checker:
                for y,a,b in runs:remove[y,a:b]=True
    # Remove matte fringe only immediately adjacent to identified background.
    adjacent=np.array(Image.fromarray(remove).filter(ImageFilter.MaxFilter(3)))&~remove
    if kind=='magenta':
        spill=adjacent&(rgb[:,:,0]>rgb[:,:,1]+32)&(rgb[:,:,2]>rgb[:,:,1]+32)
        rgba[:,:,0][spill]=np.minimum(rgb[:,:,0][spill],rgb[:,:,1][spill]+20)
        rgba[:,:,2][spill]=np.minimum(rgb[:,:,2][spill],rgb[:,:,1][spill]+45)
    else:
        fringe=adjacent&(rgb.max(2)-rgb.min(2)<20)&(rgb.min(2)>110)
        remove|=fringe
    rgba[:,:,3]=np.where(remove,0,255)
    rgba[remove,:3]=0
    return Image.fromarray(rgba),remove

def main():
    manifest=json.loads((ROOT/'manifest.json').read_text())
    audits={x['id']:x for x in json.loads((ROOT/'source-validation.json').read_text())}
    reports=[]
    for item in manifest:
        source_file=item.get('sourceFile',item['file'])
        source=ROOT/source_file
        before=hashlib.sha256(source.read_bytes()).hexdigest()
        im=Image.open(source)
        out,removed=clean(im,audits[item['id']]['backgroundDiagnostic'])
        destination=f"{item['allegiance']}/{item['id']}.png"
        out.save(ROOT/destination)
        check=Image.open(ROOT/destination);alpha=np.array(check.getchannel('A'))
        opaque=alpha==255
        original=np.array(im.convert('RGB'));result=np.array(check)[:,:,:3]
        report={'id':item['id'],'sourceFile':source_file,'file':destination,'size':list(check.size),'mode':check.mode,'transparentPixels':int((alpha==0).sum()),'opaquePixels':int(opaque.sum()),'edgePixelsDecontaminated':int((np.any(original!=result,axis=2)&opaque).sum()),'sourceUnchanged':before==hashlib.sha256(source.read_bytes()).hexdigest(),'sameDimensions':check.size==im.size,'alignment':'unchanged source coordinates; not repaired by background removal'}
        assert report['transparentPixels']>10000 and report['opaquePixels']>10000 and report['sourceUnchanged'] and report['sameDimensions']
        reports.append(report)
        item.update(sourceFile=source_file,file=destination,status='transparent-export; animation-grid-cleanup-still-required')
    (ROOT/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    (ROOT/'review/inventory.js').write_text('const INVENTORY = '+json.dumps(manifest)+';\n')
    (ROOT/'transparency-validation.json').write_text(json.dumps(reports,indent=2)+'\n')
    font=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',16)
    small=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',13)
    for side in ('friendly','enemy'):
        for category in ('ground','flying','spellcaster'):
            items=[x for x in manifest if x['allegiance']==side and x['category']==category]
            sheet=Image.new('RGB',(1000,80+270*((len(items)+3)//4)),'#171d2c');draw=ImageDraw.Draw(sheet)
            draw.text((20,16),f'{side.title()} / {category.title()} — transparent reserve characters',font=font,fill='#e7dfc8')
            draw.text((20,44),'Cleaned PNG exports · original frame spacing retained',font=small,fill='#aab6c5')
            for j,item in enumerate(items):
                im=Image.open(ROOT/item['file']);cell=im.crop((0,0,im.width//6,im.height//6));cell.thumbnail((228,222),Image.Resampling.NEAREST)
                x,y=(j%4)*250,80+(j//4)*270
                sheet.paste(cell,(x+(250-cell.width)//2,y+(224-cell.height)//2),cell)
                draw.text((x+8,y+234),item['name'],font=small,fill='#dce6ed')
            sheet.save(ROOT/'review'/f'{side}-{category}.png')
    print(f'{len(reports)} RGBA exports saved; source originals unchanged.')

if __name__=='__main__':main()
