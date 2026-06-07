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
#define JPEG_QUALITY  50
#define FRAME_W       160
#define FRAME_H       120

OV7670 *camera;

// Two servers:
// Port 80  — browser MJPEG viewer
// Port 8080 — raw TCP stream for Python
WiFiServer httpServer(80);
WiFiServer tcpServer(8080);

static uint8_t jpegBuf[70000] __attribute__((aligned(4)));

static uint32_t lastPrint  = 0;
static int      frameCount = 0;
static int      lastJpegLen = 0;

// ---- Encode one frame, return length or -1 on overflow ----
int encodeFrame() {
  int len = MiniJPEG::encode(camera->frame, jpegBuf,
                              FRAME_W, FRAME_H, JPEG_QUALITY,
                              sizeof(jpegBuf));
  return len;
}

// ---- Handle browser MJPEG stream ----
void handleHTTP(WiFiClient &client) {
  // Read and drain HTTP request
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
    // MJPEG stream
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: multipart/x-mixed-replace; boundary=frame");
    client.println("Connection: keep-alive");
    client.println("Access-Control-Allow-Origin: *");
    client.println();

    while (client.connected()) {
      camera->oneFrame();
      int len = encodeFrame();
      if (len <= 0) continue;

      client.print("--frame\r\n");
      client.print("Content-Type: image/jpeg\r\n");
      client.printf("Content-Length: %d\r\n\r\n", len);
      client.write(jpegBuf, len);
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
    Serial.println("HTTP client disconnected");

  } else {
    // Root page — serves the viewer
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html");
    client.println("Connection: close");
    client.println();
    client.print(
      "<!DOCTYPE html><html><head><title>OV7670</title>"
      "<style>"
      "body{margin:0;background:#111;display:flex;flex-direction:column;"
      "align-items:center;justify-content:center;height:100vh;color:#fff;"
      "font-family:sans-serif}"
      "img{image-rendering:pixelated;width:640px;height:480px;"
      "border:2px solid #333}"
      "p{margin:8px;font-size:14px;color:#aaa}"
      "</style></head><body>"
      "<p>OV7670 Live Feed</p>"
      "<img src='/stream'/>"
      "<p>160x120 &rarr; 640x480 &nbsp;|&nbsp; JPEG Quality 50</p>"
      "</body></html>"
    );
    client.stop();
  }
}

// ---- Handle raw TCP Python client ----
// Protocol: [4-byte big-endian length][JPEG bytes] repeated
void handleTCP(WiFiClient &client) {
  Serial.println("Python client connected");

  while (client.connected()) {
    camera->oneFrame();
    int len = encodeFrame();
    if (len <= 0) continue;

    // Send 4-byte length header (big-endian)
    uint8_t hdr[4];
    hdr[0] = (len >> 24) & 0xFF;
    hdr[1] = (len >> 16) & 0xFF;
    hdr[2] = (len >>  8) & 0xFF;
    hdr[3] = (len      ) & 0xFF;
    client.write(hdr, 4);
    client.write(jpegBuf, len);

    frameCount++;
    uint32_t now = millis();
    if (now - lastPrint >= 1000) {
      Serial.printf("TCP FPS: %d | JPEG: %d bytes | Heap: %d\n",
                    frameCount, len, ESP.getFreeHeap());
      frameCount = 0;
      lastPrint  = now;
    }
  }
  Serial.println("Python client disconnected");
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

  httpServer.begin();
  tcpServer.begin();

  Serial.println("HTTP stream: http://" + WiFi.localIP().toString() + "/stream");
  Serial.println("Browser:     http://" + WiFi.localIP().toString());
  Serial.println("Python TCP:  port 8080");
}

void loop() {
  // Check for browser connection (port 80)
  WiFiClient httpClient = httpServer.available();
  if (httpClient) {
    handleHTTP(httpClient);
    httpClient.stop();
  }

  // Check for Python connection (port 8080)
  WiFiClient tcpClient = tcpServer.available();
  if (tcpClient) {
    handleTCP(tcpClient);
    tcpClient.stop();
  }
}