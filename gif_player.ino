#include <ILI9341_t3.h> // Hardware-specific library
#include <SD.h>
#include <SPI.h>
#include <Time.h>
#include <URTouch.h>
#include <URTouchCD.h>

// TFT display and SD card will share the hardware SPI interface.
// Hardware SPI pins are specific to the Arduino board type and
// cannot be remapped to alternate pins.  For Arduino Uno,
// Duemilanove, etc., pin 11 = MOSI, pin 12 = MISO, pin 13 = SCK.

#define TFT_DC  9
#define TFT_CS 10

#define TCLK 19
#define TCS 20
#define TDIN 21
#define DOUT 22
#define IRQ 23

#define BUFFPIXEL 240
#define MAX_GIFS 32

int touchX, touchY;
long lastTouchMs = -1;
int dotLoopCount;
ILI9341_t3 tft = ILI9341_t3(TFT_CS, TFT_DC);
URTouch myTouch(IRQ, DOUT, TDIN, TCS, TCLK);

// List of folders, where each folder contains the bitmaps for a single gif.
String folders[MAX_GIFS];
int foldersLength = 0;

void setup(void) {
  // Keep the SD card inactive while working the display.
  //pinMode(SD_CS, INPUT_PULLUP);
  //delay(200);

  tft.begin();
  tft.fillScreen(ILI9341_BLUE);

  Serial.begin(9600);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  forceSerialWait(tft);

  tft.println(F("Init SD card..."));
  while (!SD.begin(BUILTIN_SDCARD)) {
    //Serial.println(F("failed to access SD card!"));
    tft.println(F("failed to access SD card!"));
    delay(2000);
  }

  Serial.println("starting file parsing");
  buildBmpDirectoryList();
  processAllAnimations();

  myTouch.InitTouch();
  myTouch.setPrecision(PREC_MEDIUM);
  Serial.println("OK!");
}

// Displays a black screen with white text, notifying
// The user that bitmaps are currently being parsed into
// RAW files.
void updateLoadingScreen() {
  tft.fillScreen(ILI9341_BLACK);
  tft.setCursor(60,120);
  tft.print(F("Processing\n      bitmaps"));

  for(int i = 0; i < (dotLoopCount % 4); i++) {
    tft.fillRect(100 + i*10, 160, 5, 5, ILI9341_WHITE);
  }
  dotLoopCount++;
}

void forceSerialWait(ILI9341_t3 tft) {
  tft.println(F("Waiting for Arduino Serial Monitor..."));
  while (!Serial) {
    if (millis() > 8000) break;
  }
}

void buildBmpDirectoryList() {
  File root = SD.open("/");

  for (int i = 0; foldersLength < MAX_GIFS; i++) {
    // NOTE: NAMES MUST BE 8 OR LESS CHARS!!
    File entry =  root.openNextFile();
    if (!entry) {
      // no more files
      break;
    }

    if (!entry.isDirectory() ||
        strcmp(entry.name(), "RAW") == 0 ||
        strcmp(entry.name(), "SYSTEM~1") == 0) {
      continue;
    }

    folders[foldersLength] = entry.name();
    foldersLength++;
    entry.close();
  }
}

void processAllAnimations() {
  updateLoadingScreen();
  long loadScreenUpdateMillis = millis();
  //Serial.println("folders length: " + String(foldersLength));
  for (int i = 0; i < foldersLength; i++) {
    if (millis() - loadScreenUpdateMillis > 750) {
      updateLoadingScreen();
      loadScreenUpdateMillis = millis();
    }
   // Serial.println("Openning folder: " + String(folders[i]) + "/");
    String folderFilename = "/" + folders[i] + "/";
    File animationFolder = SD.open(folderFilename.c_str());
    while(true) {
      File frame = animationFolder.openNextFile();
      if (!frame) {
        break;
      }

      String fullFilePath = folders[i] + "/" + frame.name();
      parseBMP(fullFilePath.c_str());
    //  Serial.print("Opening frame: ");
     // Serial.println(fullFilePath);

      frame.close();
    }
  }
}

void processTouchInput() {
  if (myTouch.dataAvailable()) {
    myTouch.read();
    touchX = myTouch.getX();
    touchY = myTouch.getY();
    lastTouchMs = millis();
    Serial.println(
      "Touch registered at x=" + String(touchX) + " y=" + String(touchY));
  }
}

void loop() {
  //processTouchInput();
  for (int i = 0; i < 40; i++) {
    String filename = "frame" + String(i*10) + ".bmp";
    rawDraw(filename.c_str());
  }
}

// Draws a pre-processed RAW file that represents a 240x320 pixel image to the screen.
void rawDraw(const char *filename) {
  // Open requested file on SD card
  String rawFilename = String(filename);
  rawFilename =
      "raw/" + rawFilename.substring(0, rawFilename.length() - 4) + ".raw";
      Serial.println(rawFilename);

  // Create matching raw file on SD card
  if (!SD.exists(rawFilename.c_str())) {
    Serial.println(F("RAW file doesn't exist"));
    //parseBMP(filename);
  }

  // Open requested file on SD card
  File rawFile;
  if (!(rawFile = SD.open(rawFilename.c_str()))) {
    Serial.println(F("BMP file not found"));
    return;
  }

  // Buffer to read in data from the raw file.
  uint16_t rowColors[240];
  for (int row = 0; row < 320; row++) { // For each scanline...
    rawFile.read(rowColors, sizeof(rowColors));
    tft.writeRect(0, row, 240, 1, rowColors);
  }

  rawFile.close();
}

void parseBMP(const char *bmpFilename) {
  File     bmpFile;
  int      bmpWidth, bmpHeight;   // W+H in pixels
  uint8_t  bmpDepth;              // Bit depth (currently must be 24)
  uint32_t bmpImageoffset;        // Start of image data in file
  uint32_t rowSize;               // Not always = bmpWidth; may have padding
  uint8_t  sdbuffer[3*BUFFPIXEL]; // pixel buffer (R+G+B per pixel)
  uint16_t buffidx = sizeof(sdbuffer); // Current position in sdbuffer
  boolean  flip    = true;        // BMP is stored bottom-to-top
  int      w, h;
  int x = 240, y = 320;
  uint8_t  r, g, b;
  uint32_t pos = 0;

  // Open requested file on SD card
  if (!(bmpFile = SD.open(bmpFilename))) {
    Serial.println(String("BMP file not found: ") + bmpFilename);
    return;
  }

  // Create parent raw directory if needed.
  if (!SD.exists("raw")) {
    SD.mkdir("raw");
  }

  // Create subfolder within raw directory if needed.
  String rawSubFolder = "raw/" + String(bmpFilename).substring(0, String(bmpFilename).indexOf('/'));
  if (!SD.exists(rawSubFolder.c_str())) {
    Serial.println("not made, so make it");
    SD.mkdir(rawSubFolder.c_str());
  }

  // Open requested file on SD card
  String newRawFilename = String(bmpFilename);
  newRawFilename =
      "raw/" + newRawFilename.substring(0, newRawFilename.length() - 4) + ".raw";

  // Create matching raw file on SD card
  if (SD.exists(newRawFilename.c_str())) {
    Serial.println(F("RAW file already exists"));
    return;
  }

  File newRawFile = SD.open(newRawFilename.c_str(), FILE_WRITE);

  if (!newRawFile) {
    Serial.println(F("Error creating new RAW file."));
    return;
  }

  // Parse BMP header
  if(read16(bmpFile) != 0x4D42) { // BMP signature
    Serial.println(F("BMP format not recognized."));
    return;
  }

  Serial.print(F("File size: ")); Serial.println(read32(bmpFile));
  (void)read32(bmpFile); // Read & ignore creator bytes
  bmpImageoffset = read32(bmpFile); // Start of image data
  // Read DIB header
  Serial.print(F("Header size: "));
  Serial.println(read32(bmpFile));
  bmpWidth  = read32(bmpFile);
  bmpHeight = read32(bmpFile);
  uint8_t byteArray[bmpWidth*2];  // hold colors for one row at a time...

  if(read16(bmpFile) != 1) {
    Serial.println(F("Error: planes -- must be '1'."));
    return;
  }

  bmpDepth = read16(bmpFile); // bits per pixel
  if(bmpDepth != 24) {
    Serial.println(F("Error: bitmap depth must be 24 bit. Depth is " + bmpDepth));
    return;
  }

  if(read32(bmpFile) != 0) {
    Serial.println(F("Error: bitmap depth must be uncompressed"));
    return;
  }

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
  if((w-1) >= tft.width())  w = tft.width();
  if((h-1) >= tft.height()) h = tft.height();

  // For each scanline...
  for (int row=0; row<h; row++) {

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

    for (int col=0; col<w; col++) { // For each pixel...
      // Time to read more pixel data?
      if (buffidx >= sizeof(sdbuffer)) { // Indeed
        bmpFile.read(sdbuffer, sizeof(sdbuffer));
        buffidx = 0; // Set index to beginning
      }

      // Convert pixel from BMP to TFT format, push to display and save result to SD.
      b = sdbuffer[buffidx++];
      g = sdbuffer[buffidx++];
      r = sdbuffer[buffidx++];
      uint16_t processedColor = tft.color565(r,g,b);
      // Attempt to save same data to another array of uint_8, to be written to SD card.
      byteArray[col*2] = processedColor & 0xFF;
      byteArray[col*2 + 1] = processedColor >> 8;
    } // end pixel

    // This uint16_t array is sent to the display, and correctly displays the image.
    // The same data being saved to the SD card, with intention of being exact
    // payload being sent to the display.
    // byteArray is an array of uint8_t to prevent loss of converting uint_16t (16 bits) to char (8 bits).
    newRawFile.write((char*) &byteArray, bmpWidth*2);
  } // end scanline

  newRawFile.close();
  bmpFile.close();
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