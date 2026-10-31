#pragma once
constexpr int HSPI=1,SPI2_HOST=2,SPI3_HOST=3,SPI_DMA_CH_AUTO=0;
struct SPIClass{explicit SPIClass(int){}void begin(int,int,int,int){}};
