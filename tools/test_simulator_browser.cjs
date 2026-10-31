/* Integration smoke test for the local development app; uses isolated headless Edge. */
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const {spawn}=require('node:child_process');
const ROOT=path.resolve(__dirname,'..'),base='http://127.0.0.1:8765';
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
const local=path.join(ROOT,'.local');fs.mkdirSync(local,{recursive:true});
const profile=fs.mkdtempSync(path.join(local,'simulator-browser-'));
const edge=spawn('C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',[
  '--headless=new','--disable-gpu','--no-first-run','--no-default-browser-check','--mute-audio',
  '--disable-background-timer-throttling','--disable-renderer-backgrounding','--disable-backgrounding-occluded-windows',
  '--autoplay-policy=no-user-gesture-required','--remote-debugging-port=0',`--user-data-dir=${profile}`,'about:blank'
],{windowsHide:true,stdio:['ignore','ignore','pipe']});
edge.stderr.on('data',data=>fs.appendFileSync(path.join(local,'simulator-edge.log'),data));
edge.on('exit',(code,signal)=>console.log('Edge exit',code,signal));
let ws,id=0,checks=0;const pending=new Map(),errors=[];
function send(method,params={}){return new Promise((resolve,reject)=>{const key=++id,timer=setTimeout(()=>{pending.delete(key);reject(new Error('CDP timeout: '+method));},15000);pending.set(key,{resolve:value=>{clearTimeout(timer);resolve(value);},reject:error=>{clearTimeout(timer);reject(error);}});ws.send(JSON.stringify({id:key,method,params}));});}
async function value(expression){const result=await send('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});if(result.exceptionDetails)throw new Error(JSON.stringify(result.exceptionDetails));return result.result.value;}
async function until(test,label,timeout=20000){const end=Date.now()+timeout;while(Date.now()<end){if(await test())return;await sleep(120);}throw new Error('Timed out: '+label);}
async function click(selector){await value(`document.querySelector(${JSON.stringify(selector)}).click()`);await sleep(160);}
async function key(name,hold=120){await send('Input.dispatchKeyEvent',{type:'keyDown',key:name,code:name});await sleep(hold);await send('Input.dispatchKeyEvent',{type:'keyUp',key:name,code:name});await sleep(120);}
async function snapshot(){return value('PocketSimulator.snapshot()');}
function check(condition,label){assert(condition,label);++checks;}
async function closePreviousTestBrowsers(){
  for(const name of fs.readdirSync(local)){
    if(!name.startsWith('simulator-browser-')||path.join(local,name)===profile)continue;
    try{
      const record=fs.readFileSync(path.join(local,name,'DevToolsActivePort'),'utf8').trim().split('\n');
      const version=await(await fetch(`http://127.0.0.1:${Number(record[0])}/json/version`,{signal:AbortSignal.timeout(800)})).json();
      // Match the exact isolated browser UUID, not just a possibly reused port.
      if(new URL(version.webSocketDebuggerUrl).pathname!==record[1].trim())continue;
      await new Promise(resolve=>{const client=new WebSocket(version.webSocketDebuggerUrl),timer=setTimeout(()=>{client.close();resolve();},1000);client.onopen=()=>client.send(JSON.stringify({id:1,method:'Browser.close'}));client.onclose=()=>{clearTimeout(timer);resolve();};client.onerror=()=>{clearTimeout(timer);resolve();};});
    }catch{}
  }
}
async function screenshot(name){const shot=await send('Page.captureScreenshot',{format:'png',captureBeyondViewport:true});fs.writeFileSync(path.join(local,name),Buffer.from(shot.data,'base64'));}
async function writeSource(file,data){for(let attempt=0;;++attempt){try{fs.writeFileSync(file,data);return;}catch(error){if(attempt>=80)throw error;await sleep(125);}}}
async function hotReload(){
  const file=path.join(ROOT,'src/main.cpp'),original=fs.readFileSync(file),marker='SOURCE-RELOAD-OK';
  try{
    const changed=original.toString('utf8').replace('拾光 · 随身播放器',marker);check(changed!==original.toString('utf8'),'source marker exists');await writeSource(file,changed);
    await until(()=>value(`PocketSimulator.screenText().includes('${marker}')`),'real source edit reaches screen',90000);++checks;
    await writeSource(file,Buffer.concat([original,Buffer.from('\n#error SIMULATOR_EXPECTED_BUILD_FAILURE\n')]));
    await until(async()=>{const status=await(await fetch(base+'/__build')).json();return status.error.includes('SIMULATOR_EXPECTED_BUILD_FAILURE');},'compiler error surfaced',90000);++checks;
    await until(()=>value(`document.getElementById('build-state').textContent.includes('编译失败')`),'browser compiler error status',45000);++checks;
    check(await value(`PocketSimulator.screenText().includes('${marker}')`),'last successful firmware kept');
  }finally{await writeSource(file,original);}
  await until(()=>value("PocketSimulator.screenText().includes('拾光 · 随身播放器')"),'source restoration rebuild',90000);++checks;
}
(async()=>{
  try{
    await closePreviousTestBrowsers();
    console.log('Starting isolated Edge smoke test');
    const portFile=path.join(profile,'DevToolsActivePort');await until(()=>fs.existsSync(portFile),'Edge start');
    const port=Number(fs.readFileSync(portFile,'utf8').split('\n')[0]);
    const pages=await(await fetch(`http://127.0.0.1:${port}/json`)).json();
    ws=new WebSocket(pages.find(p=>p.type==='page').webSocketDebuggerUrl);
    await new Promise((resolve,reject)=>{ws.onopen=resolve;ws.onerror=reject;});
    ws.onmessage=event=>{const m=JSON.parse(event.data);if(m.id){const p=pending.get(m.id);pending.delete(m.id);if(m.error)p?.reject(m.error);else p?.resolve(m.result);}else if(m.method==='Runtime.exceptionThrown')errors.push(m.params);};
    ws.onclose=()=>{for(const p of pending.values())p.reject(new Error('CDP connection closed'));pending.clear();};
    console.log('Edge connected');
    await send('Runtime.enable');console.log('Runtime enabled');await send('Page.enable');await send('Emulation.setDeviceMetricsOverride',{width:1400,height:1020,deviceScaleFactor:1,mobile:false});
    const fileOnly=process.argv.includes('--file-only');
    await send('Page.navigate',{url:fileOnly?require('node:url').pathToFileURL(path.join(ROOT,'simulator/index.html')).href:base});await until(()=>value("document.body?.dataset.ready==='true'"),'firmware startup');
    await send('Page.bringToFront');
    // Let the initial source-watch poll settle before timing physical key presses.
    if(!fileOnly){await until(()=>value("document.getElementById('build-state').textContent.includes('监视源码')"),'initial build ready');await sleep(1500);}
    if(fileOnly){check((await snapshot()).view===0,'file:// firmware startup');await click('[data-view="Reader"]');check((await snapshot()).lines.length>0,'file:// actual reader');check(errors.length===0,'no file:// browser exceptions');console.log(`Offline file preview: ${checks} assertions PASS`);return;}
    if(process.argv.includes('--hot-reload-only')){await hotReload();check(errors.length===0,'no hot-reload browser exceptions');console.log(`Source hot reload: ${checks} assertions PASS`);return;}
    check((await snapshot()).view===0,'home');await screenshot('simulator-home.png');
    await key('Enter');check((await snapshot()).view===1,'real firmware opens library');await key('Enter');
    await until(async()=>(await snapshot()).audio===1,'browser playback');++checks;
    await until(async()=>(await snapshot()).durationMs>0,'browser media metadata loaded');
    await key('ArrowRight');await until(async()=>(await snapshot()).audioProgress>=35,'right key seeks actual HTML audio');++checks;await key('ArrowLeft');
    await key('ArrowUp');check((await snapshot()).volume===11,'music page adjusts volume');await key('ArrowDown');
    await key('m');check((await snapshot()).view===2,'mode enters book library');await key('Enter');check((await snapshot()).view===4,'book opens');
    await key('ArrowDown');const offset=(await snapshot()).offset;check(offset>0,'page advances using reader.cpp');
    await key('Enter');await key('ArrowDown');await key('Enter');await key('ArrowDown');await key('Enter');check((await snapshot()).font===24,'in-reader font changes');await sleep(900);await screenshot('simulator-reader.png');
    await key('ArrowUp',760);check((await snapshot()).volume===11,'firmware long press adjusts volume');
    await key('Enter');await key('Enter');const startPercent=(await snapshot()).readerPercent;for(let i=startPercent;i<40;++i)await key('ArrowDown');await key('Enter');check((await snapshot()).progress===40,'reader commits 40 percent');const seekOffset=(await snapshot()).offset;await sleep(850);await screenshot('simulator-reader-seek.png');
    await click('#restart-button');check((await snapshot()).view===0,'restart');await click('[data-view="Reader"]');check((await snapshot()).offset===seekOffset,'real NVS bookmark restored');
    await click('[data-view="Settings"]');await key('ArrowDown');await key('ArrowDown');await key('ArrowDown');await key('Enter');check((await snapshot()).paper,'paper theme');await screenshot('simulator-settings.png');
    await value(`(()=>{const text='导入测试\\n这份 UTF-16 小说通过虚拟 SD 卡由固件读取。'.repeat(20);const data=new Uint8Array(2+text.length*2);data[0]=255;data[1]=254;for(let i=0;i<text.length;i++){data[2+i*2]=text.charCodeAt(i)&255;data[3+i*2]=text.charCodeAt(i)>>8;}const dt=new DataTransfer();dt.items.add(new File([data],'test-import-utf16.txt'));const input=document.getElementById('file-input');input.files=dt.files;input.dispatchEvent(new Event('change'));})()`);
    await until(async()=>(await snapshot()).bookCount===3,'UTF16 import rescanned by firmware');++checks;
    await send('Page.reload');await until(()=>value("document.body?.dataset.ready==='true'"),'reload persisted imports');check((await snapshot()).bookCount===3,'IndexedDB file persists');
    await value(`(async()=>{const bytes=await(await fetch('assets/afternoon.mp3')).arrayBuffer();const cover=document.createElement('canvas');cover.width=cover.height=96;const c=cover.getContext('2d'),g=c.createLinearGradient(0,0,96,96);g.addColorStop(0,'#184e70');g.addColorStop(1,'#3898bf');c.fillStyle=g;c.fillRect(0,0,96,96);const coverBytes=Uint8Array.from(atob(cover.toDataURL('image/png').split(',')[1]),c=>c.charCodeAt(0));const data=new DataTransfer();for(const [name,content] of [['Collection/song.mp3',bytes],['Collection/song.lrc','[00:00.00]文件夹歌词\\n[00:10.00]下一句歌词'],['Collection/story.txt','文件夹里的故事。'.repeat(200)],['Collection/song.png',coverBytes]]){const f=new File([content],name.split('/').pop());Object.defineProperty(f,'webkitRelativePath',{value:name});data.items.add(f);}const input=document.getElementById('folder-input');input.files=data.files;input.dispatchEvent(new Event('change'));})()`);
    await until(async()=>(await snapshot()).musicCount===3&&(await snapshot()).bookCount===4,'folder import rescanned');++checks;
    await click('[data-view="Music"]');await key('ArrowDown');await key('ArrowDown');await key('Enter');check((await snapshot()).libraryCount===1,'folder hierarchy preserved');await key('Enter',760);check((await snapshot()).playlistCount===1,'long OK adds playlist');await key('Enter');await until(async()=>(await snapshot()).lyricCount===2,'sidecar lyrics detected');check((await snapshot()).hasCover,'sidecar cover detected');await until(()=>value('PocketSimulator.images().some(i=>i.width===96&&i.height===96)'), 'PNG decoded by browser HAL');await sleep(550);check(await value("(()=>{const p=document.getElementById('screen').getContext('2d').getImageData(240,220,1,1).data;return p[2]>p[0];})()"),'cover pixels reach firmware drawing coordinates');check((await snapshot()).repeatMode===1,'folder loop selected');await screenshot('simulator-player.png');
    await key('Escape');await key('ArrowDown');await key('ArrowDown');await key('ArrowDown');await key('Enter');check((await snapshot()).libraryPlaylist,'playlist opens');await key('Enter');check((await snapshot()).repeatMode===2,'custom playlist loop selected');
    await send('Page.reload');await until(()=>value("document.body?.dataset.ready==='true'"),'folder reload');check((await snapshot()).musicCount===3&&(await snapshot()).playlistCount===1,'folder and playlist persist');
    await click('[data-view="Home"]');
    if(process.argv.includes('--hot-reload'))await hotReload();
    await send('Emulation.setDeviceMetricsOverride',{width:390,height:850,deviceScaleFactor:1,mobile:true});
    check(await value('document.documentElement.scrollWidth<=window.innerWidth'),'mobile has no horizontal overflow');await screenshot('simulator-mobile.png');
    check(errors.length===0,'no browser exceptions: '+JSON.stringify(errors));
    console.log(`Browser simulator: ${checks} integration assertions PASS; screenshots in .local/`);
  }catch(error){try{console.error('Failure state',await snapshot(),await value("document.getElementById('build-state').textContent"),await value('PocketSimulator.logs()'));}catch{}throw error;
  }finally{if(ws?.readyState===WebSocket.OPEN){try{await send('Browser.close');}catch{}}if(ws)ws.close();edge.kill();}
})().catch(error=>{console.error(error);process.exitCode=1;});
