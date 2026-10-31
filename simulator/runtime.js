/* Only platform adaptation lives here. All screens and interaction logic are C++. */
(() => {
  class FirmwareRuntime {
    constructor(binary, host) {
      this.host=host;
      const bytes=typeof binary==='string'?Uint8Array.from(atob(binary),c=>c.charCodeAt(0)):binary;
      const read=p=>this.string(p),mem=()=>new Uint8Array(this.instance.exports.memory.buffer);
      const view=()=>new DataView(this.instance.exports.memory.buffer);
      const wasi={fd_close:()=>0,fd_seek:()=>8,fd_write:(fd,iovs,count,written)=>{
        let n=0;for(let i=0;i<count;++i){const at=view().getUint32(iovs+i*8,true),len=view().getUint32(iovs+i*8+4,true);host.log?.(new TextDecoder().decode(mem().subarray(at,at+len)));n+=len;}view().setUint32(written,n,true);return 0;
      }};
      const api={
        millis:()=>host.millis()>>>0,
        log:(p,n)=>host.log?.(new TextDecoder().decode(mem().subarray(p,p+n))),
        restart:()=>host.restart?.(),
        rect:(...args)=>host.rect?.(...args),circle:(...args)=>host.circle?.(...args),
        text:(p,...args)=>host.text?.(read(p),...args),
        pref_length:(ns,key)=>host.preferences[`${read(ns)}/${read(key)}`]?.length||0,
        pref_read:(ns,key,p,size)=>{const data=host.preferences[`${read(ns)}/${read(key)}`]||[];const n=Math.min(size,data.length);mem().set(data.slice(0,n),p);return n;},
        pref_write:(ns,key,p,size)=>{host.preferences[`${read(ns)}/${read(key)}`]=Array.from(mem().subarray(p,p+size));return host.savePreferences?.()===false?0:size;},
        audio_command:(command,p,value)=>host.audioCommand(command,read(p),value),
        audio_status:field=>host.audioStatus(field),
      };
      this.instance=new WebAssembly.Instance(new WebAssembly.Module(bytes),{host:api,wasi_snapshot_preview1:wasi});
      this.e=this.instance.exports;this.e._initialize?.();
    }
    string(pointer){const memory=new Uint8Array(this.instance.exports.memory.buffer);let end=pointer;while(memory[end]&&end<memory.length)++end;return new TextDecoder().decode(memory.subarray(pointer,end));}
    mount(path,bytes){
      const name=new TextEncoder().encode(path+'\0'),a=this.e.sim_alloc(name.length),b=this.e.sim_alloc(Math.max(1,bytes.length));
      if(!a||!b)throw new Error('虚拟 SD 卡内存不足');
      try{let memory=new Uint8Array(this.e.memory.buffer);memory.set(name,a);memory.set(bytes,b);this.e.sim_file(a,b,bytes.length);}finally{this.e.sim_free(a);this.e.sim_free(b);}
    }
    setup(){this.e.sim_setup();}
    loop(){this.e.sim_loop();}
    key(key,down){this.e.sim_key(key,Number(down));}
    serial(c){this.e.sim_serial(c.charCodeAt(0));}
    save(){this.e.sim_save();}
    snapshot(){const state=JSON.parse(this.string(this.e.sim_snapshot()));state.lines=Array.from({length:this.e.sim_page_lines()},(_,i)=>this.string(this.e.sim_page_line(i)));return state;}
  }
  globalThis.FirmwareRuntime=FirmwareRuntime;
})();
