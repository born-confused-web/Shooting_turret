#include <WiFi.h>
#include <WiFiUdp.h>
#include "OV7670.h"
#include "MiniJPEG.h"

const int SIOD  = 21;
const int SIOC  = 22;
const int VSYNC = 34;
const int HREF  = 35;
const int XCLK  = 32;
const int PCLK  = 33;
const int D0    = 27;
const int D1    = 17;
const int D2    = 16;
const int D3    = 15;
const int D4    = 14;
const int D5    = 13;
const int D6    = 12;
const int D7    = 4;

#define SSID      "Galaxy A16 5G 8399"
#define PASSWORD  "lkjhgfdsa"
#define PC_IP     "10.25.118.174"   // <-- replace with your PC IP
#define UDP_PORT  5005
#define FRAME_W   160
#define FRAME_H   120
#define JPEG_QUALITY 50           // 1-100, higher = better quality, larger file
#define CHUNK     4096            // max UDP payload bytes

OV7670 *camera;
WiFiUDP udp;

// JPEG output buffer — 15KB is plenty for 160x120 at quality 50
static uint8_t jpegBuf[60000] __attribute__((aligned(4)));

uint8_t frameNum = 0;

void sendJPEG(int jpegLen) {
  int offset = 0;
  while (offset < jpegLen) {
    int chunkSize = min(CHUNK, jpegLen - offset);

    // 6-byte header: [frameNum, offset(3 bytes), totalLen(2 bytes)]
    uint8_t header[6];
    header[0] = frameNum;
    header[1] = (offset >> 16) & 0xFF;
    header[2] = (offset >>  8) & 0xFF;
    header[3] = (offset      ) & 0xFF;
    header[4] = (jpegLen >> 8) & 0xFF;
    header[5] = (jpegLen     ) & 0xFF;

    udp.beginPacket(PC_IP, UDP_PORT);
    udp.write(header, 6);
    udp.write(jpegBuf + offset, chunkSize);
    udp.endPacket();

    offset += chunkSize;
  }
  frameNum++;
}

void setup() {
  Serial.begin(115200);

  WiFi.begin(SSID, PASSWORD);
  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected: " + WiFi.localIP().toString());

  camera = new OV7670(OV7670::Mode::QQVGA_RGB565,
                      SIOD, SIOC, VSYNC, HREF, XCLK, PCLK,
                      D0, D1, D2, D3, D4, D5, D6, D7);

  for (int i = 0; i < 5; i++) {
    camera->oneFrame();
    delay(100);
  }

  udp.begin(UDP_PORT);
  Serial.printf("Streaming JPEG to %s:%d\n", PC_IP, UDP_PORT);
}

static uint32_t lastPrint = 0;
static int frameCount = 0;

void loop() {
  camera->oneFrame();

  int jpegLen = MiniJPEG::encode(camera->frame, jpegBuf,
                                  FRAME_W, FRAME_H, JPEG_QUALITY,
                                  sizeof(jpegBuf));
  if (jpegLen <= 0) {
    Serial.println("JPEG encode overflow - skipping frame");
    return;  // skip this frame
  }

  if (jpegLen > 0) {
    sendJPEG(jpegLen);
    frameCount++;
  }

  uint32_t now = millis();
  if (now - lastPrint >= 1000) {
    Serial.printf("FPS: %d | JPEG: %d bytes | Heap: %d\n",
                  frameCount, jpegLen, ESP.getFreeHeap());
    frameCount = 0;
    lastPrint  = now;
  }
}