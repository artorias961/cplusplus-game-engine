const {chromium}=require('C:/Users/artor/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
const path=require('path');
const {pathToFileURL}=require('url');
const fs=require('fs');
const http=require('http');
(async()=>{
const root=path.resolve(__dirname,'../..');
const server=http.createServer((req,res)=>{
const file=path.resolve(root,'.'+decodeURIComponent(new URL(req.url,'http://localhost').pathname));
if(!file.startsWith(root+path.sep)){res.writeHead(403);res.end();return;}
fs.readFile(file,(error,data)=>{if(error){res.writeHead(404);res.end();return;}
res.setHeader('Content-Type',({'.html':'text/html','.js':'text/javascript','.png':'image/png','.json':'application/json'})[path.extname(file)]||'text/plain');res.end(data);});
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
try {
const browser=await chromium.launch({headless:true,channel:'msedge'});
const page=await browser.newPage({viewport:{width:1280,height:1000}});
const errors=[];page.on('pageerror',e=>errors.push(String(e)));
await page.goto('http://127.0.0.1:'+server.address().port+'/reserve-characters/review/index.html');
await page.waitForTimeout(1200);
const report=await page.evaluate(()=>({cards:document.querySelectorAll('article').length,imagesReady:cards.filter(c=>c.image.complete&&c.image.naturalWidth>0).length,groups:document.querySelectorAll('section[data-group]').length,canvasBefore:cards[0].canvas.toDataURL()}));
await page.waitForTimeout(320);
report.animationChanges=await page.evaluate(old=>cards[0].canvas.toDataURL()!==old,report.canvasBefore);
delete report.canvasBefore;
await page.click('#pause');const before=await page.locator('article canvas').first().evaluate(c=>c.toDataURL());await page.waitForTimeout(250);
report.pauseHolds=await page.locator('article canvas').first().evaluate((c,b)=>c.toDataURL()===b,before);
await page.selectOption('#state',{index:2});await page.click('#pause');await page.click('#replay');await page.waitForTimeout(1200);
const attack=await page.locator('article canvas').first().evaluate(c=>c.toDataURL());await page.waitForTimeout(250);
report.oneShotHolds=await page.locator('article canvas').first().evaluate((c,b)=>c.toDataURL()===b,attack);
report.backdrops=[];
for(const [value,rgb] of [['#263447',[38,52,71]],['#eee7d8',[238,231,216]],['#405348',[64,83,72]]]){
await page.selectOption('#backdrop',value);await page.waitForTimeout(60);
const correct=await page.evaluate(expected=>cards.every(x=>{const p=x.canvas.getContext('2d').getImageData(0,0,1,1).data;return expected.every((v,i)=>p[i]===v)}),rgb);
report.backdrops.push({value,all40CanvasCornersMatch:correct});
}
await page.selectOption('#backdrop','#eee7d8');await page.selectOption('#group','enemy / spellcaster');await page.waitForTimeout(60);
await page.screenshot({path:path.join(__dirname,'preview-light.png'),fullPage:true});
await page.selectOption('#backdrop','#263447');await page.waitForTimeout(60);
await page.selectOption('#group','friendly / ground');await page.screenshot({path:path.join(__dirname,'preview-screenshot.png'),fullPage:true});
report.errors=errors;fs.writeFileSync(path.join(__dirname,'preview-validation.json'),JSON.stringify(report,null,2));
console.log(JSON.stringify(report));await browser.close();
if(errors.length||!report.animationChanges||!report.pauseHolds||!report.oneShotHolds||report.imagesReady!==40||report.backdrops.some(x=>!x.all40CanvasCornersMatch))process.exitCode=1;
} finally {server.close();}
})();
