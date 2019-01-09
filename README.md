This is a benchmark test that reads a single .bmp file (included in this folder)
and serial logs the results of the test. The following benchmark results came from
running the following program on a Teensy 3.6, driving a TFT display based on the 
ILI9341 display driver chip.

Both the SD and TFT display libraries used are added in this same repo.

Non-standard SD card libary used: github.com/PaulStoffregen/SD
Optimized ILI9341 driver library: github.com/PaulStoffregen/ILI9341_t3
  -- Thread talking about improvements: https://forum.pjrc.com/threads/29331-TFT-LCD-displaying-BMP-as-fast-as-possible-(ILI9341_t3)

Benchmark:
	Loading image 'rick_0.bmp'
	File size: 230454 Bytes
	Image Offset: 54
	Header size: 40
	Bit Depth: 24
	Image size: 240x320
	Loaded in 253 ms
	Seek: 3000
	Read: 190118
	Parse: 9948
	Draw: 46091

Based on Teensy's max SPI speed of ~24Mhz, amount of data it would take to send
a full 320x240 frame @24bit color, I'm expecting a ceiling of ~16fps.
I'm currently getting ~3fps.

Main issues are with read speeds. Currently having trouble moving away
from the aforementioned SD card library, due to the arduino standard library not
supporting the use of built-in microSD card, or rather my lack of knowledge on
how to do it properly. Even if I did, I don't know how much more efficient it is.
  