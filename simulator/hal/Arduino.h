#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <type_traits>
#include <freertos/semphr.h>

extern "C" {
__attribute__((import_module("host"),import_name("millis"))) uint32_t host_millis();
__attribute__((import_module("host"),import_name("log"))) void host_log(const char*, unsigned);
__attribute__((import_module("host"),import_name("restart"))) void host_restart();
}
class String {
    std::string value_;
public:
    String() = default;
    String(const char* p):value_(p?p:""){}
    String(const std::string& s):value_(s){}
    template<class T, typename = std::enable_if_t<std::is_arithmetic_v<T>>>
    String(T number):value_(std::to_string(number)){}
    const char* c_str() const {return value_.c_str();}
    unsigned length() const {return value_.size();}
    bool startsWith(const String& s) const {return value_.rfind(s.value_,0)==0;}
    int lastIndexOf(char c) const {auto p=value_.find_last_of(c);return p==std::string::npos?-1:int(p);}
    String substring(unsigned a, unsigned b=unsigned(-1)) const {return a>=value_.size()?String():String(value_.substr(a,b==unsigned(-1)?std::string::npos:b-a));}
    int compareTo(const String& other) const {return value_.compare(other.value_);}
    String operator+(const String& other) const {return value_+other.value_;}
    String& operator+=(const String& other) {value_+=other.value_;return *this;}
    friend String operator+(const char* a,const String& b){return String(a)+b;}
};
constexpr int LOW=0,HIGH=1,INPUT=0;
inline bool sim_pins[64]{};
inline void pinMode(int,int){}
inline int digitalRead(int pin){return pin>=0&&pin<64&&sim_pins[pin]?LOW:HIGH;}
inline uint32_t millis(){return host_millis();}
inline void delay(unsigned){}
template<class T,class U,class V> T constrain(T value,U low,V high){return std::max<T>(low,std::min<T>(value,high));}
struct SimSerial {
    std::deque<char> input;
    void begin(unsigned){}
    unsigned available() const{return input.size();}
    char read(){char c=input.front();input.pop_front();return c;}
    void println(const String& s){host_log(s.c_str(),s.length());}
    template<class... Args>void printf(const char* format,Args... args){char text[1024];int n=std::snprintf(text,sizeof(text),format,args...);if(n>0)host_log(text,std::min<unsigned>(n,sizeof(text)-1));}
};
inline SimSerial Serial;
struct SimESP {
    const char* getChipModel(){return "HOST-SIMULATOR (not hardware)";}
    unsigned getChipRevision(){return 0;}
    unsigned getChipCores(){return 0;}
    unsigned getFlashChipSize(){return 4194304;}
    unsigned getPsramSize(){return 0;}
    unsigned getFreeHeap(){return 0;}
    void restart(){host_restart();}
};
inline SimESP ESP;
