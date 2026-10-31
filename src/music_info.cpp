#include "music_info.hpp"
#include <array>

namespace {
std::string tagText(const uint8_t* p,size_t n,unsigned encoding) {
    std::string out;
    if(encoding==0||encoding==3) {
        for(size_t i=0;i<n;++i){unsigned c=p[i];if(!c)break;if(encoding==0&&c>=128){out+=char(0xC0|(c>>6));out+=char(0x80|(c&63));}else out+=char(c);}
        return out;
    }
    bool little=encoding==1&&n>=2&&p[0]==255&&p[1]==254;
    size_t i=encoding==1&&n>=2&&((p[0]==255&&p[1]==254)||(p[0]==254&&p[1]==255))?2:0;
    for(;i+1<n;i+=2){uint32_t c=little?(p[i]|p[i+1]<<8):(p[i]<<8|p[i+1]);if(!c)break;if(c>=0xD800&&c<=0xDFFF)c='?';if(c<128)out+=char(c);else if(c<2048){out+=char(0xC0|(c>>6));out+=char(0x80|(c&63));}else{out+=char(0xE0|(c>>12));out+=char(0x80|((c>>6)&63));out+=char(0x80|(c&63));}}
    return out;
}
size_t terminated(const uint8_t* p,size_t n,size_t at,bool wide) {
    for(;at<n;at+=wide?2:1)if(!p[at]&&(!wide||(at+1<n&&!p[at+1])))return at+(wide?2:1);
    return n;
}
}
bool MusicInfo::read(File& file,uint32_t at,uint8_t* bytes,unsigned count) {
    for(unsigned done=0;done<count;) {
        unsigned n=std::min(512U,count-done);
        xSemaphoreTake(mutex_,portMAX_DELAY);
        bool ok=file.seek(at+done)&&file.read(bytes+done,n)==int(n);
        xSemaphoreGive(mutex_);if(!ok)return false;done+=n;delay(1);
    }
    return true;
}
void MusicInfo::load(const String& path,int index) {
    track=index;cover.clear();lines.clear();width=height=0;warning="";
    auto open=[&](const String& name){xSemaphoreTake(mutex_,portMAX_DELAY);auto f=SD.open(name);xSemaphoreGive(mutex_);return f;};
    auto close=[&](File& f){xSemaphoreTake(mutex_,portMAX_DELAY);f.close();xSemaphoreGive(mutex_);};
    auto size=[&](File& f){xSemaphoreTake(mutex_,portMAX_DELAY);unsigned n=f?f.size():0;xSemaphoreGive(mutex_);return n;};
    String stem=path.substring(0,path.length()-4);
    File lrc=open(stem+".lrc");unsigned n=size(lrc);
    if(n&&n<=16384){std::vector<uint8_t> data(n);if(read(lrc,0,data.data(),n)){std::string value(data.begin(),data.end());if(value.compare(0,3,"\xEF\xBB\xBF")==0)value.erase(0,3);lines=pocket::lyrics(value);}}
    else if(n)warning="歌词过大，请精简至 16KB";
    close(lrc);
    String folder=path.substring(0,path.lastIndexOf('/'));
    for(const auto& name:{stem+".jpg",stem+".jpeg",stem+".png",folder+"/cover.jpg",folder+"/folder.jpg",folder+"/cover.png"}) {
        File image=open(name);unsigned bytes=size(image);
        if(bytes&&bytes<=49152){cover.resize(bytes);if(!read(image,0,cover.data(),bytes))cover.clear();}
        else if(bytes)warning="封面过大，请缩小至 48KB";
        close(image);if(!cover.empty())break;
    }
    File mp3=open(path);unsigned bytes=size(mp3);std::array<uint8_t,10> head{};
    if(bytes>=10&&read(mp3,0,head.data(),10)&&!std::memcmp(head.data(),"ID3",3)&&(head[3]==3||head[3]==4)&&!(head[5]&0x80)) {
        unsigned version=head[3],end=std::min<uint32_t>(bytes,10+pocket::sync32(head.data()+6)),at=10;
        if(head[5]&0x40){uint8_t ext[4];if(read(mp3,at,ext,4))at+=version==4?pocket::sync32(ext):4+pocket::big32(ext);}
        for(unsigned frames=0;at+10<=end&&frames<128;++frames) {
            if(!read(mp3,at,head.data(),10)||!head[0])break;
            unsigned length=version==4?pocket::sync32(head.data()+4):pocket::big32(head.data()+4),body=at+10;
            if(length>end-body)break;at=body+length;
            if(head[9])continue; // Compressed, encrypted and unsynchronised frames require preprocessing.
            bool picture=cover.empty()&&!std::memcmp(head.data(),"APIC",4),lyric=lines.empty()&&!std::memcmp(head.data(),"USLT",4);
            if(!picture&&!lyric)continue;
            if(length>(picture?49408U:16384U)){warning=picture?"内嵌封面过大，请另存小封面":"内嵌歌词过大";continue;}
            std::vector<uint8_t> data(length);if(!length||!read(mp3,body,data.data(),length))continue;
            unsigned enc=data[0];if(enc>3)continue;
            size_t p=picture?terminated(data.data(),length,1,false)+1:4;
            p=terminated(data.data(),length,p,enc==1||enc==2);
            if(p>=length)continue;
            if(picture)cover.assign(data.begin()+p,data.end());
            else lines=pocket::lyrics(tagText(data.data()+p,length-p,enc));
        }
    }
    close(mp3);
    png=cover.size()>=24&&!std::memcmp(cover.data(),"\x89PNG\r\n\x1a\n",8);
    if(png){width=pocket::big32(cover.data()+16);height=pocket::big32(cover.data()+20);}
    else if(cover.size()>4&&cover[0]==255&&cover[1]==216) {
        for(size_t p=2;p+9<cover.size();) {
            if(cover[p++]!=255)break;unsigned marker=cover[p++];if(marker==255){--p;continue;}
            unsigned len=(cover[p]<<8)|cover[p+1];if(len<2||len>cover.size()-p)break;
            if((marker>=0xC0&&marker<=0xC3)||(marker>=0xC5&&marker<=0xC7)||(marker>=0xC9&&marker<=0xCB)){height=(cover[p+3]<<8)|cover[p+4];width=(cover[p+5]<<8)|cover[p+6];break;}
            p+=len;
        }
    }
    if(!width||!height||width>2048||height>2048)cover.clear();
}
unsigned MusicInfo::current(uint32_t elapsed) const {
    if(!lines.empty()&&lines.back().at==0)return elapsed/6000%lines.size();
    unsigned index=0;for(unsigned i=0;i<lines.size();++i){if(lines[i].at>elapsed)break;index=i;}return index;
}
