#pragma once

typedef union {
     uint8_t fix_rate;
     uint8_t b5;
     uint8_t b4;

     struct {
         uint8_t b3;
         uint8_t b2;
     } b1;

     struct {
         uint32_t a1;
         uint8_t  a2;
         uint8_t  a3;
         uint8_t  a4;
         uint8_t  a5;
         struct {
             uint8_t a6;
             uint8_t a7;
         } a8[4];
         uint8_t a9;
         uint8_t a10;
         uint8_t a11;
         uint8_t a12;
     } a13;

} wifi_internal_rate_t;

#define RATE_MCS4_SP 28 // N 43.3Mb MCS4 SP

extern "C" {
  esp_err_t esp_wifi_internal_set_rate(int a, int b, int c, wifi_internal_rate_t *d);
}
