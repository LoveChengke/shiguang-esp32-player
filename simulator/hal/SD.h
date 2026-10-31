#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <map>
#include <memory>
#include <vector>
struct SimFileData {std::string path;std::vector<uint8_t> bytes;};
inline std::map<std::string,std::shared_ptr<SimFileData>> sim_files;
class File {
    struct Handle {std::shared_ptr<SimFileData> file;std::string dir;size_t position=0,entry=0;std::vector<std::string> entries;};
    std::shared_ptr<Handle> handle_;
public:
    File()=default;
    explicit File(std::shared_ptr<SimFileData> f):handle_(std::make_shared<Handle>()){handle_->file=f;}
    explicit File(std::string dir):handle_(std::make_shared<Handle>()){
        handle_->dir=dir;
        for(auto& [path,file]:sim_files)if(path.rfind(dir+"/",0)==0)handle_->entries.push_back(path);
    }
    explicit operator bool()const{return bool(handle_);}
    bool isDirectory()const{return handle_&&!handle_->file;}
    const char* name()const{return !handle_?"":handle_->file?handle_->file->path.c_str():handle_->dir.c_str();}
    File openNextFile(){if(!isDirectory()||handle_->entry>=handle_->entries.size())return {};return File(sim_files.at(handle_->entries[handle_->entry++]));}
    unsigned size()const{return handle_&&handle_->file?handle_->file->bytes.size():0;}
    bool seek(unsigned at){if(!handle_||!handle_->file||at>size())return false;handle_->position=at;return true;}
    int read(uint8_t* target,unsigned count){if(!handle_||!handle_->file)return -1;unsigned n=std::min<unsigned>(count,size()-handle_->position);std::memcpy(target,handle_->file->bytes.data()+handle_->position,n);handle_->position+=n;return n;}
    int read(){uint8_t byte;return read(&byte,1)==1?byte:-1;}
    int peek(){if(!handle_||!handle_->file||handle_->position>=size())return -1;return handle_->file->bytes[handle_->position];}
    void close(){handle_.reset();}
};
struct SimSD {
    bool begin(int,SPIClass&,unsigned){return true;}
    File open(const String& path){std::string p=path.c_str();if(p=="/music"||p=="/books")return File(p);auto i=sim_files.find(p);return i==sim_files.end()?File():File(i->second);}
};
inline SimSD SD;
