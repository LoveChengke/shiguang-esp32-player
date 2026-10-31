// Run the same main.cpp + reader.cpp WASM used by the browser, without a DOM.
const assert=require('node:assert/strict');
require('../simulator/firmware.js');require('../simulator/runtime.js');
let checks=0;const eq=(a,b)=>{assert.deepEqual(a,b);++checks;};
const preferences={},encoder=new TextEncoder();
const book='\uFEFF第一章 山间的风\r\n'+('旅人打开播放器，一边听音乐，一边读书。\n'.repeat(200));
function machine(){
  let time=0,draw=[],sound={state:0,track:-1,finished:0,progress:0};
  const r=new FirmwareRuntime(POCKET_FIRMWARE_BASE64,{preferences,millis:()=>time,savePreferences:()=>true,
    text:value=>draw.push(value),rect:(x,y,w,h)=>{if(x===0&&y===0&&w===240&&h===320)draw=[];},
    audioStatus:field=>[sound.state,sound.track,sound.finished,sound.progress][field],
    audioCommand:(command,path,value)=>{if(command===0){sound.state=1;sound.track=value;}if(command===1)sound.state=sound.state===1?2:1;return 1;}});
  r.mount('/books/001-demo.txt',encoder.encode(book));r.mount('/books/002-empty.txt',new Uint8Array());
  r.mount('/music/001-test.mp3',new Uint8Array([0]));r.mount('/music/002-test.mp3',new Uint8Array([0]));r.setup();
  const advance=ms=>{time+=ms;r.loop();};
  const key=(index,long=false)=>{r.key(index,true);advance(0);advance(31);if(long)advance(651);r.key(index,false);advance(1);advance(31);};
  return {r,key,advance,screen:()=>draw,sound};
}
let m=machine();eq(m.r.snapshot().view,0);eq(m.r.snapshot().musicCount,2);eq(m.r.snapshot().bookCount,2);
assert(m.screen().includes('拾光 · 随身播放器'));++checks;
m.key(2);eq(m.r.snapshot().view,1);m.key(2);eq(m.r.snapshot().view,3);eq(m.r.snapshot().audio,1);
m.key(2);eq(m.r.snapshot().audio,2);m.key(2);eq(m.r.snapshot().audio,1);
m.key(1);eq(m.r.snapshot().track,1);m.key(0,true);eq(m.r.snapshot().volume,11);
m.key(4);eq(m.r.snapshot().view,2);m.key(2);eq(m.r.snapshot().view,4);eq(m.r.snapshot().lines[0],'第一章 山间的风');
m.key(1);const offset=m.r.snapshot().offset;assert(offset>0);++checks;
m.key(0);eq(m.r.snapshot().offset,0);m.key(1);eq(m.r.snapshot().offset,offset);
m.key(2);eq(m.r.snapshot().font,24);eq(m.r.snapshot().offset,offset);
m.key(2,true);eq(m.r.snapshot().audio,2);m.advance(800);
assert(Object.keys(preferences).some(k=>k.startsWith('pocket/b')));++checks;
m=machine();eq(m.r.snapshot().volume,11);eq(m.r.snapshot().font,24);m.r.e.sim_open_book(0);m.r.loop();eq(m.r.snapshot().offset,offset);
m.key(0);for(let i=0;i<100&&m.r.snapshot().searching;++i)m.advance(2);assert(m.r.snapshot().offset<offset);++checks;
m.r.e.sim_navigate(5);m.r.loop();m.key(1);m.key(1);m.key(2);eq(m.r.snapshot().repeatOne,true);m.key(1);m.key(2);eq(m.r.snapshot().paper,true);
m.r.save();m=machine();eq(m.r.snapshot().paper,true);eq(m.r.snapshot().repeatOne,true);
m.r.e.sim_open_book(1);m.r.loop();eq(m.r.snapshot().readerActive,false);
eq(m.r.snapshot().audio,0);m.r.e.sim_play_track(0);m.sound.finished++;m.advance(1);eq(m.r.snapshot().track,0);
console.log(`Firmware simulator: ${checks} assertions PASS (actual main.cpp + reader.cpp)`);
