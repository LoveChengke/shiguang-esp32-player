#pragma once
#include <PocketCore.hpp>
#include <array>
#include <cstdio>
#include <cstring>

namespace pocket {
inline uint32_t big32(const uint8_t* p) { return uint32_t(p[0])<<24 | uint32_t(p[1])<<16 | uint32_t(p[2])<<8 | p[3]; }
inline uint32_t sync32(const uint8_t* p) { return uint32_t(p[0]&127)<<21 | uint32_t(p[1]&127)<<14 | uint32_t(p[2]&127)<<7 | (p[3]&127); }
struct Mp3Frame { unsigned bytes=0, rate=0, samples=0, bitrate=0, side=0; bool crc=false; };
inline Mp3Frame mp3Frame(const uint8_t* p) {
    const unsigned version=(p[1]>>3)&3, bit=p[2]>>4, frequency=(p[2]>>2)&3;
    if(p[0]!=255 || (p[1]&0xE0)!=0xE0 || version==1 || ((p[1]>>1)&3)!=1 || !bit || bit==15 || frequency==3) return {};
    constexpr unsigned high[]={0,32,40,48,56,64,80,96,112,128,160,192,224,256,320};
    constexpr unsigned low[]={0,8,16,24,32,40,48,56,64,80,96,112,128,144,160};
    constexpr unsigned rates[]={44100,48000,32000};
    unsigned rate=rates[frequency]/(version==3?1:version==2?2:4), kbps=version==3?high[bit]:low[bit];
    bool mono=(p[3]>>6)==3;
    return {(version==3?144000U:72000U)*kbps/rate+((p[2]>>1)&1),rate,version==3?1152U:576U,kbps*1000,(version==3?(mono?17U:32U):(mono?9U:17U)),!(p[1]&1)};
}
struct Mp3Timing {
    uint32_t start=0, bytes=0, duration=0;
    std::array<uint8_t,100> toc{};
    bool hasToc=false;
    void parse(const uint8_t* frame, size_t count, uint32_t offset, uint32_t fileSize) {
        hasToc=false;
        if(count<4||offset>=fileSize)return;
        auto h=mp3Frame(frame); if(!h.bytes)return;
        start=offset; bytes=fileSize-offset;
        duration=uint64_t(bytes)*8000/h.bitrate;
        size_t at=4+(h.crc?2:0)+h.side;
        if(at+12<=count && (!std::memcmp(frame+at,"Xing",4)||!std::memcmp(frame+at,"Info",4))) {
            auto flags=big32(frame+at+4); at+=8;
            if(flags&1) { if(at+4>count)return; duration=uint64_t(big32(frame+at))*h.samples*1000/h.rate; at+=4; }
            if(flags&2) { if(at+4>count)return; auto n=big32(frame+at); if(n && n<=fileSize-offset)bytes=n; at+=4; }
            if((flags&4)&&at+100<=count){std::copy_n(frame+at,100,toc.begin());hasToc=true;}
        } else if(count>=54 && !std::memcmp(frame+36,"VBRI",4)) {
            duration=uint64_t(big32(frame+50))*h.samples*1000/h.rate;
        }
    }
    uint32_t position(unsigned percent) const {
        percent=std::min(99U,percent);
        return start+(hasToc?uint64_t(bytes)*toc[percent]/256:uint64_t(bytes)*percent/100);
    }
};
struct Lyric { uint32_t at=0; std::string text; };
inline std::vector<Lyric> lyrics(std::string_view input) {
    std::vector<Lyric> result; int offset=0; size_t at=0; bool timed=false;
    while(at<input.size()&&result.size()<96) {
        size_t end=input.find('\n',at); if(end==input.npos)end=input.size();
        std::string line(input.substr(at,end-at)); at=end+1;
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(line.rfind("[offset:",0)==0){std::sscanf(line.c_str(),"[offset:%d]",&offset);continue;}
        std::vector<uint32_t> times;size_t p=0;
        while(p<line.size()&&line[p]=='[') {
            unsigned minutes=0,seconds=0,fraction=0;int used=0;
            if(std::sscanf(line.c_str()+p,"[%u:%u%n",&minutes,&seconds,&used)<2||seconds>=60||!used)break;
            size_t q=p+used;
            if(q<line.size()&&line[q]=='.'){++q;unsigned digits=0;while(q<line.size()&&line[q]>='0'&&line[q]<='9'){if(digits<3)fraction=fraction*10+line[q]-'0';++digits;++q;}if(digits==1)fraction*=100;else if(digits==2)fraction*=10;}
            if(q>=line.size()||line[q]!=']')break;
            times.push_back((minutes*60+seconds)*1000+fraction);p=q+1;
        }
        if(times.empty()&&(!line.empty()&&line[0]=='['))continue;
        std::string value=line.substr(p,180);
        // Do not truncate in the middle of a UTF-8 character.
        while(!value.empty()&&p+value.size()<line.size()&&(uint8_t(line[p+value.size()])&0xC0)==0x80)value.pop_back();
        if(value.empty())continue;
        if(!times.empty())timed=true;
        if(times.empty())result.push_back({UINT32_MAX,value});
        else for(auto time:times)if(result.size()<96)result.push_back({time,value});
    }
    if(timed)result.erase(std::remove_if(result.begin(),result.end(),[](const Lyric& l){return l.at==UINT32_MAX;}),result.end());
    for(auto& line:result)line.at=line.at==UINT32_MAX?0:uint32_t(std::max<int64_t>(0,int64_t(line.at)+offset));
    std::stable_sort(result.begin(),result.end(),[](const Lyric&a,const Lyric&b){return a.at<b.at;});
    return result;
}
}
