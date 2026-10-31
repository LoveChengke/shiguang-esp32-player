// Compile the actual firmware translation unit, not a reimplementation of its UI.
#include "../src/main.cpp"
#include <cstdlib>

extern "C" {
void sim_setup(){setup();}
void sim_loop(){loop();}
void sim_key(unsigned key,unsigned down){if(key<5)sim_pins[board::buttons[key]]=down;}
void sim_serial(unsigned c){Serial.input.push_back(char(c));}
void* sim_alloc(unsigned size){return std::malloc(size);}
void sim_free(void* p){std::free(p);}
void sim_file(const char* path,const uint8_t* data,unsigned size){auto f=std::make_shared<SimFileData>();f->path=path;f->bytes.assign(data,data+size);sim_files[path]=f;}
// Inspector shortcuts operate on the real firmware state. They contain no UI logic.
void sim_navigate(unsigned screen){
    if(screen>5)return;
    if(screen==4&&!reader.active()){if(books.empty()||!reader.open(books[0]))return;}
    reader.save();view=View(screen);selected=0;redraw=true;
}
void sim_open_book(unsigned index){if(index<books.size()&&reader.open(books[index])){view=View::Reader;redraw=true;}}
void sim_play_track(unsigned index){if(index<music.size()){startTrack(index);view=View::Player;redraw=true;}}
void sim_volume(unsigned value){setVolume(value);}
void sim_save(){reader.save();saveSettings();}
unsigned sim_page_lines(){return reader.page.lines.size();}
const char* sim_page_line(unsigned index){return index<reader.page.lines.size()?reader.page.lines[index].c_str():"";}
// Pure observation; all transitions, settings, pagination and drawing remain in src/.
const char* sim_snapshot(){
    static char out[1024];auto audio=player::status();
    std::snprintf(out,sizeof(out),"{\"view\":%d,\"homeSelected\":%d,\"selected\":%d,\"settingSelected\":%d,\"volume\":%u,\"font\":%u,\"repeatOne\":%s,\"paper\":%s,\"offset\":%u,\"readerActive\":%s,\"searching\":%s,\"musicCount\":%u,\"bookCount\":%u,\"audio\":%d,\"track\":%d}",int(view),homeSelected,selected,settingSelected,unsigned(volume),reader.font,repeatOne?"true":"false",paper?"true":"false",reader.offset,reader.active()?"true":"false",reader.searching()?"true":"false",unsigned(music.size()),unsigned(books.size()),int(audio.state),audio.track);
    return out;
}
}
