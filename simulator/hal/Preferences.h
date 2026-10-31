#pragma once
#include <Arduino.h>
extern "C" {
__attribute__((import_module("host"),import_name("pref_length"))) unsigned host_pref_length(const char*,const char*);
__attribute__((import_module("host"),import_name("pref_read"))) unsigned host_pref_read(const char*,const char*,void*,unsigned);
__attribute__((import_module("host"),import_name("pref_write"))) unsigned host_pref_write(const char*,const char*,const void*,unsigned);
}
class Preferences {
    std::string ns_;
public:
    bool begin(const char* ns,bool){ns_=ns;return true;}
    unsigned getBytesLength(const char* key){return host_pref_length(ns_.c_str(),key);}
    unsigned getBytes(const char* key,void* data,unsigned size){return host_pref_read(ns_.c_str(),key,data,size);}
    unsigned putBytes(const char* key,const void* data,unsigned size){return host_pref_write(ns_.c_str(),key,data,size);}
};
