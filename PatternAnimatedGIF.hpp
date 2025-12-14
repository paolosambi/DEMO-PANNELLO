/*
 * Pattern for playing animated GIFs from LittleFS
 * Integrates AnimatedGIF library with Aurora pattern system
 */

#ifndef PatternAnimatedGIF_H
#define PatternAnimatedGIF_H

#include "FS.h"
#include <SPIFFS.h>
#include <AnimatedGIF.h>

#define GIF_FILESYSTEM SPIFFS

// Forward declaration - matrix is defined in main sketch
extern MatrixPanel_I2S_DMA *matrix;

// GIF decoder instance
static AnimatedGIF gif;
static File gifFile;
static bool gifOpen = false;
static unsigned long gifStartTime = 0;
static const unsigned long GIF_PLAY_TIME = 8000; // Play each GIF for 8 seconds

// Offset per centrare la GIF sul pannello
static int gifOffsetX = 0;
static int gifOffsetY = 0;

// List of GIF files
static std::vector<String> gifFiles;
static int currentGifIndex = 0;
static bool gifFilesScanned = false;

// GIF callback functions
static void * GIFOpenFile(const char *fname, int32_t *pSize)
{
  gifFile = GIF_FILESYSTEM.open(fname);
  if (gifFile)
  {
    *pSize = gifFile.size();
    return (void *)&gifFile;
  }
  return NULL;
}

static void GIFCloseFile(void *pHandle)
{
  File *f = static_cast<File *>(pHandle);
  if (f != NULL)
     f->close();
}

static int32_t GIFReadFile(GIFFILE *pFile, uint8_t *pBuf, int32_t iLen)
{
    int32_t iBytesRead;
    iBytesRead = iLen;
    File *f = static_cast<File *>(pFile->fHandle);
    if ((pFile->iSize - pFile->iPos) < iLen)
       iBytesRead = pFile->iSize - pFile->iPos - 1;
    if (iBytesRead <= 0)
       return 0;
    iBytesRead = (int32_t)f->read(pBuf, iBytesRead);
    pFile->iPos = f->position();
    return iBytesRead;
}

static int32_t GIFSeekFile(GIFFILE *pFile, int32_t iPosition)
{
  File *f = static_cast<File *>(pFile->fHandle);
  f->seek(iPosition);
  pFile->iPos = (int32_t)f->position();
  return pFile->iPos;
}

// Draw callback for GIF frames
static void GIFDraw(GIFDRAW *pDraw)
{
    uint8_t *s;
    uint16_t *d, *usPalette, usTemp[320];
    int x, y, iWidth;

    iWidth = pDraw->iWidth;
    if (iWidth + gifOffsetX > matrix->width())
        iWidth = matrix->width() - gifOffsetX;

    usPalette = pDraw->pPalette;
    y = pDraw->iY + pDraw->y + gifOffsetY;

    // Salta se fuori schermo
    if (y < 0 || y >= matrix->height()) return;

    s = pDraw->pPixels;
    if (pDraw->ucDisposalMethod == 2)
    {
      for (x=0; x<iWidth; x++)
      {
        if (s[x] == pDraw->ucTransparent)
           s[x] = pDraw->ucBackground;
      }
      pDraw->ucHasTransparency = 0;
    }

    if (pDraw->ucHasTransparency)
    {
      uint8_t *pEnd, c, ucTransparent = pDraw->ucTransparent;
      int x, iCount;
      pEnd = s + pDraw->iWidth;
      x = 0;
      iCount = 0;
      while(x < pDraw->iWidth)
      {
        c = ucTransparent-1;
        d = usTemp;
        while (c != ucTransparent && s < pEnd)
        {
          c = *s++;
          if (c == ucTransparent)
          {
            s--;
          }
          else
          {
             *d++ = usPalette[c];
             iCount++;
          }
        }
        if (iCount)
        {
          for(int xOffset = 0; xOffset < iCount; xOffset++ ){
            int px = x + xOffset + gifOffsetX;
            if (px >= 0 && px < matrix->width())
              matrix->drawPixel(px, y, usTemp[xOffset]);
          }
          x += iCount;
          iCount = 0;
        }
        c = ucTransparent;
        while (c == ucTransparent && s < pEnd)
        {
          c = *s++;
          if (c == ucTransparent)
             iCount++;
          else
             s--;
        }
        if (iCount)
        {
          x += iCount;
          iCount = 0;
        }
      }
    }
    else
    {
      s = pDraw->pPixels;
      for (x=0; x<pDraw->iWidth; x++)
      {
        int px = x + gifOffsetX;
        if (px >= 0 && px < matrix->width())
          matrix->drawPixel(px, y, usPalette[*s++]);
        else
          s++; // Salta pixel fuori schermo
      }
    }
}

// Scan directory for GIF files
static void scanGifFiles(const char* dirname) {
    gifFiles.clear();
    File root = GIF_FILESYSTEM.open(dirname);
    if (!root || !root.isDirectory()) {
        Serial.print("Failed to open directory: ");
        Serial.println(dirname);
        return;
    }

    Serial.print("Scanning directory: ");
    Serial.println(dirname);

    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            String filename = file.name();
            Serial.print("Found file: ");
            Serial.println(filename);

            if (filename.endsWith(".gif") || filename.endsWith(".GIF")) {
                // Costruisci il path completo
                String fullPath;
                if (filename.startsWith("/")) {
                    fullPath = filename; // Già path completo
                } else {
                    // Aggiungi dirname
                    fullPath = String(dirname);
                    if (!fullPath.endsWith("/")) fullPath += "/";
                    fullPath += filename;
                }
                gifFiles.push_back(fullPath);
                Serial.print("Added GIF: ");
                Serial.println(fullPath);
            }
        }
        file = root.openNextFile();
    }
    root.close();

    Serial.print("Total GIFs found: ");
    Serial.println(gifFiles.size());
}

class PatternAnimatedGIF : public Drawable {
private:
    int frameDelay = 0;

public:
    PatternAnimatedGIF() {
        name = (char *)"Animated GIF";
    }

    void start() override {
        // Scan for GIF files if not done yet
        if (!gifFilesScanned) {
            // Prova prima /gifs, poi root /
            scanGifFiles("/gifs");
            if (gifFiles.size() == 0) {
                scanGifFiles("/");
            }
            gifFilesScanned = true;
            gif.begin(LITTLE_ENDIAN_PIXELS);
        }

        // Open first/next GIF
        openNextGif();
    }

    void stop() override {
        if (gifOpen) {
            gif.close();
            gifOpen = false;
        }
    }

    void openNextGif() {
        if (gifFiles.size() == 0) {
            Serial.println("No GIF files found!");
            return;
        }

        if (gifOpen) {
            gif.close();
            gifOpen = false;
        }

        // Get current GIF path
        String gifPath = gifFiles[currentGifIndex];
        Serial.print("Opening GIF: ");
        Serial.println(gifPath);

        char pathBuf[256];
        gifPath.toCharArray(pathBuf, sizeof(pathBuf));

        if (gif.open(pathBuf, GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw)) {
            gifOpen = true;
            gifStartTime = millis();

            // Calcola offset per centrare la GIF sul pannello
            int gifW = gif.getCanvasWidth();
            int gifH = gif.getCanvasHeight();
            gifOffsetX = (matrix->width() - gifW) / 2;
            gifOffsetY = (matrix->height() - gifH) / 2;

            Serial.printf("GIF opened: %d x %d, centered at offset (%d, %d)\n", gifW, gifH, gifOffsetX, gifOffsetY);

            // Pulisci schermo prima di mostrare nuova GIF
            matrix->fillScreen(0);
        } else {
            Serial.println("Failed to open GIF");
        }

        // Move to next GIF for next time
        currentGifIndex = (currentGifIndex + 1) % gifFiles.size();
    }

    unsigned int drawFrame() override {
        if (gifFiles.size() == 0) {
            // No GIFs - show message
            matrix->fillScreen(0);
            return 1000;
        }

        if (!gifOpen) {
            openNextGif();
            if (!gifOpen) return 100;
        }

        // Check if we should switch to next GIF
        if (millis() - gifStartTime > GIF_PLAY_TIME) {
            openNextGif();
            if (!gifOpen) return 100;
        }

        // Play one frame
        int frameDelay = 0;
        if (!gif.playFrame(false, &frameDelay)) {
            // GIF ended, restart it
            gif.reset();
        }

        // Return delay for next frame (minimum 10ms)
        return (frameDelay > 10) ? frameDelay : 10;
    }
};

#endif
