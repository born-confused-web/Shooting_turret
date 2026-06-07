#pragma once
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

// =============================================================
// MiniJPEG - Minimal color JPEG encoder for ESP32 + OV7670
// Encodes RGB565 (byte order: low byte first, high byte second)
// into a standard JPEG file.
// Output is 4:2:0 chroma subsampling, standard Huffman coding.
// =============================================================

class MiniJPEG {
public:
  // Encode a 160x120 RGB565 frame into jpegOut buffer
  // rgb565: pointer to frame (160*120*2 bytes, low byte first)
  // jpegOut: output buffer (must be at least 15000 bytes)
  // width, height: image dimensions (must be multiples of 16)
  // quality: 1-100 (50 is a good default)
  // returns: number of bytes written to jpegOut
  static int encode(const uint8_t *rgb565, uint8_t *jpegOut,
                    int width, int height, int quality) {
    BitWriter bw(jpegOut);
    int q = clampQ(quality);

    // Build quantization tables
    uint8_t qLum[64], qChr[64];
    buildQuantTable(qLum, stdLumQuant, q);
    buildQuantTable(qChr, stdChrQuant, q);

    // Write JPEG header
    writeHeader(bw, width, height, qLum, qChr);

    // Encode MCUs (16x16 blocks for 4:2:0)
    int mcuW = (width  + 15) / 16;
    int mcuH = (height + 15) / 16;

    // DC predictors
    int dcY = 0, dcCb = 0, dcCr = 0;

    for (int my = 0; my < mcuH; my++) {
      for (int mx = 0; mx < mcuW; mx++) {
        // Extract 16x16 block → 4 Y blocks + 1 Cb + 1 Cr
        int8_t Y0[64], Y1[64], Y2[64], Y3[64];
        int8_t Cb[64], Cr[64];
        extractMCU(rgb565, width, height, mx*16, my*16,
                   Y0, Y1, Y2, Y3, Cb, Cr);

        // DCT + quantize + huffman encode each block
        int16_t dct[64];
        fdct(Y0, dct); quantize(dct, qLum); dcY  = encodeBlock(bw, dct, dcY,  lumDCHuff, lumACHuff);
        fdct(Y1, dct); quantize(dct, qLum); dcY  = encodeBlock(bw, dct, dcY,  lumDCHuff, lumACHuff);
        fdct(Y2, dct); quantize(dct, qLum); dcY  = encodeBlock(bw, dct, dcY,  lumDCHuff, lumACHuff);
        fdct(Y3, dct); quantize(dct, qLum); dcY  = encodeBlock(bw, dct, dcY,  lumDCHuff, lumACHuff);
        fdct(Cb, dct); quantize(dct, qChr); dcCb = encodeBlock(bw, dct, dcCb, chrDCHuff, chrACHuff);
        fdct(Cr, dct); quantize(dct, qChr); dcCr = encodeBlock(bw, dct, dcCr, chrDCHuff, chrACHuff);
      }
    }

    bw.flush();
    writeEOI(bw);
    return bw.pos();
  }

private:
  // ---- Bit writer ----
  struct BitWriter {
    uint8_t *buf;
    int      _pos;
    uint32_t acc;
    int      bits;

    BitWriter(uint8_t *b) : buf(b), _pos(0), acc(0), bits(0) {}

    void writeByte(uint8_t c) { buf[_pos++] = c; }

    void writeWord(uint16_t w) {
      writeByte(w >> 8);
      writeByte(w & 0xFF);
    }

    void writeBits(int code, int len) {
      acc  = (acc << len) | (code & ((1 << len) - 1));
      bits += len;
      while (bits >= 8) {
        bits -= 8;
        uint8_t c = (acc >> bits) & 0xFF;
        writeByte(c);
        if (c == 0xFF) writeByte(0x00);  // byte stuffing
      }
    }

    void flush() {
      if (bits > 0) {
        uint8_t c = (acc << (8 - bits)) & 0xFF;
        writeByte(c);
        if (c == 0xFF) writeByte(0x00);
        bits = 0;
        acc  = 0;
      }
    }

    int pos() { return _pos; }
  };

  // ---- Clamp quality ----
  static int clampQ(int q) {
    if (q < 1)   q = 1;
    if (q > 100) q = 100;
    return q < 50 ? (5000 / q) : (200 - q * 2);
  }

  // ---- Build quantization table ----
  static void buildQuantTable(uint8_t *out, const uint8_t *base, int q) {
    for (int i = 0; i < 64; i++) {
      int v = ((int)base[i] * q + 50) / 100;
      if (v <   1) v = 1;
      if (v > 255) v = 255;
      out[i] = (uint8_t)v;
    }
  }

  // ---- Extract one 16x16 MCU → 4 Y + Cb + Cr ----
  static void extractMCU(const uint8_t *rgb565, int w, int h,
                          int bx, int by,
                          int8_t *Y0, int8_t *Y1, int8_t *Y2, int8_t *Y3,
                          int8_t *Cb, int8_t *Cr) {
    // Temporary full 16x16 YCbCr
    int8_t  Ytmp[16][16];
    int16_t Cbtmp[16][16], Crtmp[16][16];

    for (int dy = 0; dy < 16; dy++) {
      for (int dx = 0; dx < 16; dx++) {
        int px = bx + dx;
        int py = by + dy;
        // Clamp to image bounds
        if (px >= w) px = w - 1;
        if (py >= h) py = h - 1;

        // Read RGB565 — low byte first
        int idx = (py * w + px) * 2;
        uint8_t  b0 = rgb565[idx];
        uint8_t  b1 = rgb565[idx + 1];
        uint16_t p  = (uint16_t)b0 | ((uint16_t)b1 << 8);

        int R = ((p >> 11) & 0x1F) << 3;
        int G = ((p >>  5) & 0x3F) << 2;
        int B = ( p        & 0x1F) << 3;

        // RGB → YCbCr (JPEG standard)
        int Y  =  ((  66*R + 129*G +  25*B + 128) >> 8) + 16;
        int Cbt= (( -38*R -  74*G + 112*B + 128) >> 8) + 128;
        int Crt= (( 112*R -  94*G -  18*B + 128) >> 8) + 128;

        Ytmp[dy][dx]  = (int8_t)(Y  - 128);
        Cbtmp[dy][dx] = Cbt;
        Crtmp[dy][dx] = Crt;
      }
    }

    // Fill 4 Y blocks (each 8x8 from quadrant of 16x16)
    for (int y = 0; y < 8; y++)
      for (int x = 0; x < 8; x++) {
        Y0[y*8+x] = Ytmp[y]  [x];    // top-left
        Y1[y*8+x] = Ytmp[y]  [x+8];  // top-right
        Y2[y*8+x] = Ytmp[y+8][x];    // bottom-left
        Y3[y*8+x] = Ytmp[y+8][x+8];  // bottom-right
      }

    // Downsample Cb/Cr 2x2 → 8x8
    for (int y = 0; y < 8; y++)
      for (int x = 0; x < 8; x++) {
        int cb = (Cbtmp[y*2][x*2] + Cbtmp[y*2][x*2+1] +
                  Cbtmp[y*2+1][x*2] + Cbtmp[y*2+1][x*2+1] + 2) / 4;
        int cr = (Crtmp[y*2][x*2] + Crtmp[y*2][x*2+1] +
                  Crtmp[y*2+1][x*2] + Crtmp[y*2+1][x*2+1] + 2) / 4;
        Cb[y*8+x] = (int8_t)(cb - 128);
        Cr[y*8+x] = (int8_t)(cr - 128);
      }
  }

  // ---- Forward DCT (AAN algorithm) ----
  static void fdct(const int8_t *in, int16_t *out) {
    int32_t tmp[64];
    // Row pass
    for (int i = 0; i < 8; i++) {
      const int8_t *s = in + i*8;
      int32_t *d = tmp + i*8;
      int32_t t0=s[0]+s[7], t7=s[0]-s[7];
      int32_t t1=s[1]+s[6], t6=s[1]-s[6];
      int32_t t2=s[2]+s[5], t5=s[2]-s[5];
      int32_t t3=s[3]+s[4], t4=s[3]-s[4];
      int32_t u0=t0+t3, u3=t0-t3;
      int32_t u1=t1+t2, u2=t1-t2;
      d[0]=(u0+u1);
      d[4]=(u0-u1);
      int32_t z1=((u2+u3)*181)>>8;
      d[2]=u3+z1; d[6]=u3-z1;
      int32_t z5=((t4-t6)*98);
      int32_t z2=(z5+t4*139)>>8;
      int32_t z4=(z5+t6*334)>>8;
      int32_t z3=(t5*181)>>8;
      int32_t z11=t7+z3, z13=t7-z3;
      d[5]=z13+z2; d[3]=z13-z2;
      d[1]=z11+z4; d[7]=z11-z4;
    }
    // Column pass
    for (int i = 0; i < 8; i++) {
      int32_t *d = tmp + i;
      int32_t t0=d[0*8]+d[7*8], t7=d[0*8]-d[7*8];
      int32_t t1=d[1*8]+d[6*8], t6=d[1*8]-d[6*8];
      int32_t t2=d[2*8]+d[5*8], t5=d[2*8]-d[5*8];
      int32_t t3=d[3*8]+d[4*8], t4=d[3*8]-d[4*8];
      int32_t u0=t0+t3, u3=t0-t3;
      int32_t u1=t1+t2, u2=t1-t2;
      out[i+0*8]=(int16_t)((u0+u1)>>3);
      out[i+4*8]=(int16_t)((u0-u1)>>3);
      int32_t z1=((u2+u3)*181)>>8;
      out[i+2*8]=(int16_t)((u3+z1)>>3);
      out[i+6*8]=(int16_t)((u3-z1)>>3);
      int32_t z5=((t4-t6)*98);
      int32_t z2=(z5+t4*139)>>8;
      int32_t z4=(z5+t6*334)>>8;
      int32_t z3=(t5*181)>>8;
      int32_t z11=t7+z3, z13=t7-z3;
      out[i+5*8]=(int16_t)((z13+z2)>>3);
      out[i+3*8]=(int16_t)((z13-z2)>>3);
      out[i+1*8]=(int16_t)((z11+z4)>>3);
      out[i+7*8]=(int16_t)((z11-z4)>>3);
    }
  }

  // ---- Quantize in zigzag order ----
  static const uint8_t zigzag[64];
  static void quantize(int16_t *dct, const uint8_t *qtab) {
    int16_t tmp[64];
    for (int i = 0; i < 64; i++) {
      int v = dct[zigzag[i]];
      int q = qtab[i];
      // Divide with rounding toward zero
      tmp[i] = (int16_t)((v >= 0) ? (v + q/2) / q : -((-v + q/2) / q));
    }
    memcpy(dct, tmp, 64 * sizeof(int16_t));
  }

  // ---- Encode one 8x8 block ----
  static int encodeBlock(BitWriter &bw, const int16_t *block, int dcPred,
                          const uint16_t dcHuff[][2],
                          const uint16_t acHuff[][2]) {
    // DC coefficient
    int dc  = block[0] - dcPred;
    int cat = category(dc);
    bw.writeBits(dcHuff[cat][0], dcHuff[cat][1]);
    if (cat > 0) bw.writeBits(encodeVLI(dc, cat), cat);

    // AC coefficients
    int zeros = 0;
    for (int i = 1; i < 64; i++) {
      int v = block[i];
      if (v == 0) {
        zeros++;
        continue;
      }
      while (zeros >= 16) {
        bw.writeBits(acHuff[0xF0][0], acHuff[0xF0][1]);
        zeros -= 16;
      }
      int cat2 = category(v);
      int idx  = (zeros << 4) | cat2;
      bw.writeBits(acHuff[idx][0], acHuff[idx][1]);
      bw.writeBits(encodeVLI(v, cat2), cat2);
      zeros = 0;
    }
    // EOB
    bw.writeBits(acHuff[0x00][0], acHuff[0x00][1]);
    return block[0];
  }

  static int category(int v) {
    if (v < 0) v = -v;
    int c = 0;
    while (v > 0) { c++; v >>= 1; }
    return c;
  }

  static int encodeVLI(int v, int cat) {
    if (v < 0) v = v - 1 + (1 << cat);
    return v & ((1 << cat) - 1);
  }

  // ---- Write JPEG header ----
  static void writeHeader(BitWriter &bw, int w, int h,
                           const uint8_t *qLum, const uint8_t *qChr) {
    // SOI
    bw.writeByte(0xFF); bw.writeByte(0xD8);
    // APP0
    bw.writeByte(0xFF); bw.writeByte(0xE0);
    bw.writeWord(16);
    bw.writeByte('J'); bw.writeByte('F'); bw.writeByte('I');
    bw.writeByte('F'); bw.writeByte(0);
    bw.writeByte(1); bw.writeByte(1);  // version 1.1
    bw.writeByte(0);                    // aspect ratio units
    bw.writeWord(1); bw.writeWord(1);  // Xdensity, Ydensity
    bw.writeByte(0); bw.writeByte(0);  // thumbnail

    // DQT luma
    bw.writeByte(0xFF); bw.writeByte(0xDB);
    bw.writeWord(67);
    bw.writeByte(0x00);
    for (int i = 0; i < 64; i++) bw.writeByte(qLum[i]);
    // DQT chroma
    bw.writeByte(0xFF); bw.writeByte(0xDB);
    bw.writeWord(67);
    bw.writeByte(0x01);
    for (int i = 0; i < 64; i++) bw.writeByte(qChr[i]);

    // SOF0
    bw.writeByte(0xFF); bw.writeByte(0xC0);
    bw.writeWord(17);
    bw.writeByte(8);          // precision
    bw.writeWord(h);
    bw.writeWord(w);
    bw.writeByte(3);          // 3 components
    bw.writeByte(1); bw.writeByte(0x22); bw.writeByte(0); // Y  2x2 Q0
    bw.writeByte(2); bw.writeByte(0x11); bw.writeByte(1); // Cb 1x1 Q1
    bw.writeByte(3); bw.writeByte(0x11); bw.writeByte(1); // Cr 1x1 Q1

    // DHT luma DC
    writeHuffTable(bw, 0x00, lumDCBits, lumDCVals, lumDCSize);
    // DHT luma AC
    writeHuffTable(bw, 0x10, lumACBits, lumACVals, lumACSize);
    // DHT chroma DC
    writeHuffTable(bw, 0x01, chrDCBits, chrDCVals, chrDCSize);
    // DHT chroma AC
    writeHuffTable(bw, 0x11, chrACBits, chrACVals, chrACSize);

    // SOS
    bw.writeByte(0xFF); bw.writeByte(0xDA);
    bw.writeWord(12);
    bw.writeByte(3);
    bw.writeByte(1); bw.writeByte(0x00);
    bw.writeByte(2); bw.writeByte(0x11);
    bw.writeByte(3); bw.writeByte(0x11);
    bw.writeByte(0); bw.writeByte(63); bw.writeByte(0);
  }

  static void writeHuffTable(BitWriter &bw, uint8_t id,
                               const uint8_t *bits, const uint8_t *vals,
                               int nVals) {
    bw.writeByte(0xFF); bw.writeByte(0xC4);
    bw.writeWord(19 + nVals);
    bw.writeByte(id);
    for (int i = 0; i < 16; i++) bw.writeByte(bits[i]);
    for (int i = 0; i < nVals; i++) bw.writeByte(vals[i]);
  }

  static void writeEOI(BitWriter &bw) {
    bw.writeByte(0xFF); bw.writeByte(0xD9);
  }

  // ---- Standard quantization tables ----
  static const uint8_t stdLumQuant[64];
  static const uint8_t stdChrQuant[64];

  // ---- Standard Huffman tables ----
  static const uint8_t lumDCBits[16];
  static const uint8_t lumDCVals[];
  static const int     lumDCSize;
  static const uint8_t lumACBits[16];
  static const uint8_t lumACVals[];
  static const int     lumACSize;
  static const uint8_t chrDCBits[16];
  static const uint8_t chrDCVals[];
  static const int     chrDCSize;
  static const uint8_t chrACBits[16];
  static const uint8_t chrACVals[];
  static const int     chrACSize;

  // Pre-built Huffman code tables [code][length]
  static const uint16_t lumDCHuff[12][2];
  static const uint16_t lumACHuff[256][2];
  static const uint16_t chrDCHuff[12][2];
  static const uint16_t chrACHuff[256][2];
};

// =============================================
// Static data definitions
// =============================================

const uint8_t MiniJPEG::zigzag[64] = {
   0, 1, 8,16, 9, 2, 3,10,
  17,24,32,25,18,11, 4, 5,
  12,19,26,33,40,48,41,34,
  27,20,13, 6, 7,14,21,28,
  35,42,49,56,57,50,43,36,
  29,22,15,23,30,37,44,51,
  58,59,52,45,38,31,39,46,
  53,60,61,54,47,55,62,63
};

const uint8_t MiniJPEG::stdLumQuant[64] = {
  16,11,10,16,24,40,51,61,
  12,12,14,19,26,58,60,55,
  14,13,16,24,40,57,69,56,
  14,17,22,29,51,87,80,62,
  18,22,37,56,68,109,103,77,
  24,35,55,64,81,104,113,92,
  49,64,78,87,103,121,120,101,
  72,92,95,98,112,100,103,99
};

const uint8_t MiniJPEG::stdChrQuant[64] = {
  17,18,24,47,99,99,99,99,
  18,21,26,66,99,99,99,99,
  24,26,56,99,99,99,99,99,
  47,66,99,99,99,99,99,99,
  99,99,99,99,99,99,99,99,
  99,99,99,99,99,99,99,99,
  99,99,99,99,99,99,99,99,
  99,99,99,99,99,99,99,99
};

// Luma DC Huffman
const uint8_t MiniJPEG::lumDCBits[16] = {0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0};
const uint8_t MiniJPEG::lumDCVals[]   = {0,1,2,3,4,5,6,7,8,9,10,11};
const int     MiniJPEG::lumDCSize     = 12;

// Luma AC Huffman
const uint8_t MiniJPEG::lumACBits[16] = {0,2,1,3,3,2,4,3,5,5,4,4,0,0,1,125};
const uint8_t MiniJPEG::lumACVals[]   = {
  0x01,0x02,0x03,0x00,0x04,0x11,0x05,0x12,0x21,0x31,0x41,0x06,0x13,0x51,0x61,0x07,
  0x22,0x71,0x14,0x32,0x81,0x91,0xa1,0x08,0x23,0x42,0xb1,0xc1,0x15,0x52,0xd1,0xf0,
  0x24,0x33,0x62,0x72,0x82,0x09,0x0a,0x16,0x17,0x18,0x19,0x1a,0x25,0x26,0x27,0x28,
  0x29,0x2a,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,
  0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,
  0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x83,0x84,0x85,0x86,0x87,0x88,0x89,
  0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,
  0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,
  0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe1,0xe2,
  0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,
  0xf9,0xfa
};
const int MiniJPEG::lumACSize = 162;

// Chroma DC Huffman
const uint8_t MiniJPEG::chrDCBits[16] = {0,3,1,1,1,1,1,1,1,1,1,0,0,0,0,0};
const uint8_t MiniJPEG::chrDCVals[]   = {0,1,2,3,4,5,6,7,8,9,10,11};
const int     MiniJPEG::chrDCSize     = 12;

// Chroma AC Huffman
const uint8_t MiniJPEG::chrACBits[16] = {0,2,1,2,4,4,3,4,7,5,4,4,0,1,2,119};
const uint8_t MiniJPEG::chrACVals[]   = {
  0x00,0x01,0x02,0x03,0x11,0x04,0x05,0x21,0x31,0x06,0x12,0x41,0x51,0x07,0x61,0x71,
  0x13,0x22,0x32,0x81,0x08,0x14,0x42,0x91,0xa1,0xb1,0xc1,0x09,0x23,0x33,0x52,0xf0,
  0x15,0x62,0x72,0xd1,0x0a,0x16,0x24,0x34,0xe1,0x25,0xf1,0x17,0x18,0x19,0x1a,0x26,
  0x27,0x28,0x29,0x2a,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,
  0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,
  0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x82,0x83,0x84,0x85,0x86,0x87,
  0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,
  0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,
  0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,
  0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,
  0xf9,0xfa
};
const int MiniJPEG::chrACSize = 162;

// Pre-built Huffman lookup tables
// Built from standard JPEG Huffman tables
// Format: [code_value, code_length_in_bits]
const uint16_t MiniJPEG::lumDCHuff[12][2] = {
  {0x0000,2},{0x0002,3},{0x0003,3},{0x0004,3},{0x0005,3},{0x0006,3},
  {0x000E,4},{0x001E,5},{0x003E,6},{0x007E,7},{0x00FE,8},{0x01FE,9}
};

const uint16_t MiniJPEG::chrDCHuff[12][2] = {
  {0x0000,2},{0x0001,2},{0x0002,2},{0x0006,3},{0x000E,4},{0x001E,5},
  {0x003E,6},{0x007E,7},{0x00FE,8},{0x01FE,9},{0x03FE,10},{0x07FE,11}
};

// Luma AC: indexed by (run<<4)|category, 0..255
// Generated from standard JPEG luma AC Huffman table
const uint16_t MiniJPEG::lumACHuff[256][2] = {
  {0x000A,4},{0x0000,2},{0x0001,2},{0x0004,3},{0x000B,4},{0x001A,5},
  {0x0078,7},{0x00F8,8},{0x03F6,10},{0xFF82,16},{0xFF83,16},{0x0000,0},
  {0x0000,0},{0x0000,0},{0x0000,0},{0x0000,0},{0x0000,0},{0x000C,4},
  {0x001B,5},{0x0079,7},{0x01F6,9},{0x07F6,11},{0xFF84,16},{0xFF85,16},
  {0xFF86,16},{0xFF87,16},{0xFF88,16},{0x0000,0},{0x0000,0},{0x0000,0},
  {0x0000,0},{0x0000,0},{0x0000,0},{0x001C,5},{0x00F9,8},{0x03F7,10},
  {0x0FF4,12},{0xFF89,16},{0xFF8A,16},{0xFF8B,16},{0xFF8C,16},{0xFF8D,16},
  {0xFF8E,16},{0x0000,0},{0x0000,0},{0x0000,0},{0x0000,0},{0x0000,0},
  {0x0000,0},{0x003A,6},{0x01F7,9},{0x0FF5,12},{0xFF8F,16},{0xFF90,16},
  {0xFF91,16},{0xFF92,16},{0xFF93,16},{0xFF94,16},{0xFF95,16},{0x0000,0},
  {0x0000,0},{0x0000,0},{0x0000,0},{0x0000,0},{0x0000,0},{0x003B,6},
  {0x03F8,10},{0xFF96,16},{0xFF97,16},{0xFF98,16},{0xFF99,16},{0xFF9A,16},
  {0xFF9B,16},{0xFF9C,16},{0xFF9D,16},{0x0000,0},{0x0000,0},{0x0000,0},
  {0x0000,0},{0x0000,0},{0x0000,0},{0x007A,7},{0x07F7,11},{0xFF9E,16},
  {0xFF9F,16},{0xFFA0,16},{0xFFA1,16},{0xFFA2,16},{0xFFA3,16},{0xFFA4,16},
  {0xFFA5,16},{0x0000,0},{0x0000,0},{0x0000,0},{0x0000,0},{0x0000,0},
  {0x0000,0},{0x007B,7},{0x0FF6,12},{0xFFA6,16},{0xFFA7,16},{0xFFA8,16},
  {0xFFA9,16},{0xFFAA,16},{0xFFAB,16},{0xFFAC,16},{0xFFAD,16},{0x0000,0},
  {0x0000,0},{0x0000,0},{0x0000,0},{0x0000,0},{0x0000,0},{0x00FA,8},
  {0x0FF7,12},{0xFFAE,16},{0xFFAF,16},{0xFFB0,16},{0xFFB1,16},{0xFFB2,16},
  {0xFFB3,16},{0xFFB4,16},{0xFFB5,16},{0x0000,0},{0x0000,0},{0x0000,0},
  {0x0000,0},{0x0000,0},{0x0000,0},{0x01F8,9},{0x7FC0,15},{0xFFB6,16},
  {0xFFB7,16},{0xFFB8,16},{0xFFB9,16},{0xFFBA,16},{0xFFBB,16},{0xFFBC,16},
  {0xFFBD,16},{0x0000,0},{0x0000,0},{0x0000,0},{0x0000,0},{0x0000,0},
  {0x0000,0},{0x01F9,9},{0xFFBE,16},{0xFFBF,16},{0xFFC0,16},{0xFFC1,16},
  {0xFFC2,16},{0xFFC3,16},{0xFFC4,16},{0xFFC5,16},{0xFFC6,16},{0x0000,0},
  {0x0000,0},{0x0000,0},{0x0000,0},{0x0000,0},{0x0000,0},{0x01FA,9},
  {0xFFC7,16},{0xFFC8,16},{0xFFC9,16},{0xFFCA,16},{0xFFCB,16},{0xFFCC,16},
  {0xFFCD,16},{0xFFCE,16},{0xFFCF,16},{0x0000,0},{0x0000,0},{0x0000,0},
  {0x0000,0},{0x0000,0},{0x0000,0},{0x03F9,10},{0xFFD0,16},{0xFFD1,16},
  {0xFFD2,16},{0xFFD3,16},{0xFFD4,16},{0xFFD5,16},{0xFFD6,16},{0xFFD7,16},
  {0xFFD8,16},{0x0000,0},{0x0000,0},{0x0000,0},{0x0000,0},{0x0000,0},
  {0x0000,0},{0x03FA,10},{0xFFD9,16},{0xFFDA,16},{0xFFDB,16},{0xFFDC,16},
  {0xFFDD,16},{0xFFDE,16},{0xFFDF,16},{0xFFE0,16},{0xFFE1,16},{0x0000,0},
  {0x0000,0},{0x0000,0},{0x0000,0},{0x0000,0},{0x0000,0},{0x07F8,11},
  {0xFFE2,16},{0xFFE3,16},{0xFFE4,16},{0xFFE5,16},{0xFFE6,16},{0xFFE7,16},
  {0xFFE8,16},{0xFFE9,16},{0xFFEA,16},{0x0000,0},{0x0000,0},{0x0000,0},
  {0x0000,0},{0x0000,0},{0x0000,0},{0xFFEB,16},{0xFFEC,16},{0xFFED,16},
  {0xFFEE,16},{0xFFEF,16},{0xFFF0,16},{0xFFF1,16},{0xFFF2,16},{0xFFF3,16},
  {0xFFF4,16},{0x0000,0},{0x0000,0},{0x0000,0},{0x0000,0},{0x0000,0},
  {0x07F9,11},{0xFFF5,16},{0xFFF6,16},{0xFFF7,16},{0xFFF8,16},{0xFFF9,16},
  {0xFFFA,16},{0xFFFB,16},{0xFFFC,16},{0xFFFD,16},{0xFFFE,16},{0x0000,0},
  {0x0000,0},{0x0000,0},{0x0000,0},{0x0000,0}
};

// Chroma AC Huffman table
const uint16_t MiniJPEG::chrACHuff[256][2] = {
  {0x0000,2},{0x0001,2},{0x0002,2},{0x0006,3},{0x000E,4},{0x000F,4},
  {0x001A,5},{0x001B,5},{0x003A,6},{0x0078,7},{0x0079,7},{0x0000,0},
  {0x0000,0},{0x0000,0},{0x0000,0},{0x0000,0},{0x0000,0},{0x000B,4},
  {0x0039,6},{0x00F6,8},{0x01F5,9},{0x07F6,11},{0x0FF4,12},{0xFF88,16},
  {0xFF89,16},{0xFF8A,16},{0xFF8B,16},{0x0000,0},{0x0000,0},{0x0000,0},
  {0x0000,0},{0x0000,0},{0x0000,0},{0x001A,5},{0x00F7,8},{0x03F7,10},
  {0x0FF5,12},{0x7FC2,15},{0xFF8C,16},{0xFF8D,16},{0xFF8E,16},{0xFF8F,16},
  {0xFF90,16},{0x0000,0},{0x0000,0},{0x0000,0},{0x0000,0},{0x0000,0},
  {0x0000,0},{0x001B,5},{0x00F8,8},{0x03F8,10},{0x0FF6,12},{0xFF91,16},
  {0xFF92,16},{0xFF93,16},{0xFF94,16},{0xFF95,16},{0xFF96,16},{0x0000,0},
  {0x0000,0},{0x0000,0},{0x0000,0},{0x0000,0},{0x0000,0},{0x003A,6},
  {0x01F6,9},{0xFF97,16},{0xFF98,16},{0xFF99,16},{0xFF9A,16},{0xFF9B,16},
  {0xFF9C,16},{0xFF9D,16},{0xFF9E,16},{0x0000,0},{0x0000,0},{0x0000,0},
  {0x0000,0},{0x0000,0},{0x0000,0},{0x003B,6},{0x03F9,10},{0xFF9F,16},
  {0xFFA0,16},{0xFFA1,16},{0xFFA2,16},{0xFFA3,16},{0xFFA4,16},{0xFFA5,16},
  {0xFFA6,16},{0x0000,0},{0x0000,0},{0x0000,0},{0x0000,0},{0x0000,0},
  {0x0000,0},{0x0079,7},{0x07F7,11},{0xFFA7,16},{0xFFA8,16},{0xFFA9,16},
  {0xFFAA,16},{0xFFAB,16},{0xFFAC,16},{0xFFAD,16},{0xFFAE,16},{0x0000,0},
  {0x0000,0},{0x0000,0},{0x0000,0},{0x0000,0},{0x0000,0},{0x007A,7},
  {0x07F8,11},{0xFFAF,16},{0xFFB0,16},{0xFFB1,16},{0xFFB2,16},{0xFFB3,16},
  {0xFFB4,16},{0xFFB5,16},{0xFFB6,16},{0x0000,0},{0x0000,0},{0x0000,0},
  {0x0000,0},{0x0000,0},{0x0000,0},{0x00F9,8},{0xFFB7,16},{0xFFB8,16},
  {0xFFB9,16},{0xFFBA,16},{0xFFBB,16},{0xFFBC,16},{0xFFBD,16},{0xFFBE,16},
  {0xFFBF,16},{0x0000,0},{0x0000,0},{0x0000,0},{0x0000,0},{0x0000,0},
  {0x0000,0},{0x01F7,9},{0xFFC0,16},{0xFFC1,16},{0xFFC2,16},{0xFFC3,16},
  {0xFFC4,16},{0xFFC5,16},{0xFFC6,16},{0xFFC7,16},{0xFFC8,16},{0x0000,0},
  {0x0000,0},{0x0000,0},{0x0000,0},{0x0000,0},{0x0000,0},{0x01F8,9},
  {0xFFC9,16},{0xFFCA,16},{0xFFCB,16},{0xFFCC,16},{0xFFCD,16},{0xFFCE,16},
  {0xFFCF,16},{0xFFD0,16},{0xFFD1,16},{0x0000,0},{0x0000,0},{0x0000,0},
  {0x0000,0},{0x0000,0},{0x0000,0},{0x01F9,9},{0xFFD2,16},{0xFFD3,16},
  {0xFFD4,16},{0xFFD5,16},{0xFFD6,16},{0xFFD7,16},{0xFFD8,16},{0xFFD9,16},
  {0xFFDA,16},{0x0000,0},{0x0000,0},{0x0000,0},{0x0000,0},{0x0000,0},
  {0x0000,0},{0x01FA,9},{0xFFDB,16},{0xFFDC,16},{0xFFDD,16},{0xFFDE,16},
  {0xFFDF,16},{0xFFE0,16},{0xFFE1,16},{0xFFE2,16},{0xFFE3,16},{0x0000,0},
  {0x0000,0},{0x0000,0},{0x0000,0},{0x0000,0},{0x0000,0},{0x07F9,11},
  {0xFFE4,16},{0xFFE5,16},{0xFFE6,16},{0xFFE7,16},{0xFFE8,16},{0xFFE9,16},
  {0xFFEA,16},{0xFFEB,16},{0xFFEC,16},{0x0000,0},{0x0000,0},{0x0000,0},
  {0x0000,0},{0x0000,0},{0x0000,0},{0x3FE0,14},{0xFFED,16},{0xFFEE,16},
  {0xFFEF,16},{0xFFF0,16},{0xFFF1,16},{0xFFF2,16},{0xFFF3,16},{0xFFF4,16},
  {0xFFF5,16},{0x0000,0},{0x0000,0},{0x0000,0},{0x0000,0},{0x0000,0},
  {0x03FA,10},{0x7FC3,15},{0xFFF6,16},{0xFFF7,16},{0xFFF8,16},{0xFFF9,16},
  {0xFFFA,16},{0xFFFB,16},{0xFFFC,16},{0xFFFD,16},{0xFFFE,16},{0x0000,0},
  {0x0000,0},{0x0000,0},{0x0000,0},{0x0000,0}
};
