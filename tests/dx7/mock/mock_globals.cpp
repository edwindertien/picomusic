#include "Arduino.h"
#include "SPI.h"
uint64_t g_mock_us = 0;
bool     g_mock_real = false;
void   (*g_delay_us_hook)(uint32_t) = nullptr;
uint8_t  g_pin[64] = {0};
void (*g_pin_hook)(int, int) = nullptr;
MockSerial Serial;
SPIClass SPI1;
