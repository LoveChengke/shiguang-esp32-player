#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <Arduino.h>
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>
#include <PocketCore.hpp>
#include <algorithm>
#include "audio_service.hpp"
#include "board.hpp"
#include "reader.hpp"
#include "music_info.hpp"

class Display : public lgfx::LGFX_Device {
    lgfx::Panel_ILI9341 panel;
    lgfx::Bus_SPI bus;
public:
    Display() {
        auto b = bus.config();
#if defined(POCKET_BOARD_S3)
        b.spi_host = SPI2_HOST;
#else
        b.spi_host = SPI3_HOST;
#endif
        b.spi_mode = 0; b.freq_write = 40000000; b.freq_read = 16000000;
        b.pin_sclk = board::tftSck; b.pin_mosi = board::tftMosi; b.pin_miso = -1;
        b.pin_dc = board::tftDc; b.dma_channel = SPI_DMA_CH_AUTO; b.use_lock = true;
        bus.config(b); panel.setBus(&bus);
        auto p = panel.config();
        p.pin_cs = board::tftCs; p.pin_rst = board::tftReset; p.pin_busy = -1;
        p.panel_width = p.memory_width = 240; p.panel_height = p.memory_height = 320;
        p.readable = false; p.invert = false; p.rgb_order = false;
        panel.config(p); setPanel(&panel);
    }
};
namespace {
Display lcd;
SPIClass sdSpi(HSPI);
SemaphoreHandle_t sdMutex;
Preferences prefs;
Reader reader;
MusicInfo musicInfo;
std::vector<String> music, books;
std::vector<int> playlist;
pocket::Button buttons[board::keyCount];
enum class View { Home, Music, Books, Player, Reader, Settings };
View view = View::Home;
struct LibraryItem { String path; int index; }; // -1 folder, -2 saved playlist
std::vector<LibraryItem> library;
String libraryFolder="/music", playbackFolder="/music";
bool libraryPlaylist=false;
int selected=0, homeSelected=0, settingSelected=0;
int readerControl=0, readerPercent=0, clockEdit=0;
bool readerEditing=false;
uint8_t volume=10, repeatMode=0;
bool sdReady=false, audioReady=false, paper=false, redraw=true;
bool settingsDirty=false, catalogTruncated=false;
uint32_t settingsAt=0,lastRefresh=0,finishedSeen=0,toastAt=0;
unsigned initialMinutes=12*60,clockBase=0,clockAt=0,lastMinute=UINT32_MAX;
String toast;
uint16_t bg,card,ink,muted;
constexpr uint16_t accent=0x35F8;
const char* modes[]={"全部循环","文件夹循环","自选列表循环","单曲循环"};

void palette(){bg=paper?0xF79C:0x0841;card=paper?0xE6F9:0x10C3;ink=paper?0x18C3:0xEF7D;muted=paper?0x6B6D:0x8C71;}
String basename(const String& path){int slash=path.lastIndexOf('/');return slash<0?path:path.substring(slash+1);}
String parent(const String& path){int slash=path.lastIndexOf('/');return slash<0?String():path.substring(0,slash);}
bool same(const String& a,const String& b){return a.compareTo(b)==0;}
unsigned clockMinutes(){return (clockBase+uint32_t(millis()-clockAt)/60000)%1440;}
String clockText(){char value[8];auto t=clockMinutes();snprintf(value,sizeof(value),"%02u:%02u",t/60,t%60);return value;}
String fit(const String& value,unsigned width){
    if(lcd.textWidth(value)<=int(width))return value;
    String out;std::string_view v(value.c_str());
    for(size_t at=0;at<v.size();){auto g=pocket::decode(v,at);String next=out+value.substring(at,at+g.bytes);if(lcd.textWidth(next+"...")>int(width))break;out=next;at+=g.bytes;}
    return out+"...";
}
void text(const String& value,int x,int y,uint16_t color){lcd.setTextColor(color,bg);lcd.setCursor(x,y);lcd.print(value);}
void notice(const String& value){toast=value;toastAt=millis();redraw=true;Serial.println(value);}
void saveSettings(){
    if(!settingsDirty)return;
    auto t=clockMinutes();uint8_t data[]={2,volume,uint8_t(reader.font),repeatMode,uint8_t(paper),uint8_t(t/60),uint8_t(t%60)};
    if(prefs.putBytes("settings",data,sizeof(data))==sizeof(data))settingsDirty=false;
    else{settingsAt=millis();notice("设置保存失败");}
}
void changedSettings(){settingsDirty=true;settingsAt=millis();redraw=true;}
void setVolume(int value){uint8_t next=constrain(value,0,21);if(audioReady&&!player::volume(next)){notice("操作繁忙，请稍后再试");return;}volume=next;changedSettings();}
bool listed(int track){return std::find(playlist.begin(),playlist.end(),track)!=playlist.end();}
void togglePlaylist(int track){
    auto candidate=playlist;auto at=std::find(candidate.begin(),candidate.end(),track);
    if(at==candidate.end())candidate.push_back(track);else candidate.erase(at);
    std::vector<uint64_t> saved{1};for(auto index:candidate)saved.push_back(pocket::hash64(music[index].c_str()));
    if(prefs.putBytes("playlist",saved.data(),saved.size()*sizeof(uint64_t))!=saved.size()*sizeof(uint64_t)){notice("播放列表保存失败");return;}
    playlist=std::move(candidate);notice(listed(track)?"已加入播放列表":"已移出播放列表");
}
void scan(const String& root,unsigned depth=0){
    if(depth>3)return;
    File dir=SD.open(root);if(!dir||!dir.isDirectory())return;
    while(File entry=dir.openNextFile()){
        String name=entry.name();String path=name.startsWith("/")?name:root+"/"+name;bool directory=entry.isDirectory();entry.close();
        if(basename(path).startsWith("."))continue;
        if(path.length()>=board::maxPathBytes){catalogTruncated=true;continue;}
        if(directory)scan(path,depth+1);
        else{bool songs=root.startsWith("/music");auto& target=songs?music:books;
            if(pocket::extension(path.c_str(),songs?".mp3":".txt")){if(target.size()<board::maxFiles)target.push_back(path);else catalogTruncated=true;}}
    }
    dir.close();
}
void buildLibrary(){
    library.clear();auto& items=view==View::Music?music:books;
    if(libraryPlaylist){for(int i:playlist)library.push_back({music[i],i});}
    else{
        for(unsigned i=0;i<items.size();++i){
            String prefix=libraryFolder+"/";if(!items[i].startsWith(prefix))continue;
            String rest=items[i].substring(prefix.length());int slash=-1;
            for(unsigned j=0;j<rest.length();++j)if(rest.c_str()[j]=='/'){slash=j;break;}
            if(slash<0)library.push_back({items[i],int(i)});
            else{String folder=prefix+rest.substring(0,slash);bool exists=false;for(const auto& item:library)if(same(item.path,folder)){exists=true;break;}if(!exists)library.push_back({folder,-1});}
        }
        if(view==View::Music&&same(libraryFolder,"/music"))library.push_back({"我的播放列表",-2});
    }
    selected=constrain(selected,0,std::max(0,int(library.size())-1));
}
void enterLibrary(bool songs){view=songs?View::Music:View::Books;libraryFolder=songs?"/music":"/books";libraryPlaylist=false;selected=0;buildLibrary();redraw=true;}
void startTrack(int index){
    if(music.empty()||!audioReady){notice("请先放入 MP3 文件");return;}
    index=(index+int(music.size()))%int(music.size());
    if(!player::play(music[index].c_str(),index))notice("播放命令未提交，请重试");redraw=true;
}
void adjacentTrack(int direction){
    int current=player::status().track;std::vector<int> queue;
    for(int i=0;i<int(music.size());++i)if(repeatMode==2?listed(i):repeatMode==1?same(parent(music[i]),playbackFolder):true)queue.push_back(i);
    if(repeatMode==2)queue=playlist;
    if(queue.empty()){notice("播放列表为空，请在音乐库长按确认添加");return;}
    auto it=std::find(queue.begin(),queue.end(),current);int at=it==queue.end()?(direction>0?-1:0):it-queue.begin();
    startTrack(queue[(at+direction+int(queue.size()))%int(queue.size())]);
}
void togglePause(){auto s=player::status();if(!audioReady){notice("音频任务不可用");return;}if(s.state==AudioState::Playing||s.state==AudioState::Paused){if(!player::pause())notice("操作繁忙，请稍后再试");}else if(repeatMode==2)adjacentTrack(1);else startTrack(s.track>=0?s.track:0);redraw=true;}
void seekMusic(int direction){auto s=player::status();if(!s.durationMs){notice("尚无可调整的歌曲进度");return;}int step=std::max(1U,1000000U/s.durationMs);if(!player::seek(constrain(int(s.progress)+direction*step,0,99)))notice("进度调整失败，请稍后重试");redraw=true;}
void header(const String& title,const String& subtitle){
    lcd.setFont(&fonts::efontCN_16);text(fit(title,164),12,10,ink);
    lcd.setFont(&fonts::efontCN_12);text(clockText(),196,13,accent);
    lcd.setFont(&fonts::efontCN_16);text(fit(subtitle,216),12,32,muted);lcd.drawFastHLine(12,55,216,card);
}
void footer(const String& message=""){
    String value=toast.length()?toast:message;if(!value.length())return;
    lcd.setFont(&fonts::efontCN_12);text(fit(value,216),12,301,muted);
}
void bar(int y,unsigned percent){lcd.fillRoundRect(12,y,216,4,2,card);if(percent)lcd.fillRect(12,y,216*std::min(100U,percent)/100,4,accent);}
String duration(uint32_t ms){char value[20];snprintf(value,sizeof(value),"%u:%02u",ms/60000,ms/1000%60);return value;}
void drawHome(){
    header("拾光 · 随身播放器",sdReady?"本地音乐与书页，随时继续":"未检测到存储卡");
    const String titles[]={"听音乐","读小说","偏好设置"};const String subtitles[]={String(music.size())+" 首 MP3",String(books.size())+" 本 TXT","音量 / 字号 / 时钟"};
    for(int i=0;i<3;++i){int y=68+i*67;lcd.fillRoundRect(12,y,216,57,7,card);if(homeSelected==i)lcd.drawRoundRect(12,y,216,57,7,accent);
        lcd.setFont(&fonts::efontCN_16);lcd.setTextColor(ink,card);lcd.setCursor(24,y+8);lcd.print(titles[i]);lcd.setFont(&fonts::efontCN_12);lcd.setTextColor(muted,card);lcd.setCursor(24,y+33);lcd.print(subtitles[i]);}
    footer();
}
void drawLibrary(bool songs){
    header(songs?"音乐库":"我的书架",libraryPlaylist?"我的播放列表 · "+String(playlist.size())+" 首":libraryFolder);
    if(library.empty())text("文件夹为空",16,112,muted);
    for(int i=selected/6*6;i<std::min(selected/6*6+6,int(library.size()));++i){
        int y=66+i%6*35;const auto& item=library[i];if(i==selected)lcd.fillRoundRect(10,y,220,31,5,card);
        lcd.setTextColor(i==selected?accent:ink,i==selected?card:bg);lcd.setCursor(16,y+7);
        String label=item.index==-1?"[+] "+basename(item.path):item.index==-2?"[*] "+item.path:(songs&&listed(item.index)?"* ":"")+basename(item.path);lcd.print(fit(label,207));
    }
    lcd.setFont(&fonts::efontCN_12);text(String(library.size())+(songs?" 项  ·  长确认增删自选":" 项"),12,282,muted);footer();
}
void drawPlayer(){
    auto s=player::status();header("正在播放",modes[repeatMode]);
    bool cover=!musicInfo.cover.empty();
    if(cover){float scale=96.0f/std::max(musicInfo.width,musicInfo.height);int x=(240-int(musicInfo.width*scale))/2;
        if(musicInfo.png)lcd.drawPng(musicInfo.cover.data(),musicInfo.cover.size(),x,65,96,96,0,0,scale);
        else lcd.drawJpg(musicInfo.cover.data(),musicInfo.cover.size(),x,65,96,96,0,0,scale);
    }else{lcd.drawCircle(120,109,35,accent);lcd.drawCircle(120,109,24,card);lcd.fillCircle(120,109,4,accent);}
    String title=s.track>=0&&s.track<int(music.size())?basename(music[s.track]):"选择一首音乐开始";
    lcd.setFont(&fonts::efontCN_16);text(fit(title,216),12,170,ink);
    if(!musicInfo.lines.empty()){
        unsigned line=musicInfo.current(s.elapsedMs);lcd.setFont(&fonts::efontCN_12);
        text(fit(musicInfo.lines[line].text.c_str(),216),12,196,accent);
        if(line+1<musicInfo.lines.size())text(fit(musicInfo.lines[line+1].text.c_str(),216),12,213,muted);
    }
    bar(242,s.progress);lcd.setFont(&fonts::efontCN_12);text(duration(s.elapsedMs)+" / "+duration(s.durationMs),12,252,muted);text(String(s.progress)+"%",198,252,muted);
    const char* label=s.state==AudioState::Playing?"播放中":s.state==AudioState::Paused?"已暂停":s.state==AudioState::Error?"解码失败":"已停止";
    text(label,12,278,muted);text("音量 "+String(volume)+" / 21",136,278,ink);footer(musicInfo.warning);
}
void drawReader(){
    header("阅读 "+String(reader.progress())+"%",basename(reader.path));lcd.setFont(reader.font==24?&fonts::efontCN_24:&fonts::efontCN_16);lcd.setTextColor(ink,bg);
    int y=64,lineHeight=reader.font==24?30:22;for(const auto& line:reader.page.lines){lcd.setCursor(12,y);lcd.print(line.c_str());y+=lineHeight;}
    bar(287,reader.progress());
    if(readerControl){
        lcd.fillRoundRect(12,112,216,132,8,card);lcd.drawRoundRect(12,112,216,132,8,accent);lcd.setFont(&fonts::efontCN_16);
        lcd.setTextColor(muted,card);lcd.setCursor(24,125);lcd.print(readerEditing?"上下调整 · 确认应用":"阅读设置");
        lcd.setTextColor(readerControl==1?accent:ink,card);lcd.setCursor(24,159);lcd.print("进度   "+String(readerPercent)+"%");
        lcd.setTextColor(readerControl==2?accent:ink,card);lcd.setCursor(24,199);lcd.print("字号   "+String(reader.font));
    }
    footer(reader.error.length()?reader.error:reader.searching()?"正在定位上一页":reader.page.invalidUtf8?"请先转换为 UTF-8":"");
}
void drawSettings(){
    header("偏好设置","设置与书签保存在主板");
    String rows[]={"音量       "+String(volume)+" / 21","小说字号   "+String(reader.font),"播放模式   "+String(modes[repeatMode]),String("界面主题   ")+(paper?"暖纸":"深夜"),"时间       "+clockText()+(clockEdit==1?" 时":clockEdit==2?" 分":"")};
    for(int i=0;i<5;++i){int y=68+i*43;if(i==settingSelected)lcd.fillRoundRect(12,y,216,38,6,card);lcd.setTextColor(i==settingSelected?accent:ink,i==settingSelected?card:bg);lcd.setCursor(22,y+10);lcd.print(rows[i]);}footer();
}
void draw(){palette();lcd.fillScreen(bg);lcd.setFont(&fonts::efontCN_16);switch(view){case View::Home:drawHome();break;case View::Music:drawLibrary(true);break;case View::Books:drawLibrary(false);break;case View::Player:drawPlayer();break;case View::Reader:drawReader();break;case View::Settings:drawSettings();break;}redraw=false;}
void event(int key,pocket::Press press){
    if(press==pocket::Press::None)return;
    int direction=key==0||key==5?-1:1;
    if(press==pocket::Press::Long){
        if(key==2&&view==View::Music&&!library.empty()&&library[selected].index>=0){togglePlaylist(library[selected].index);buildLibrary();}
        else if(key<2)setVolume(int(volume)+(key==0?1:-1));
        else if(key==5||key==6){if(view==View::Player)adjacentTrack(direction);}
        else if(key==2||key==4)togglePause();
        else{reader.save();saveSettings();view=View::Home;readerControl=0;redraw=true;}return;
    }
    if(key==4){readerControl=0;if(view==View::Player){if(reader.active())view=View::Reader;else enterLibrary(false);}else view=View::Player;redraw=true;return;}
    if(key==3){
        if(view==View::Reader&&readerControl){readerControl=0;readerEditing=false;}
        else{reader.save();saveSettings();clockEdit=0;
            if(view==View::Reader){reader.close();enterLibrary(false);}
            else if(view==View::Player)enterLibrary(true);
            else if((view==View::Music||view==View::Books)&&(libraryPlaylist||!same(libraryFolder,view==View::Music?"/music":"/books"))){if(libraryPlaylist)libraryPlaylist=false;else libraryFolder=parent(libraryFolder);selected=0;buildLibrary();}
            else view=View::Home;}
        redraw=true;return;
    }
    if(view==View::Home){if(key<2)homeSelected=(homeSelected+direction+3)%3;else if(key==2){if(!sdReady&&homeSelected!=2){ESP.restart();return;}if(homeSelected<2)enterLibrary(homeSelected==0);else view=View::Settings;}}
    else if(view==View::Music||view==View::Books){
        if(library.empty())return;
        if(key<2)selected=(selected+direction+int(library.size()))%int(library.size());
        else if(key==2){auto item=library[selected];
            if(item.index<0){if(item.index==-2)libraryPlaylist=true;else libraryFolder=item.path;selected=0;buildLibrary();}
            else if(view==View::Music){repeatMode=libraryPlaylist?2:same(libraryFolder,"/music")?0:1;playbackFolder=parent(item.path);changedSettings();startTrack(item.index);view=View::Player;}
            else if(reader.open(item.path)){view=View::Reader;readerControl=0;}else notice(reader.error);}
    }else if(view==View::Player){if(key==2)togglePause();else if(key<2)setVolume(int(volume)+(key==0?1:-1));else if(key==5||key==6)seekMusic(direction);}
    else if(view==View::Reader){
        if(readerControl){
            if(key==2){if(!readerEditing)readerEditing=true;else{if(readerControl==1)reader.seekPercent(readerPercent);readerControl=0;readerEditing=false;}}
            else if(key<2||key==5||key==6){if(!readerEditing)readerControl=readerControl==1?2:1;else if(readerControl==1)readerPercent=constrain(readerPercent+direction,0,100);else{reader.setFont(reader.font==16?24:16);changedSettings();}}
        }else if(key==0)reader.previous();else if(key==1)reader.next();else if(key==2){readerControl=1;readerEditing=false;readerPercent=reader.progress();}else if(key==5||key==6)reader.seekPercent(constrain(int(reader.progress())+direction*5,0,100));
    }else if(view==View::Settings){
        if(clockEdit){if(key==2){if(++clockEdit==3){clockEdit=0;changedSettings();}}else if(key<2||key==5||key==6){int t=clockMinutes();clockBase=(t+direction*(clockEdit==1?60:1)+1440)%1440;clockAt=millis();changedSettings();}}
        else if(key<2)settingSelected=(settingSelected+direction+5)%5;
        else if(key==2){if(settingSelected==0)setVolume((volume+1)%22);else if(settingSelected==1)reader.setFont(reader.font==16?24:16);else if(settingSelected==2){repeatMode=(repeatMode+1)%4;auto s=player::status();if(s.track>=0)playbackFolder=parent(music[s.track]);}else if(settingSelected==3)paper=!paper;else clockEdit=1;changedSettings();}
    }
    redraw=true;
}
void serialInput(){
    for(unsigned n=0;Serial.available()&&n<8;++n){char c=Serial.read();const char* keys="udobmlr";const char* key=strchr(keys,c);if(c&&key)event(key-keys,pocket::Press::Short);else if(c=='p')togglePause();else if(c=='+')setVolume(volume+1);else if(c=='-')setVolume(volume-1);else if(c=='?')Serial.printf("STATE view=%d heap=%u sd=%d music=%u books=%u audio=%d bookByte=%u\n",int(view),ESP.getFreeHeap(),sdReady,unsigned(music.size()),unsigned(books.size()),int(player::status().state),reader.offset);}
}
}
void setup() {
    Serial.begin(115200); delay(200);
    Serial.printf("\nPOCKET boot: chip=%s rev=%u cores=%u flash=%u psram=%u heap=%u\n", ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores(), ESP.getFlashChipSize(), ESP.getPsramSize(), ESP.getFreeHeap());
    Serial.printf("Reference wiring: %s\n", board::name);
    for (int pin : board::buttons) pinMode(pin, INPUT);
    sdMutex = xSemaphoreCreateMutex();
    if (!sdMutex) { Serial.println("FATAL SD mutex allocation failed"); return; }
    bool preferencesOk = prefs.begin("pocket", false);
    if (!preferencesOk) Serial.println("WARNING NVS unavailable: progress cannot be saved");
    clockBase=initialMinutes;clockAt=millis();
    uint8_t settings[7]{};size_t length=prefs.getBytesLength("settings");
    if(length==5||length==7){prefs.getBytes("settings",settings,length);
        if(settings[0]==1||settings[0]==2){volume=std::min<uint8_t>(21,settings[1]);reader.font=settings[2]==24?24:16;repeatMode=settings[0]==1?(settings[3]?3:0):std::min<uint8_t>(3,settings[3]);paper=settings[4];if(length==7&&settings[5]<24&&settings[6]<60)clockBase=settings[5]*60+settings[6];}
    }
    musicInfo.begin(sdMutex);
    reader.begin(sdMutex, prefs);
    lcd.init(); lcd.setRotation(0); lcd.setTextWrap(false); lcd.setTextSize(1);
    sdSpi.begin(board::sdSck, board::sdMiso, board::sdMosi, board::sdCs);
    sdReady = SD.begin(board::sdCs, sdSpi, 10000000);
    if (sdReady) {
        scan("/music"); scan("/books");
        auto compare = [](const String& a, const String& b) { return a.compareTo(b) < 0; };
        std::sort(music.begin(), music.end(), compare); std::sort(books.begin(), books.end(), compare);
        size_t playlistBytes=prefs.getBytesLength("playlist");
        if(playlistBytes>=8&&playlistBytes<=129*8&&playlistBytes%8==0){std::vector<uint64_t> saved(playlistBytes/8);prefs.getBytes("playlist",saved.data(),playlistBytes);if(saved[0]==1)for(size_t n=1;n<saved.size();++n)for(unsigned i=0;i<music.size();++i)if(saved[n]==pocket::hash64(music[i].c_str())&&!listed(i))playlist.push_back(i);}
        audioReady = player::begin(sdMutex);
        if (audioReady) player::volume(volume);
    }
    Serial.printf("READY sd=%d audio=%d tracks=%u books=%u heap=%u\n", sdReady, audioReady, unsigned(music.size()), unsigned(books.size()), ESP.getFreeHeap());
    if (!sdReady) notice("未检测到 SD 卡，插卡后按确认重试");
    else if (catalogTruncated) notice("部分文件超出数量、路径或目录限制");
    else if (!audioReady) notice("音频任务创建失败");
    draw();
}
void loop() {
    if (!sdMutex) { delay(100); return; }
    const uint32_t now = millis();
    for (unsigned i = 0; i < board::keyCount; ++i) event(i, buttons[i].update(digitalRead(board::buttons[i]) == LOW, now));
    serialInput();
    if (reader.active() && reader.tick()) redraw = true;
    if (settingsDirty && now - settingsAt >= 750) saveSettings();
    auto state = player::status();
    if (state.finished != finishedSeen) {
        finishedSeen = state.finished;
        if(state.track>=0){if(repeatMode==3)startTrack(state.track);else adjacentTrack(1);}
    }
    if(state.track>=0&&state.track<int(music.size())&&state.track!=musicInfo.track){musicInfo.load(music[state.track],state.track);redraw=true;}
    if(clockMinutes()!=lastMinute){lastMinute=clockMinutes();redraw=true;}
    if (toast.length() && now - toastAt >= 2500) { toast = ""; redraw = true; }
    if (view == View::Player && now - lastRefresh >= 500) { redraw = true; lastRefresh = now; }
    if (redraw) draw();
    delay(2);
}
