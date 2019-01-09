/***************************************************
  This is a benchmark test that reads a single .bmp file (included in this folder)
  and serial logs the results of the test. The following benchmark results came from
  running the following program on a Teensy 3.6, driving a TFT display based on the 
  ILI9341 display driver chip.
  
  Non-standard SD card libary used: github.com/PaulStoffregen/SD
    -- 
  Optimized ILI9341 driver library: github.com/PaulStoffregen/ILI9341_t3
  
  Benchmark:
	Loading image 'rick_0.bmp'
	File size: 230454
	Image Offset: 54
	Header size: 40
	Bit Depth: 24
	Image size: 240x320
	Loaded in 253 ms
	Seek: 3000
	Read: 190118
	Parse: 9948
	Draw: 46091
	
  Main issues are with read speeds. Currently having trouble moving away
  from the aforementioned SD card library, due to the arduino standard library not
  supporting the use of built-in microSD card, or rather my lack of knowledge on
  how to do it properly. Even if I did, I don't know how much more efficient it is.
 ****************************************************/


#include <ILI9341_t3.h> // Hardware-specific library
#include <SD.h>
#include <SPI.h>

// TFT display and SD card will share the hardware SPI interface.
// Hardware SPI pins are specific to the Arduino board type and
// cannot be remapped to alternate pins.  For Arduino Uno,
// Duemilanove, etc., pin 11 = MOSI, pin 12 = MISO, pin 13 = SCK.

#define TFT_DC  9
#define TFT_CS 10
ILI9341_t3 tft = ILI9341_t3(TFT_CS, TFT_DC);

//#define SD_CS 4
// List of folders, where each folder contains the bitmaps for a single gif.


void setup(void) {
  tft.begin();
  tft.fillScreen(ILI9341_BLUE);

  Serial.begin(9600);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  forceSerialWait(tft);
  
  Serial.print(F("Initializing SD card..."));
  tft.println(F("Init SD card..."));
  while (!SD.begin(BUILTIN_SDCARD)) {
    Serial.println(F("failed to access SD card!"));
    tft.println(F("failed to access SD card!"));
    delay(2000);
  }
  Serial.println("OK!");
}

void forceSerialWait(ILI9341_t3 tft) {
  tft.println(F("Waiting for Arduino Serial Monitor..."));
  while (!Serial) {
    if (millis() > 8000) break;
  }
}


bool ranOnce = false;
void loop() {
  if (!ranOnce) {bmpDraw("rick_0.bmp", 0, 0);}
  ranOnce = true;
}

// This function opens a Windows Bitmap (BMP) file and
// displays it at the given coordinates.  It's sped up
// by reading many pixels worth of data at a time
// (rather than pixel by pixel).  Increasing the buffer
// size takes more of the Arduino's precious RAM but
// makes loading a little faster.  20 pixels seems a
// good balance for tiny AVR chips.

// Larger buffers are slightly more efficient, but if
// the buffer is too large, extra data is read unnecessarily.
// For example, if the image is 240 pixels wide, a 100
// pixel buffer will read 3 groups of 100 pixels.  The
// last 60 pixels from the 3rd read may not be used.
 #define BUFFPIXEL 240
//===========================================================
// Try Draw using writeRect
void bmpDraw(char *filename, uint8_t x, uint16_t y) {

  File     bmpFile;
  int      bmpWidth, bmpHeight;   // W+H in pixels
  uint8_t  bmpDepth;              // Bit depth (currently must be 24)
  uint32_t bmpImageoffset;        // Start of image data in file
  uint32_t rowSize;               // Not always = bmpWidth; may have padding
  uint8_t  sdbuffer[3*BUFFPIXEL]; // pixel buffer (R+G+B per pixel)
  uint16_t  buffidx = sizeof(sdbuffer); // Current position in sdbuffer
  boolean  goodBmp = false;       // Set to true on valid header parse
  boolean  flip    = true;        // BMP is stored bottom-to-top
  int      w, h, row, col;
  uint8_t  r, g, b;
  uint32_t pos = 0, startTime = millis();

  uint16_t awColors[320];  // hold colors for one row at a time...

  if((x >= tft.width()) || (y >= tft.height())) return;

  Serial.print(F("Loading image '"));
  Serial.print(filename);
  Serial.println('\'');

  // Open requested file on SD card
  if ((bmpFile = SD.open(filename)) == NULL) {
    Serial.print(F("File not found"));
    return;
  }

  elapsedMicros usec;
  uint32_t us;
  uint32_t total_seek = 0;
  uint32_t total_read = 0;
  uint32_t total_parse = 0;
  uint32_t total_draw = 0;

  // Parse BMP header
  if(read16(bmpFile) == 0x4D42) { // BMP signature
    Serial.print(F("File size: ")); Serial.println(read32(bmpFile));
    (void)read32(bmpFile); // Read & ignore creator bytes
    bmpImageoffset = read32(bmpFile); // Start of image data
    Serial.print(F("Image Offset: ")); Serial.println(bmpImageoffset, DEC);
    // Read DIB header
    Serial.print(F("Header size: ")); Serial.println(read32(bmpFile));
    bmpWidth  = read32(bmpFile);
    bmpHeight = read32(bmpFile);
    if(read16(bmpFile) == 1) { // # planes -- must be '1'
      bmpDepth = read16(bmpFile); // bits per pixel
      Serial.print(F("Bit Depth: ")); Serial.println(bmpDepth);
      if((bmpDepth == 24) && (read32(bmpFile) == 0)) { // 0 = uncompressed

        goodBmp = true; // Supported BMP format -- proceed!
        Serial.print(F("Image size: "));
        Serial.print(bmpWidth);
        Serial.print('x');
        Serial.println(bmpHeight);

        // BMP rows are padded (if needed) to 4-byte boundary
        rowSize = (bmpWidth * 3 + 3) & ~3;

        // If bmpHeight is negative, image is in top-down order.
        // This is not canon but has been observed in the wild.
        if(bmpHeight < 0) {
          bmpHeight = -bmpHeight;
          flip      = false;
        }

        // Crop area to be loaded
        w = bmpWidth;
        h = bmpHeight;
        if((x+w-1) >= tft.width())  w = tft.width()  - x;
        if((y+h-1) >= tft.height()) h = tft.height() - y;

        usec = 0;
        for (row=0; row<h; row++) { // For each scanline...

          // Seek to start of scan line.  It might seem labor-
          // intensive to be doing this on every line, but this
          // method covers a lot of gritty details like cropping
          // and scanline padding.  Also, the seek only takes
          // place if the file position actually needs to change
          // (avoids a lot of cluster math in SD library).
          if(flip) // Bitmap is stored bottom-to-top order (normal BMP)
            pos = bmpImageoffset + (bmpHeight - 1 - row) * rowSize;
          else     // Bitmap is stored top-to-bottom
            pos = bmpImageoffset + row * rowSize;
          if(bmpFile.position() != pos) { // Need seek?
            bmpFile.seek(pos);
            buffidx = sizeof(sdbuffer); // Force buffer reload
          }
          us = usec;
          usec -= us;
          total_seek += us;

          for (col=0; col<w; col++) { // For each pixel...
            // Time to read more pixel data?
            if (buffidx >= sizeof(sdbuffer)) { // Indeed
              us = usec;
              usec -= us;
              total_parse += us;
              bmpFile.read(sdbuffer, sizeof(sdbuffer));
              buffidx = 0; // Set index to beginning
              us = usec;
              usec -= us;
              total_read += us;
            }

            // Convert pixel from BMP to TFT format, push to display
            b = sdbuffer[buffidx++];
            g = sdbuffer[buffidx++];
            r = sdbuffer[buffidx++];
            awColors[col] = tft.color565(r,g,b);
          } // end pixel
          us = usec;
          usec -= us;
          total_parse += us;
          tft.writeRect(0, row, w, 1, awColors);
          us = usec;
          usec -= us;
          total_draw += us;
        } // end scanline
        Serial.print(F("Loaded in "));
        Serial.print(millis() - startTime);
        Serial.println(" ms");
        Serial.print("Seek: ");
        Serial.println(total_seek);
        Serial.print("Read: ");
        Serial.println(total_read);
        Serial.print("Parse: ");
        Serial.println(total_parse);
        Serial.print("Draw: ");
        Serial.println(total_draw);
      } // end goodBmp
    }
  }

  bmpFile.close();
  if(!goodBmp) Serial.println(F("BMP format not recognized."));
}

// These read 16- and 32-bit types from the SD card file.
// BMP data is stored little-endian, Arduino is little-endian too.
// May need to reverse subscript order if porting elsewhere.

uint16_t read16(File &f) {
  uint16_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read(); // MSB
  return result;
}

uint32_t read32(File &f) {
  uint32_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read();
  ((uint8_t *)&result)[2] = f.read();
  ((uint8_t *)&result)[3] = f.read(); // MSB
  return result;
}
