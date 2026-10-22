#pragma once
struct SpiFake { void begin(int, int, int, int) {} };
inline SpiFake SPI;
