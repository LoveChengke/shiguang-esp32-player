#pragma once
#include "FreeRTOS.h"
// Browser execution is single-threaded. These are compatibility boundaries only.
inline SemaphoreHandle_t xSemaphoreCreateMutex(){return reinterpret_cast<void*>(1);}
inline bool xSemaphoreTake(SemaphoreHandle_t,uint32_t){return true;}
inline bool xSemaphoreGive(SemaphoreHandle_t){return true;}
