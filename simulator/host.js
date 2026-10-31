/* Hardware/backend host only. UI, navigation, settings and reading run in src/*.cpp. */
(() => {
  'use strict';
  const $=id=>document.getElementById(id),encoder=new TextEncoder();
  const NVS='pocket-firmware-nvs-v1',views=['Home','Music','Books','Player','Reader','Settings'];
  const canvas=$('screen'),ctx=canvas.getContext('2d');ctx.scale(2,2);
  const audio=new Audio();audio.preload='metadata';
  let firmware,binary=globalThis.POCKET_FIRMWARE_BASE64,preferences={},db=null,ready=false;
  let startTime=performance.now(),currentTrack=-1,finished=0,audioState=0,mediaTab='music',generation=0,pendingSeek=null;
  let notificationTimer,saveLabel='设置与书签自动保存在本机',buildVersion=null,buildError='',restarting=false;
  let drawText=[],logs=[],lastSnapshot;
  let tracks=[
    {name:'001-午后信号.mp3',url:'assets/afternoon.mp3',detail:'原创演示 · 00:24',demo:true,kind:'music'},
    {name:'002-晚风来信.mp3',url:'assets/evening.mp3',detail:'原创演示 · 00:24',demo:true,kind:'music'}
  ];
  let extras=[];
  let books=globalThis.POCKET_DEMO_BOOKS.map(book=>({...book,bytes:encoder.encode(book.text),demo:true,kind:'books'}));
  const rgb=value=>`rgb(${Math.round((value>>11&31)*255/31)},${Math.round((value>>5&63)*255/63)},${Math.round((value&31)*255/31)})`;
  function rect(x,y,w,h,color,r,stroke){
    if(x===0&&y===0&&w===240&&h===320&&!stroke)drawText=[];
    ctx.beginPath();ctx.roundRect(x+(stroke?.5:0),y+(stroke?.5:0),Math.max(0,w-(stroke?1:0)),Math.max(0,h-(stroke?1:0)),Math.min(r,Math.max(0,Math.min(w,h)/2)));
    if(stroke){ctx.strokeStyle=rgb(color);ctx.lineWidth=1;ctx.stroke();}else{ctx.fillStyle=rgb(color);ctx.fill();}
  }
  function circle(x,y,r,color,fill){ctx.beginPath();ctx.arc(x,y,r,0,2*Math.PI);if(fill){ctx.fillStyle=rgb(color);ctx.fill();}else{ctx.strokeStyle=rgb(color);ctx.lineWidth=1;ctx.stroke();}}
  function text(value,x,y,font,color,bg){
    drawText.push(value);ctx.textBaseline='top';
    for(const c of value){const ascii=c.codePointAt(0)<128,w=ascii?font/2:font;ctx.fillStyle=rgb(bg);ctx.fillRect(x,y,w,font);ctx.fillStyle=rgb(color);ctx.font=`${font}px ${ascii?'Consolas,monospace':'"Microsoft YaHei","PingFang SC",sans-serif'}`;ctx.fillText(c,x,y+1,w);x+=w;}
  }
  const imageCache=new Map();
  function image(bytes,x,y,w,h,scale){
    let hash=2166136261;for(const byte of bytes)hash=Math.imul(hash^byte,16777619);const key=bytes.length+':'+hash;
    let img=imageCache.get(key);if(!img){img=new Image();img.src=URL.createObjectURL(new Blob([bytes],{type:bytes[0]===137?'image/png':'image/jpeg'}));imageCache.set(key,img);if(imageCache.size>6){const oldest=imageCache.keys().next().value;URL.revokeObjectURL(imageCache.get(oldest).src);imageCache.delete(oldest);}}
    if(img.complete&&img.naturalWidth)ctx.drawImage(img,x,y,Math.min(w,img.naturalWidth*scale),Math.min(h,img.naturalHeight*scale));
  }
  function notify(message){$('notification').textContent=message;$('notification').classList.add('visible');clearTimeout(notificationTimer);notificationTimer=setTimeout(()=>$('notification').classList.remove('visible'),3500);}
  function savePreferences(){
    try{localStorage.setItem(NVS,JSON.stringify(preferences));saveLabel='NVS 已保存 · '+new Date().toLocaleTimeString('zh-CN',{hour12:false});return true;}
    catch{saveLabel='浏览器拒绝保存，记录仅本次有效';return false;}
  }
  function audioStatus(field){
    if(field===0)return audioState;
    if(field===1)return currentTrack;
    if(field===2)return finished;
    if(field===4)return Math.floor(audio.currentTime*1000)||0;
    if(field===5)return Number.isFinite(audio.duration)?Math.floor(audio.duration*1000):0;
    return Number.isFinite(audio.duration)&&audio.duration>0?Math.min(100,Math.floor(audio.currentTime/audio.duration*100)):0;
  }
  function audioCommand(command,path,value){
    logs.push(`AUDIO command=${command} value=${value}`);if(logs.length>200)logs.shift();
    if(command===3){if(!Number.isFinite(audio.duration)||audio.duration<=0)return 0;pendingSeek=Math.min(99,Math.max(0,value));if(audio.readyState>=2){audio.currentTime=audio.duration*pendingSeek/100;pendingSeek=null;}return 1;}
    if(command===2){audio.volume=value/21*.7;return 1;}
    if(command===1){
      if(audioState===1){audio.pause();audioState=2;}
      else if(audioState===2)audio.play().then(()=>{audioState=1;}).catch(()=>notify('浏览器阻止了播放，请再点击一次 OK。'));
      return 1;
    }
    const track=tracks.find(t=>`/music/${t.name}`===path);if(!track)return 0;
    pendingSeek=null;currentTrack=value;audioState=1;const token=++generation;audio.src=track.url;
    audio.play().catch(error=>{
      if(token!==generation)return;
      audioState=error.name==='NotAllowedError'?2:3;
      notify(error.name==='NotAllowedError'?'浏览器需要手动操作，请点击 OK 继续播放。':'该 MP3 无法由浏览器播放。');
    });return 1;
  }
  function stopAudio(){pendingSeek=null;++generation;audio.pause();audio.removeAttribute('src');audio.load();currentTrack=-1;finished=0;audioState=0;}
  audio.addEventListener('loadeddata',()=>{if(pendingSeek!==null&&Number.isFinite(audio.duration)){audio.currentTime=audio.duration*pendingSeek/100;pendingSeek=null;}});
  audio.addEventListener('ended',()=>{++finished;audioState=0;});
  audio.addEventListener('error',()=>{if(currentTrack>=0)audioState=3;});
  function sortMedia(){
    const compare=(a,b)=>{const x=encoder.encode(a.name),y=encoder.encode(b.name);for(let i=0;i<Math.min(x.length,y.length);++i)if(x[i]!==y[i])return x[i]-y[i];return x.length-y.length;};tracks.sort(compare);books.sort(compare);
  }
  function boot(newBinary=binary){
    if(restarting)return;restarting=true;ready=false;
    try{
      if(firmware)firmware.save();stopAudio();for(const timer of releaseTimers.values())clearTimeout(timer);releaseTimers.clear();held.clear();document.querySelectorAll('.device-key').forEach(b=>b.classList.remove('pressed'));
      binary=newBinary;startTime=performance.now();
      firmware=new FirmwareRuntime(binary,{preferences,millis:()=>performance.now()-startTime,clock:()=>{const now=new Date();return now.getHours()*60+now.getMinutes();},rect,circle,text,image,audioCommand,audioStatus,
        savePreferences,log:value=>{logs.push(value);if(logs.length>200)logs.shift();},restart:()=>queueMicrotask(()=>boot())});
      sortMedia();for(const book of books)firmware.mount(`/books/${book.name}`,book.bytes);
      for(const track of tracks)firmware.mount(`/music/${track.name}`,track.head?new Uint8Array(track.head):new Uint8Array([0]));
      for(const item of extras)firmware.mount(`/music/${item.name}`,new Uint8Array(item.bytes));
      firmware.setup();ready=true;$('engine-state').textContent='真实固件 C++ / WASM';document.body.dataset.ready='true';updateStatus();
    }catch(error){$('engine-state').textContent='执行失败';$('engine-state').classList.add('fatal');notify(error.message);console.error(error);}
    finally{restarting=false;}
  }
  function updateStatus(){
    if(!ready)return;const s=firmware.snapshot();lastSnapshot=s;
    $('view-grid').querySelectorAll('button').forEach(b=>{const active=views[s.view]===b.dataset.view;b.classList.toggle('active',active);b.setAttribute('aria-pressed',String(active));});
    $('audio-state').textContent=['尚未播放','♫ 正在播放','Ⅱ 已暂停','无法解码'][s.audio]||'未知';
    $('volume').value=s.volume;$('volume-value').textContent=`${s.volume} / 21`;
    $('byte-state').textContent=s.readerActive?`${s.offset.toLocaleString()} B · ${s.font}px`:'尚未打开书籍';
    $('save-state').textContent=saveLabel;$('device-led').classList.toggle('playing',s.audio===1);
  }
  let previousStatus=0;
  function frame(){
    const now=performance.now();
    if(ready){try{firmware.loop();if(now-previousStatus>=100){previousStatus=now;updateStatus();}}catch(error){ready=false;notify(`固件执行错误：${error.message}`);console.error(error);}}
  }
  function showMedia(){
    $('music-count').textContent=tracks.length;$('books-count').textContent=books.length;
    $('music-tab').setAttribute('aria-selected',String(mediaTab==='music'));$('books-tab').setAttribute('aria-selected',String(mediaTab==='books'));
    const list=$('media-list');list.replaceChildren();
    for(const item of mediaTab==='music'?tracks:books){
      const button=document.createElement('button');button.className='media-item';
      const icon=document.createElement('span');icon.className='media-item-icon';icon.textContent=mediaTab==='music'?'♫':'▤';
      const copy=document.createElement('span');copy.className='media-item-copy';const title=document.createElement('strong');title.textContent=item.name.replace(/^\d+-/,'').replace(/\.(mp3|txt)$/i,'');
      const detail=document.createElement('small');detail.textContent=item.detail||(item.demo?'原创示例 · UTF-8':`本地导入 · ${item.bytes?Math.ceil(item.bytes.length/1024)+' KB':'MP3'}`);
      copy.append(title,detail);const arrow=document.createElement('span');arrow.className='media-item-arrow';arrow.textContent='↗';button.append(icon,copy,arrow);
      button.addEventListener('click',()=>{if(!ready)return;if(item.kind==='music')firmware.e.sim_play_track(tracks.indexOf(item));else firmware.e.sim_open_book(books.indexOf(item));firmware.loop();updateStatus();});list.append(button);
    }
  }
  function openDB(){return new Promise((resolve,reject)=>{const request=indexedDB.open('pocket-simulator-files',1);request.onupgradeneeded=()=>request.result.createObjectStore('files',{keyPath:'id'});request.onsuccess=()=>resolve(request.result);request.onerror=()=>reject(request.error);request.onblocked=()=>reject(new Error('数据库被其他窗口占用'));});}
  function dbOperation(mode,operation){return new Promise((resolve,reject)=>{if(!db){reject(new Error('浏览器未允许保存文件'));return;}const transaction=db.transaction('files',mode);let result;const request=operation(transaction.objectStore('files'));request.onsuccess=()=>{result=request.result;};transaction.oncomplete=()=>resolve(result);transaction.onerror=()=>reject(transaction.error);transaction.onabort=()=>reject(transaction.error);});}
  function addFile(item){if(item.kind==='sidecar'){extras.push({...item,bytes:new Uint8Array(item.bytes)});return;}if(item.kind==='music')tracks.push({...item,url:URL.createObjectURL(item.blob)});else books.push({...item,bytes:new Uint8Array(item.bytes)});}
  async function importFiles(files){
    let count=0,errors=[];
    for(const file of files){try{
      const kind=/\.mp3$/i.test(file.name)?'music':/\.txt$/i.test(file.name)?'books':/\.(lrc|jpe?g|png)$/i.test(file.name)?'sidecar':null;if(!kind)continue;
      const name=file.webkitRelativePath||file.relativePath||file.name;const parts=name.replace(/\\/g,'/').split('/');if(parts.some(p=>!p||p==='.'||p==='..')||parts.length>4)throw new Error('最多支持 3 级子文件夹');
      const items=kind==='music'?tracks:kind==='books'?books:extras;if(kind!=='sidecar'&&items.length>=128)throw new Error('该分类已达到 128 个文件');
      if(items.some(i=>i.name.toLowerCase()===name.toLowerCase()))throw new Error('已有同名文件');
      if(encoder.encode(`/${kind==='books'?'books':'music'}/${name}`).length>=192)throw new Error('文件名过长');if(!file.size)throw new Error('文件为空');
      if(file.size>(kind==='books'?8:128)*1024*1024)throw new Error(kind==='books'?'模拟器单本小说导入上限 8MB':'模拟器单曲导入上限 128MB');
      const item={id:`local:${kind}/${name}`,name,kind};
      if(kind==='books'){
        const raw=new Uint8Array(await file.arrayBuffer());let encoding=$('encoding').value;
        if(raw[0]===0xff&&raw[1]===0xfe)encoding='utf-16le';else if(raw[0]===0xfe&&raw[1]===0xff)encoding='utf-16be';
        const value=new TextDecoder(encoding,{fatal:true}).decode(raw);if(value.includes('\0')||!value.trim())throw new Error('编码错误或内容为空，请检查 TXT 编码');
        item.bytes=encoder.encode(value.replace(/\r\n?/g,'\n'));
      }else if(kind==='music'){item.blob=file;item.head=await file.slice(0,131072).arrayBuffer();}
      else{
        if(file.size>49152)throw new Error('歌词/封面上限 48KB，歌词不超过 16KB');
        item.bytes=await file.arrayBuffer();
        if(/\.lrc$/i.test(name)){
          const raw=new Uint8Array(item.bytes);let encoding=$('encoding').value;
          if(raw[0]===255&&raw[1]===254)encoding='utf-16le';else if(raw[0]===254&&raw[1]===255)encoding='utf-16be';
          item.bytes=encoder.encode(new TextDecoder(encoding,{fatal:true}).decode(raw));if(item.bytes.length>16384)throw new Error('歌词超过 16KB');
        }
      }
      if(db)await dbOperation('readwrite',store=>store.add(item));addFile(item);++count;mediaTab=kind==='books'?'books':'music';
    }catch(error){errors.push(`${file.name}：${error.message}`);}}
    if(count)boot();showMedia();const message=[count?`已导入 ${count} 个文件，固件已重启扫描${db?'':'（本次窗口有效）'}`:'',errors.join('；')].filter(Boolean).join('。');
    $('media-note').textContent=message;notify(message);$('file-input').value='';$('folder-input').value='';
  }
  const held=new Map(),releaseTimers=new Map();
  function press(key,source){
    if(!ready||held.has(source)||$('help-dialog').open)return;
    held.set(source,{key,at:performance.now()});document.querySelector(`[data-key="${key}"]`).classList.add('pressed');firmware.key(key,true);firmware.loop();
  }
  function release(source,cancel=false){
    const record=held.get(source);if(!record)return;held.delete(source);
    const finish=()=>{releaseTimers.delete(source);if(![...held.values()].some(r=>r.key===record.key)){document.querySelector(`[data-key="${record.key}"]`).classList.remove('pressed');if(ready){firmware.key(record.key,false);firmware.loop();}}};
    const remaining=55-(performance.now()-record.at);
    if(cancel||remaining<=0)finish();else releaseTimers.set(source,setTimeout(finish,remaining));
  }
  function tap(key){const source=`tap:${key}:${performance.now()}`;press(key,source);release(source);}
  document.querySelectorAll('[data-key]').forEach(button=>{
    button.addEventListener('pointerdown',e=>{if(e.button!==0)return;e.preventDefault();button.setPointerCapture(e.pointerId);press(Number(button.dataset.key),`pointer:${e.pointerId}`);});
    button.addEventListener('pointerup',e=>release(`pointer:${e.pointerId}`));button.addEventListener('pointercancel',e=>release(`pointer:${e.pointerId}`,true));
    button.addEventListener('click',e=>{if(e.detail===0)tap(Number(button.dataset.key));});button.addEventListener('contextmenu',e=>e.preventDefault());
  });
  const keyMap={ArrowUp:0,ArrowDown:1,ArrowLeft:5,ArrowRight:6,Enter:2,Escape:3,m:4,M:4,u:0,d:1,o:2,b:3};
  window.addEventListener('keydown',e=>{
    if(!ready||e.target.closest?.('input,select,textarea,dialog,[contenteditable="true"]')||$('help-dialog').open||e.ctrlKey||e.metaKey||e.altKey)return;
    if((e.key==='Enter'||e.key===' ')&&e.target instanceof HTMLButtonElement)return;
    if(e.key in keyMap){e.preventDefault();if(!e.repeat)press(keyMap[e.key],`keyboard:${e.code}`);}
    else if(!e.repeat&&(e.key===' '||e.key==='p')){e.preventDefault();firmware.serial('p');}
    else if(!e.repeat&&(e.key==='+'||e.key==='='))firmware.serial('+');else if(!e.repeat&&e.key==='-')firmware.serial('-');
  });
  window.addEventListener('keyup',e=>{if(held.has(`keyboard:${e.code}`)){e.preventDefault();release(`keyboard:${e.code}`);}});
  window.addEventListener('blur',()=>{for(const source of held.keys())release(source,true);if(ready)firmware.save();});
  window.addEventListener('pagehide',()=>{if(ready)firmware.save();});document.addEventListener('visibilitychange',()=>{if(document.hidden&&ready)firmware.save();});
  $('view-grid').addEventListener('click',e=>{const button=e.target.closest('[data-view]');if(button&&ready){firmware.e.sim_navigate(views.indexOf(button.dataset.view));firmware.loop();updateStatus();}});
  $('volume').addEventListener('input',e=>{if(ready)firmware.e.sim_volume(Number(e.target.value));});
  $('music-tab').addEventListener('click',()=>{mediaTab='music';showMedia();});$('books-tab').addEventListener('click',()=>{mediaTab='books';showMedia();});
  $('folder-input').addEventListener('change',e=>importFiles(e.target.files));
  $('file-input').addEventListener('change',e=>importFiles(e.target.files));$('import-zone').addEventListener('dragover',e=>{e.preventDefault();$('import-zone').classList.add('dragging');});$('import-zone').addEventListener('dragleave',()=>$('import-zone').classList.remove('dragging'));$('import-zone').addEventListener('drop',e=>{e.preventDefault();$('import-zone').classList.remove('dragging');importFiles(e.dataTransfer.files);});
  $('restart-button').addEventListener('click',()=>{boot();notify('固件已重启，NVS 中的设置与书签已保留。');});
  $('clear-button').addEventListener('click',async()=>{
    if(!confirm('清除模拟器的导入文件、设置和书签？ESP32 板上的数据不会受到影响。'))return;
    try{if(db)await dbOperation('readwrite',store=>store.clear());}catch(error){notify(`清除失败：${error.message}`);return;}
    if(ready)firmware.save();firmware=null;for(const t of tracks)if(!t.demo)URL.revokeObjectURL(t.url);tracks=tracks.filter(t=>t.demo);books=books.filter(b=>b.demo);extras=[];preferences={};savePreferences();boot();showMedia();notify('已恢复初始演示内容。');
  });
  $('help-button').addEventListener('click',()=>$('help-dialog').showModal());$('close-help').addEventListener('click',()=>$('help-dialog').close());
  $('start-demo').addEventListener('click',()=>{$('help-dialog').close();if(ready){firmware.e.sim_play_track(0);firmware.e.sim_navigate(4);}});
  document.querySelector('.brand').addEventListener('click',e=>{e.preventDefault();if(ready)firmware.e.sim_navigate(0);});
  $('build-state').addEventListener('click',()=>{if(!buildError)return;const dialog=document.createElement('dialog'),pre=document.createElement('pre'),button=document.createElement('button');pre.textContent=buildError;pre.style.cssText='font:11px/1.7 Consolas,monospace;white-space:pre-wrap;overflow-wrap:anywhere';button.textContent='关闭';button.className='primary-button';button.onclick=()=>{dialog.close();dialog.remove();};dialog.append(pre,button);document.body.append(dialog);dialog.showModal();});
  async function pollBuild(){
    if(location.protocol==='file:'){$('build-state').textContent='预构建 · 启动开发模式可自动更新';return;}
    try{
      const status=await(await fetch('/__build',{cache:'no-store'})).json();
      buildError=status.error||'';$('build-state').classList.toggle('fatal',!!buildError);$('build-state').title=buildError;
      $('build-state').textContent=status.building?'源码已变更，正在编译…':buildError?'编译失败 · 点击查看':'监视源码 · 保存后自动更新';
      if(!buildVersion)buildVersion=status.version;
      else if(status.version&&status.version!==buildVersion&&!status.building){
        const next=new Uint8Array(await(await fetch('assets/firmware.wasm?v='+status.version,{cache:'no-store'})).arrayBuffer());
        buildVersion=status.version;boot(next);notify('新源码编译成功，已载入新的固件界面。');
      }
    }catch{$('build-state').textContent='开发服务未连接 · 当前版本仍可用';}
    setTimeout(pollBuild,1200);
  }
  async function init(){
    try{const saved=JSON.parse(localStorage.getItem(NVS)||'{}');if(saved&&typeof saved==='object')preferences=saved;}catch{saveLabel='浏览器未允许本地保存';}
    try{db=await openDB();const files=await dbOperation('readonly',store=>store.getAll());for(const file of files)addFile(file);}catch{notify('导入文件只能在本次窗口保留。');}
    boot();showMedia();setInterval(frame,8);pollBuild();
  }
  globalThis.PocketSimulator={snapshot:()=>firmware?.snapshot(),screenText:()=>[...drawText],logs:()=>[...logs],images:()=>[...imageCache.values()].map(i=>({width:i.naturalWidth,height:i.naturalHeight})),audio:()=>({time:audio.currentTime,duration:audio.duration,readyState:audio.readyState,paused:audio.paused,error:audio.error?.message})};
  init();
})();
