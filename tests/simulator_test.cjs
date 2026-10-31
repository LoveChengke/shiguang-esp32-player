const assert=require('node:assert/strict');
require('../simulator/firmware.js');require('../simulator/runtime.js');
let checks=0;const eq=(a,b)=>{assert.deepEqual(a,b);++checks;},ok=a=>{assert(a);++checks;};
const preferences={},encoder=new TextEncoder();
const book='\uFEFF第一章 山间的风\r\n'+('旅人打开播放器，一边听音乐，一边读书。\n'.repeat(200));
const png=Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+j2ioAAAAASUVORK5CYII=','base64');
function id3(version=3){
  const frame=(id,body)=>{const head=Buffer.alloc(10);head.write(id);if(version===4){for(let i=0;i<4;++i)head[4+i]=(body.length>>((3-i)*7))&127;}else head.writeUInt32BE(body.length,4);return Buffer.concat([head,body]);};
  const body=Buffer.concat([frame('USLT',Buffer.concat([Buffer.from([1]),Buffer.from('eng\0\0'),Buffer.from('\ufeff内嵌歌词\n下一行','utf16le')])),frame('APIC',Buffer.concat([Buffer.from([0]),Buffer.from('image/png\0'),Buffer.from([3,0]),png]))]);
  const head=Buffer.from([73,68,51,version,0,0,(body.length>>21)&127,(body.length>>14)&127,(body.length>>7)&127,body.length&127]);return Buffer.concat([head,body]);
}
function machine(folders=false){
  let time=0,draw=[],images=0,sound={state:0,track:-1,finished:0,progress:0,elapsed:0,duration:24000};
  const r=new FirmwareRuntime(POCKET_FIRMWARE_BASE64,{preferences,millis:()=>time,savePreferences:()=>true,
    text:value=>draw.push(value),rect:(x,y,w,h)=>{if(x===0&&y===0&&w===240&&h===320)draw=[];},image:()=>++images,
    audioStatus:field=>[sound.state,sound.track,sound.finished,sound.progress,sound.elapsed,sound.duration][field],
    audioCommand:(command,path,value)=>{if(command===0){sound.state=1;sound.track=value;sound.progress=0;sound.elapsed=0;}if(command===1)sound.state=sound.state===1?2:1;if(command===3){sound.progress=value;sound.elapsed=value*sound.duration/100;}return 1;}});
  r.mount('/books/001-demo.txt',encoder.encode(book));r.mount('/books/002-empty.txt',new Uint8Array());
  r.mount('/music/001-test.mp3',new Uint8Array([0]));r.mount('/music/002-test.mp3',id3());
  r.mount('/music/001-test.LRC',encoder.encode('[00:00.00]第一句歌词\n[00:05.00]第二句歌词\n[00:15.00]第三句歌词'));
  r.mount('/music/001-test.png',png);
  if(folders){r.mount('/music/Album/001-same.mp3',new Uint8Array([0]));r.mount('/music/Album/002-same.mp3',new Uint8Array([0]));r.mount('/music/Other/001-same.mp3',new Uint8Array([0]));r.mount('/books/Series/novel.txt',encoder.encode(book));}
  r.setup();
  const advance=ms=>{time+=ms;r.loop();};
  const key=(index,long=false)=>{r.key(index,true);advance(0);advance(31);if(long)advance(651);r.key(index,false);advance(1);advance(31);};
  const navigate=screen=>{r.e.sim_navigate(screen);r.loop();};
  return {r,key,advance,navigate,screen:()=>draw,sound,images:()=>images};
}
let m=machine();eq(m.r.snapshot().view,0);eq(m.r.snapshot().musicCount,2);eq(m.r.snapshot().bookCount,2);ok(m.screen().includes('拾光 · 随身播放器'));
for(let view=0;view<6;++view){m.navigate(view);ok(m.screen().some(t=>/^\d{2}:\d{2}$/.test(t)));ok(!m.screen().some(t=>t.includes('上下翻页')));}
m.navigate(0);m.key(2);eq(m.r.snapshot().view,1);m.key(2);eq(m.r.snapshot().view,3);eq(m.r.snapshot().audio,1);
eq(m.r.snapshot().lyricCount,3);eq(m.r.snapshot().hasCover,true);ok(m.images()>0);ok(m.screen().includes('第一句歌词'));
m.key(2);eq(m.r.snapshot().audio,2);m.key(6);ok(m.r.snapshot().audioProgress>0);eq(m.r.snapshot().audio,2);m.key(5);eq(m.r.snapshot().audioProgress,0);
m.key(2);eq(m.r.snapshot().audio,1);m.key(0);eq(m.r.snapshot().volume,11);m.key(1);eq(m.r.snapshot().volume,10);m.key(0,true);eq(m.r.snapshot().volume,11);
m.key(6,true);eq(m.r.snapshot().track,1);eq(m.r.snapshot().lyricCount,2);eq(m.r.snapshot().hasCover,true);ok(m.screen().includes('内嵌歌词'));
m.key(4);eq(m.r.snapshot().view,4);m.key(1);const offset=m.r.snapshot().offset;ok(offset>0);m.key(0);eq(m.r.snapshot().offset,0);m.key(1);eq(m.r.snapshot().offset,offset);
// In-page settings: choose exact percentage using up/down, then commit.
m.key(2);eq(m.r.snapshot().readerControl,1);m.key(2);const initial=m.r.snapshot().readerPercent;for(let i=initial;i<40;++i)m.key(1);m.key(2);
eq(m.r.snapshot().progress,40);ok(!m.r.snapshot().readerControl);ok(!m.r.snapshot().lines.join('').includes('�'));const target=m.r.snapshot().offset;
m.key(2);m.key(1);eq(m.r.snapshot().readerControl,2);m.key(2);m.key(1);eq(m.r.snapshot().font,24);m.key(2);eq(m.r.snapshot().offset,target);
m.key(2,true);eq(m.r.snapshot().audio,2);m.advance(800);ok(Object.keys(preferences).some(k=>k.startsWith('pocket/b')));
m=machine();eq(m.r.snapshot().volume,11);eq(m.r.snapshot().font,24);m.r.e.sim_open_book(0);m.r.loop();eq(m.r.snapshot().offset,target);
m.key(0);for(let i=0;i<500&&m.r.snapshot().searching;++i)m.advance(2);ok(m.r.snapshot().offset<target);
m.key(2);m.key(2);for(let i=0;i<100;++i)m.key(1);m.key(2);ok(m.r.snapshot().offset<encoder.encode(book).length);ok(!m.r.snapshot().lines.join('').includes('�'));
m.key(5);ok(m.r.snapshot().progress>=94&&m.r.snapshot().progress<=95);
m.navigate(5);m.key(1);m.key(1);m.key(2);m.key(2);m.key(2);eq(m.r.snapshot().repeatOne,true);m.key(1);m.key(2);eq(m.r.snapshot().paper,true);
m.key(1);m.key(2);m.key(1);m.key(2);m.key(1);m.key(2);eq(m.r.snapshot().clock,'13:01');m.r.save();m=machine();eq(m.r.snapshot().clock,'13:01');eq(m.r.snapshot().paper,true);eq(m.r.snapshot().repeatOne,true);
m.r.e.sim_open_book(1);m.r.loop();eq(m.r.snapshot().readerActive,false);m.r.e.sim_play_track(0);m.sound.finished++;m.advance(1);eq(m.r.snapshot().track,0);
// Folder queues do not escape into another album, including after last track EOF.
for(const key of Object.keys(preferences))delete preferences[key];
m=machine(true);m.navigate(1);eq(m.r.snapshot().libraryCount,5);m.key(1);m.key(1);m.key(2);eq(m.r.snapshot().libraryCount,2);m.key(2);eq(m.r.snapshot().track,2);eq(m.r.snapshot().repeatMode,1);
m.sound.finished++;m.advance(1);eq(m.r.snapshot().track,3);m.sound.finished++;m.advance(1);eq(m.r.snapshot().track,2);
// Playlist membership and chosen order survive restarts, without filename ambiguity.
m.navigate(1);m.key(1);m.key(2,true);eq(m.r.snapshot().playlistCount,1);m.key(0);m.key(2,true);eq(m.r.snapshot().playlistCount,2);m.r.save();
m=machine(true);eq(m.r.snapshot().playlistCount,2);m.navigate(1);for(let i=0;i<4;++i)m.key(1);m.key(2);eq(m.r.snapshot().libraryPlaylist,true);eq(m.r.snapshot().libraryCount,2);m.key(2);eq(m.r.snapshot().track,1);eq(m.r.snapshot().repeatMode,2);
m.sound.finished++;m.advance(1);eq(m.r.snapshot().track,0);m.sound.finished++;m.advance(1);eq(m.r.snapshot().track,1);
m.key(3);for(let i=0;i<4;++i)m.key(1);m.key(2);m.key(2,true);eq(m.r.snapshot().playlistCount,1);m.key(2,true);eq(m.r.snapshot().playlistCount,0);eq(m.r.snapshot().libraryCount,0);m.sound.finished++;m.advance(1);eq(m.r.snapshot().audio,1); // no new play command for empty queue
m.key(3);eq(m.r.snapshot().libraryPlaylist,false);m.navigate(2);m.key(1);m.key(1);m.key(2);eq(m.r.snapshot().libraryCount,1);m.key(2);eq(m.r.snapshot().view,4);
m=machine();m.r.mount('/music/002-test.mp3',id3(4));m.r.e.sim_play_track(1);m.r.loop();eq(m.r.snapshot().hasCover,true);eq(m.r.snapshot().lyricCount,2);ok(m.screen().includes('内嵌歌词'));
console.log(`Firmware simulator: ${checks} assertions PASS (actual main.cpp + reader.cpp + music_info.cpp)`);
