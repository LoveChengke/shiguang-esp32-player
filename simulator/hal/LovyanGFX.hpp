#pragma once
#include <Arduino.h>
#include <SPI.h>
extern "C" {
__attribute__((import_module("host"),import_name("rect"))) void host_rect(int,int,int,int,int,int,int);
__attribute__((import_module("host"),import_name("circle"))) void host_circle(int,int,int,int,int);
__attribute__((import_module("host"),import_name("text"))) void host_text(const char*,int,int,int,int,int);
__attribute__((import_module("host"),import_name("image"))) void host_image(const uint8_t*,unsigned,int,int,int,int,float);
}
namespace fonts {struct Font{int size;};inline Font efontCN_12{12},efontCN_16{16},efontCN_24{24};}
namespace lgfx {
struct Bus_SPI {
    struct Config{int spi_host,spi_mode,freq_write,freq_read,pin_sclk,pin_mosi,pin_miso,pin_dc,dma_channel;bool use_lock;};
    Config config(){return {};}void config(Config){}
};
struct Panel_ILI9341 {
    struct Config{int pin_cs,pin_rst,pin_busy,panel_width,memory_width,panel_height,memory_height;bool readable,invert,rgb_order;};
    Config config(){return {};}void config(Config){}void setBus(Bus_SPI*){}
};
class LGFX_Device {
    int x_=0,y_=0,font_=16,color_=0,bg_=0;
public:
    void setPanel(Panel_ILI9341*){}void init(){}void setRotation(int){}void setTextWrap(bool){}void setTextSize(int){}
    void setFont(const fonts::Font* font){font_=font->size;}
    void setTextColor(int color,int bg){color_=color;bg_=bg;}
    void setCursor(int x,int y){x_=x;y_=y;}
    int textWidth(const String& value){int width=0;const auto* p=reinterpret_cast<const unsigned char*>(value.c_str());while(*p){if(*p<128){width+=font_/2;++p;}else{width+=font_;++p;while((*p&0xc0)==0x80)++p;}}return width;}
    void print(const String& value){host_text(value.c_str(),x_,y_,font_,color_,bg_);x_+=textWidth(value);}
    void fillScreen(int color){host_rect(0,0,240,320,color,0,0);}
    void fillRect(int x,int y,int w,int h,int c){host_rect(x,y,w,h,c,0,0);}
    void drawRect(int x,int y,int w,int h,int c){host_rect(x,y,w,h,c,0,1);}
    void drawFastHLine(int x,int y,int w,int c){fillRect(x,y,w,1,c);}
    void drawFastVLine(int x,int y,int h,int c){fillRect(x,y,1,h,c);}
    void fillRoundRect(int x,int y,int w,int h,int r,int c){host_rect(x,y,w,h,c,r,0);}
    void drawRoundRect(int x,int y,int w,int h,int r,int c){host_rect(x,y,w,h,c,r,1);}
    void drawCircle(int x,int y,int r,int c){host_circle(x,y,r,c,0);}
    void fillCircle(int x,int y,int r,int c){host_circle(x,y,r,c,1);}
    bool drawJpg(const uint8_t* data,unsigned n,int x,int y,int w,int h,int,int,float scale){host_image(data,n,x,y,w,h,scale);return true;}
    bool drawPng(const uint8_t* data,unsigned n,int x,int y,int w,int h,int,int,float scale){host_image(data,n,x,y,w,h,scale);return true;}
};
}
