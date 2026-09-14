"""Read-only source audit and user-requested labeled review contact sheets."""
import json
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
plan = json.loads((ROOT / 'generation-plan.json').read_text())
font = ImageFont.truetype('C:/Windows/Fonts/arial.ttf', 18)
small = ImageFont.truetype('C:/Windows/Fonts/arial.ttf', 13)
audit = []
for side in ['friendly', 'enemy']:
    for category in ['ground', 'flying', 'spellcaster']:
        roster = [r for r in plan['roster'] if r[1:3] == [side, category]]
        sheet = Image.new('RGB', (1000, 80 + ((len(roster)+3)//4)*270), '#171d2c')
        draw = ImageDraw.Draw(sheet)
        draw.text((20, 16), f'{side.title()} / {category.title()} — reserve source review', font=font, fill='#e7dfc8')
        draw.text((20, 44), 'First source cell shown; generated backgrounds and alignment are not final.', font=small, fill='#aab6c5')
        for j, r in enumerate(roster):
            files = sorted((ROOT/side).glob(r[0]+'-source*.png'))
            x,y = (j%4)*250, 80+(j//4)*270
            draw.text((x+8,y+234), r[0].replace('-',' ').title(), font=small, fill='#dce6ed')
            if not files:
                draw.text((x+10,y+90),'Not generated yet',font=small,fill='#8994a6')
                continue
            im = Image.open(files[-1])
            rgba = im.convert('RGBA')
            alpha = rgba.getchannel('A')
            histogram = alpha.histogram()
            pixels=np.asarray(rgba)
            rgb=pixels[:,:,:3].astype('int16')
            magenta=(rgb[:,:,0]>175)&(rgb[:,:,1]<100)&(rgb[:,:,2]>175)
            matte='magenta' if float(magenta.mean())>.15 else 'painted-checkerboard-or-other'
            if matte=='magenta':
                content=~magenta
            else:
                # Conservative diagnostic only: bright neutral pixels may be scenery matte OR armor.
                # Never use this heuristic to modify source images or certify extraction.
                neutral=(rgb.max(2)-rgb.min(2)<18)&(rgb.min(2)>165)
                content=~neutral
            frame_diagnostics=[]
            for row in range(6):
                for col in range(6):
                    left,top,right,bottom=round(col*im.width/6),round(row*im.height/6),round((col+1)*im.width/6),round((row+1)*im.height/6)
                    region=content[top:bottom,left:right]
                    yy,xx=np.nonzero(region)
                    frame_diagnostics.append({'row':row,'column':col,'sourceRect':[left,top,right-left,bottom-top], 'estimatedContentBounds':None if not len(xx) else [int(xx.min()),int(yy.min()),int(xx.max()+1),int(yy.max()+1)], 'potentialGridContact':bool(region[:2,:].sum()>3 or region[-2:,:].sum()>3 or region[:,:2].sum()>3 or region[:,-2:].sum()>3)})
            audit.append({'id':r[0], 'file':str(files[-1].relative_to(ROOT)).replace('\\','/'), 'size':list(im.size), 'mode':im.mode, 'transparentPixels':histogram[0], 'partialAlphaPixels':sum(histogram[1:255]), 'sixGridDivisible':im.width%6==0 and im.height%6==0, 'requestedCell':r[3], 'exactRequestedDimensions':im.size==(r[3]*6,r[3]*6), 'backgroundDiagnostic':matte,'framesWithPotentialGridContact':sum(f['potentialGridContact'] for f in frame_diagnostics),'frames':frame_diagnostics,'alignmentStatus':'unverified; source rectangles are provisional uniform divisions','diagnosticNote':'Content bounds use color heuristics only. Potential grid contact requires visual review, not automatic acceptance or deletion.'})
            cell = rgba.crop((0,0,im.width//6,im.height//6))
            cell.thumbnail((228,222),Image.Resampling.NEAREST)
            sheet.paste(cell,(x+(250-cell.width)//2,y+(224-cell.height)//2),cell)
        sheet.save(ROOT/'review'/f'{side}-{category}-sources.png')
(ROOT/'source-validation.json').write_text(json.dumps(audit,indent=2)+'\n')
print(f'Audited {len(audit)} generated sources; {sum(x["transparentPixels"]>0 for x in audit)} have transparent pixels.')
