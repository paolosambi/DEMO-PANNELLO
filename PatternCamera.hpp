#ifndef PatternCamera_H
#define PatternCamera_H

#include <WiFi.h>
#include <TJpg_Decoder.h>

// Variabili globali per il decoder
static bool camDecoding = false;

// Callback per TJpg_Decoder - scrive direttamente nel buffer effects
bool camJpgOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
    if (!camDecoding) return false;

    // Scala l'immagine al pannello
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            int srcX = x + i;
            int srcY = y + j;
            // Scala da dimensione camera a VPANEL
            int dstX = srcX * VPANEL_W / 320;
            int dstY = srcY * VPANEL_H / 240;
            if (dstX < VPANEL_W && dstY < VPANEL_H && dstX >= 0 && dstY >= 0) {
                uint16_t color = bitmap[j * w + i];
                uint8_t r = ((color >> 11) & 0x1F) << 3;
                uint8_t g = ((color >> 5) & 0x3F) << 2;
                uint8_t b = (color & 0x1F) << 3;
                effects.leds[dstY * VPANEL_W + dstX] = CRGB(r, g, b);
            }
        }
    }
    return true;
}

class PatternCamera : public Drawable {
private:
    unsigned long lastFrame = 0;
    int frameInterval = 150;  // ms tra frame (più veloce con PSRAM)
    uint8_t* jpegBuf = nullptr;
    size_t jpegBufSize = 0;
    bool initialized = false;
    WiFiClient client;
    int consecutiveErrors = 0;
    bool hasValidFrame = false;

public:
    PatternCamera() {
        name = (char *)"Camera";
    }

    void start() {
        if (!initialized) {
            // Con PSRAM usa buffer più grande (150KB), senza PSRAM usa 60KB
            if (psramFound()) {
                jpegBufSize = 150000;
                jpegBuf = (uint8_t*)ps_malloc(jpegBufSize);
                Serial.print("Camera: PSRAM found, buffer ");
            } else {
                jpegBufSize = 60000;
                jpegBuf = (uint8_t*)malloc(jpegBufSize);
                Serial.print("Camera: No PSRAM, buffer ");
            }
            Serial.print(jpegBufSize);
            Serial.println(" bytes");

            if (jpegBuf) {
                TJpgDec.setJpgScale(1);
                TJpgDec.setCallback(camJpgOutput);
                initialized = true;
                Serial.println("Camera pattern initialized");
            } else {
                Serial.println("Camera: Failed to allocate buffer!");
            }
        }
        lastFrame = 0;
        consecutiveErrors = 0;
    }

    void stop() {
        if (client.connected()) {
            client.stop();
        }
    }

    bool captureFrame() {
        if (!jpegBuf || !initialized) {
            Serial.println("Camera not initialized!");
            return false;
        }

        // Connetti alla camera
        if (!client.connect("192.168.1.100", 80)) {
            Serial.println("Camera: Connection failed to 192.168.1.100:80");
            return false;
        }

        // Invia richiesta HTTP
        client.println("GET /capture HTTP/1.1");
        client.println("Host: 192.168.1.100");
        client.println("Connection: close");
        client.println();

        // Aspetta risposta
        unsigned long timeout = millis() + 5000;
        while (client.available() == 0) {
            if (millis() > timeout) {
                Serial.println("Camera: Response timeout");
                client.stop();
                return false;
            }
            delay(10);
        }

        // Leggi header HTTP e trova Content-Length
        size_t contentLength = 0;
        while (client.available()) {
            String line = client.readStringUntil('\n');
            line.trim();

            if (line.startsWith("Content-Length:")) {
                contentLength = line.substring(15).toInt();
            }

            if (line.length() == 0) {
                // Fine header
                break;
            }
        }

        if (contentLength == 0 || contentLength > jpegBufSize) {
            Serial.print("Camera: Bad content length: ");
            Serial.println(contentLength);
            client.stop();
            return false;
        }

        // Leggi dati JPEG
        size_t bytesRead = 0;
        timeout = millis() + 5000;

        while (bytesRead < contentLength && millis() < timeout) {
            if (client.available()) {
                size_t toRead = min((size_t)client.available(), contentLength - bytesRead);
                size_t r = client.readBytes(jpegBuf + bytesRead, toRead);
                bytesRead += r;
            }
            yield();
        }

        client.stop();

        if (bytesRead < contentLength) {
            Serial.print("Camera: Incomplete ");
            Serial.print(bytesRead);
            Serial.print("/");
            Serial.println(contentLength);
            return false;
        }

        // Decodifica JPEG
        camDecoding = true;

        for (int i = 0; i < VPANEL_W * VPANEL_H; i++) {
            effects.leds[i] = CRGB::Black;
        }

        JRESULT res = TJpgDec.drawJpg(0, 0, jpegBuf, bytesRead);
        camDecoding = false;

        if (res != JDR_OK) {
            // Non stampa errore per ogni frame - troppo spam
            return false;
        }

        return true;
    }

    void showError() {
        static int offset = 0;
        offset = (offset + 1) % 8;
        for (int y = 0; y < VPANEL_H; y++) {
            for (int x = 0; x < VPANEL_W; x++) {
                if ((x + y + offset) % 8 < 4) {
                    effects.leds[y * VPANEL_W + x] = CRGB(50, 0, 0);
                } else {
                    effects.leds[y * VPANEL_W + x] = CRGB::Black;
                }
            }
        }
    }

    unsigned int drawFrame() {
        unsigned long now = millis();

        if (now - lastFrame >= (unsigned long)frameInterval) {
            lastFrame = now;

            if (captureFrame()) {
                consecutiveErrors = 0;
                hasValidFrame = true;
            } else {
                consecutiveErrors++;
                // Mostra errore solo dopo 5 fallimenti consecutivi
                if (consecutiveErrors > 5 || !hasValidFrame) {
                    showError();
                }
                // Altrimenti mantiene l'ultimo frame valido
            }
        }

        effects.ShowFrame();
        return 0;
    }
};

#endif
