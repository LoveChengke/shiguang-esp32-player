// Hardware boundary: firmware UI commands drive browser audio instead of I2S.
#include "audio_service.hpp"
extern "C" {
__attribute__((import_module("host"),import_name("audio_command"))) int host_audio_command(int,const char*,int);
__attribute__((import_module("host"),import_name("audio_status"))) unsigned host_audio_status(int);
}
namespace player {
bool begin(SemaphoreHandle_t){return true;}
bool play(const char* path,int index){return host_audio_command(0,path,index);}
bool pause(){return host_audio_command(1,"",0);}
bool volume(uint8_t value){return host_audio_command(2,"",value);}
bool seek(uint8_t value){return host_audio_command(3,"",value);}
Playback status(){return {AudioState(host_audio_status(0)),int(host_audio_status(1)),host_audio_status(2),uint8_t(host_audio_status(3)),host_audio_status(4),host_audio_status(5)};}
}
