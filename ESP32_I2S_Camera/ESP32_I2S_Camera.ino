#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiServer.h>
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

#define SSID          "Galaxy A16 5G 8399"
#define PASSWORD      "lkjhgfdsa"
#define JPEG_QUALITY  35
#define FRAME_W       160
#define FRAME_H       120

// ---- Dual frame buffer (ping-pong) ----
// Core 1 encodes into one buffer while Core 0 sends the other
#define JPEG_BUF_SIZE 30000

static uint8_t jpegBuf[2][JPEG_BUF_SIZE] __attribute__((aligned(4)));
static volatile int  jpegLen[2]   = {0, 0};
static volatile int  writeIdx     = 0;  // core 1 writes here
static volatile int  readIdx      = 1;  // core 0 reads here
static volatile bool frameReady   = false;
static volatile bool clientActive = false;

// Mutex to safely swap buffers
static SemaphoreHandle_t swapMutex;

OV7670 *camera;
WiFiServer httpServer(80);
WiFiServer tcpServer(8080);

// ============================================================
// CORE 1 TASK — Camera capture + JPEG encode
// Runs on core 1 (default Arduino core)
// ============================================================
void encodeTask(void *param) {
  while (true) {
    // Capture frame
    camera->oneFrame();

    // Encode into writeIdx buffer
    int len = MiniJPEG::encode(
      camera->frame, jpegBuf[writeIdx],
      FRAME_W, FRAME_H, JPEG_QUALITY, JPEG_BUF_SIZE
    );

    if (len > 0) {
      // Swap buffers atomically
      xSemaphoreTake(swapMutex, portMAX_DELAY);
      jpegLen[writeIdx] = len;
      // Swap read/write indices
      int tmp  = readIdx;
      readIdx  = writeIdx;
      writeIdx = tmp;
      frameReady = true;
      xSemaphoreGive(swapMutex);
    }

    // Small yield so WiFi stack gets time
    vTaskDelay(1);
  }
}

// ============================================================
// CORE 0 TASK — WiFi send
// Runs on core 0
// ============================================================

// ---- Raw TCP sender for Python ----
void handleTCP(WiFiClient &client) {
  Serial.println("Python client connected on port 8080");
  client.setNoDelay(true);   // Fix 1: disable Nagle
  client.setTimeout(500);    // Fix 3: drop slow clients

  clientActive = true;

  uint32_t lastPrint  = millis();
  int      frameCount = 0;
  int      lastLen    = 0;

  while (client.connected()) {
    // Wait for a new frame
    if (!frameReady) {
      vTaskDelay(1);
      continue;
    }

    // Grab the ready buffer
    xSemaphoreTake(swapMutex, portMAX_DELAY);
    int  idx = readIdx;
    int  len = jpegLen[idx];
    frameReady = false;
    xSemaphoreGive(swapMutex);

    if (len <= 0) continue;

    // Send 4-byte big-endian length header + JPEG
    uint8_t hdr[4] = {
      (uint8_t)(len >> 24),
      (uint8_t)(len >> 16),
      (uint8_t)(len >>  8),
      (uint8_t)(len      )
    };
    client.write(hdr, 4);
    client.write(jpegBuf[idx], len);

    frameCount++;
    lastLen = len;
    uint32_t now = millis();
    if (now - lastPrint >= 1000) {
      Serial.printf("TCP FPS: %d | JPEG: %d bytes | Heap: %d\n",
                    frameCount, lastLen, ESP.getFreeHeap());
      frameCount = 0;
      lastPrint  = now;
    }
  }

  clientActive = false;
  Serial.println("Python client disconnected");
}

// ---- HTTP MJPEG stream for browser ----
void handleHTTP(WiFiClient &client) {
  // Read request
  String req = "";
  unsigned long t = millis();
  while (client.connected() && millis() - t < 2000) {
    if (client.available()) {
      String line = client.readStringUntil('\n');
      line.trim();
      if (req == "") req = line;
      if (line.length() == 0) break;
    }
  }
  Serial.println("HTTP: " + req);

  if (req.indexOf("/stream") >= 0) {
    client.setNoDelay(true);
    client.setTimeout(500);
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: multipart/x-mixed-replace; boundary=frame");
    client.println("Connection: keep-alive");
    client.println("Access-Control-Allow-Origin: *");
    client.println();

    clientActive = true;
    uint32_t lastPrint  = millis();
    int      frameCount = 0;

    while (client.connected()) {
      if (!frameReady) { vTaskDelay(1); continue; }

      xSemaphoreTake(swapMutex, portMAX_DELAY);
      int idx = readIdx;
      int len = jpegLen[idx];
      frameReady = false;
      xSemaphoreGive(swapMutex);

      if (len <= 0) continue;

      client.print("--frame\r\n");
      client.print("Content-Type: image/jpeg\r\n");
      client.printf("Content-Length: %d\r\n\r\n", len);
      client.write(jpegBuf[idx], len);
      client.print("\r\n");

      frameCount++;
      uint32_t now = millis();
      if (now - lastPrint >= 1000) {
        Serial.printf("HTTP FPS: %d | JPEG: %d bytes | Heap: %d\n",
                      frameCount, len, ESP.getFreeHeap());
        frameCount = 0;
        lastPrint  = now;
      }
    }
    clientActive = false;
    Serial.println("HTTP client disconnected");

  } else {
    // Root page
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html");
    client.println("Connection: close");
    client.println();
    client.print(
      "<!DOCTYPE html><html><head><title>OV7670</title>"
      "<style>"
      "body{margin:0;background:#111;display:flex;flex-direction:column;"
      "align-items:center;justify-content:center;height:100vh;"
      "color:#fff;font-family:sans-serif}"
      "img{image-rendering:pixelated;width:640px;height:480px;"
      "border:2px solid #333}"
      "p{margin:8px;font-size:13px;color:#aaa}"
      "</style></head><body>"
      "<p>OV7670 Live Feed</p>"
      "<img src='/stream'/>"
      "<p>160x120 &rarr; 640x480 | JPEG quality 35</p>"
      "</body></html>"
    );
    client.stop();
  }
}

// Core 0 task — handles all WiFi clients
void wifiTask(void *param) {
  while (true) {
    WiFiClient httpClient = httpServer.available();
    if (httpClient) {
      handleHTTP(httpClient);
      httpClient.stop();
    }

    WiFiClient tcpClient = tcpServer.available();
    if (tcpClient) {
      handleTCP(tcpClient);
      tcpClient.stop();
    }

    vTaskDelay(1);
  }
}

// ============================================================
// Setup — runs on core 1
// ============================================================
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

  swapMutex = xSemaphoreCreateMutex();

  httpServer.begin();
  tcpServer.begin();

  Serial.println("Browser:  http://" + WiFi.localIP().toString());
  Serial.println("Stream:   http://" + WiFi.localIP().toString() + "/stream");
  Serial.println("Python:   port 8080");

  // Start WiFi send task on core 0, priority 1
  xTaskCreatePinnedToCore(
    wifiTask,    // function
    "wifiTask",  // name
    8192,        // stack size
    NULL,        // param
    1,           // priority
    NULL,        // handle
    0            // core 0
  );

  // Start encode task on core 1, priority 2 (higher than wifi)
  xTaskCreatePinnedToCore(
    encodeTask,
    "encodeTask",
    8192,
    NULL,
    2,
    NULL,
    1            // core 1
  );
}

// loop() is unused — both tasks run independently
void loop() {
  vTaskDelay(1000);
}