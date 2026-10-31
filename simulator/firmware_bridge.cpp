// Compile the actual firmware translation unit, not a reimplementation of its UI.
#include "../src/main.cpp"
#include <cstdlib>

extern "C" {
void sim_setup(){setup();}
void sim_clock(unsigned minutes){initialMinutes=minutes%1440;}
void sim_loop(){loop();}
void sim_key(unsigned key,unsigned down){if(key<board::keyCount)sim_pins[board::buttons[key]]=down;}
void sim_serial(unsigned c){Serial.input.push_back(char(c));}
void* sim_alloc(unsigned size){return std::malloc(size);}
void sim_free(void* p){std::free(p);}
void sim_file(const char* path,const uint8_t* data,unsigned size){auto f=std::make_shared<SimFileData>();f->path=path;f->bytes.assign(data,data+size);sim_files[path]=f;}
// Inspector shortcuts operate on the real firmware state. They contain no UI logic.
void sim_navigate(unsigned screen){
    if(screen>5)return;
    if(screen==4&&!reader.active()){if(books.empty()||!reader.open(books[0]))return;}
    reader.save();readerControl=0;if(screen==1||screen==2)enterLibrary(screen==1);else view=View(screen);selected=0;redraw=true;
}
void sim_open_book(unsigned index){if(index<books.size()&&reader.open(books[index])){view=View::Reader;redraw=true;}}
void sim_play_track(unsigned index){if(index<music.size()){startTrack(index);view=View::Player;redraw=true;}}
void sim_volume(unsigned value){setVolume(value);}
void sim_save(){reader.save();saveSettings();}
unsigned sim_page_lines(){return reader.page.lines.size();}
const char* sim_page_line(unsigned index){return index<reader.page.lines.size()?reader.page.lines[index].c_str():"";}
// Pure observation; all transitions, settings, pagination and drawing remain in src/.
const char* sim_snapshot(){
    static char out[1536];auto audio=player::status();
    std::snprintf(out,sizeof(out),"{\"view\":%d,\"homeSelected\":%d,\"selected\":%d,\"settingSelected\":%d,\"volume\":%u,\"font\":%u,\"repeatOne\":%s,\"repeatMode\":%u,\"paper\":%s,\"offset\":%u,\"progress\":%u,\"readerControl\":%d,\"readerPercent\":%d,\"readerEditing\":%s,\"readerActive\":%s,\"searching\":%s,\"musicCount\":%u,\"bookCount\":%u,\"playlistCount\":%u,\"libraryCount\":%u,\"libraryPlaylist\":%s,\"lyricCount\":%u,\"hasCover\":%s,\"clock\":\"%s\",\"audio\":%d,\"track\":%d,\"audioProgress\":%u,\"durationMs\":%u,\"elapsedMs\":%u}",int(view),homeSelected,selected,settingSelected,unsigned(volume),reader.font,repeatMode==3?"true":"false",unsigned(repeatMode),paper?"true":"false",reader.offset,reader.progress(),readerControl,readerPercent,readerEditing?"true":"false",reader.active()?"true":"false",reader.searching()?"true":"false",unsigned(music.size()),unsigned(books.size()),unsigned(playlist.size()),unsigned(library.size()),libraryPlaylist?"true":"false",unsigned(musicInfo.lines.size()),musicInfo.cover.empty()?"false":"true",clockText().c_str(),int(audio.state),audio.track,unsigned(audio.progress),audio.durationMs,audio.elapsedMs);
    return out;
}
}
