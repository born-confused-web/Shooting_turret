#pragma once
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

// =============================================================
// MiniJPEG - Minimal color JPEG encoder for ESP32 + OV7670
// RGB565 input (low byte first, high byte second)
// 4:2:0 chroma subsampling, standard Huffman coding
// =============================================================

class MiniJPEG {
public:
  static int encode(const uint8_t *rgb565, uint8_t *jpegOut,
                    int width, int height, int quality,
                    int outBufSize) {
    // Build quantization tables
    int q = qualityScale(quality);
    uint8_t qLum[64], qChr[64];
    buildQuantTable(qLum, stdLumQuant, q);
    buildQuantTable(qChr, stdChrQuant, q);

    // Build Huffman code tables from standard bit/val arrays
    uint16_t lumDC[12][2], chrDC[12][2];
    uint16_t lumAC[256][2], chrAC[256][2];
    buildHuffCodes(lumDCBits, lumDCVals, 12,  lumDC);
    buildHuffCodes(chrDCBits, chrDCVals, 12,  chrDC);
    buildHuffCodes(lumACBits, lumACVals, 162, lumAC);
    buildHuffCodes(chrACBits, chrACVals, 162, chrAC);

    BitWriter bw(jpegOut, outBufSize);

    writeHeader(bw, width, height, qLum, qChr);

    int mcuW = (width  + 15) / 16;
    int mcuH = (height + 15) / 16;
    int dcY = 0, dcCb = 0, dcCr = 0;

    for (int my = 0; my < mcuH; my++) {
      for (int mx = 0; mx < mcuW; mx++) {
        int8_t  Y0[64], Y1[64], Y2[64], Y3[64];
        int8_t  Cb[64], Cr[64];
        int16_t dct[64];

        extractMCU(rgb565, width, height, mx*16, my*16,
                   Y0, Y1, Y2, Y3, Cb, Cr);

        fdct(Y0, dct); quantize(dct, qLum);
        dcY  = encodeBlock(bw, dct, dcY,  lumDC, lumAC);
        fdct(Y1, dct); quantize(dct, qLum);
        dcY  = encodeBlock(bw, dct, dcY,  lumDC, lumAC);
        fdct(Y2, dct); quantize(dct, qLum);
        dcY  = encodeBlock(bw, dct, dcY,  lumDC, lumAC);
        fdct(Y3, dct); quantize(dct, qLum);
        dcY  = encodeBlock(bw, dct, dcY,  lumDC, lumAC);
        fdct(Cb, dct); quantize(dct, qChr);
        dcCb = encodeBlock(bw, dct, dcCb, chrDC, chrAC);
        fdct(Cr, dct); quantize(dct, qChr);
        dcCr = encodeBlock(bw, dct, dcCr, chrDC, chrAC);

        if (bw.overflow) return -1;
      }
    }

    bw.flush();
    bw.writeByte(0xFF);
    bw.writeByte(0xD9);
    return bw.overflow ? -1 : bw.pos();
  }

private:
  // ---- Bit writer ----
  struct BitWriter {
    uint8_t *buf;
    int      _pos, maxPos;
    uint32_t acc;
    int      bits;
    bool     overflow;

    BitWriter(uint8_t *b, int maxSize)
      : buf(b), _pos(0), maxPos(maxSize), acc(0), bits(0), overflow(false) {}

    void writeByte(uint8_t c) {
      if (_pos >= maxPos) { overflow = true; return; }
      buf[_pos++] = c;
    }
    void writeWord(uint16_t w) { writeByte(w>>8); writeByte(w&0xFF); }
    void writeBits(uint32_t code, int len) {
      acc  = (acc << len) | (code & ((1u << len) - 1));
      bits += len;
      while (bits >= 8) {
        bits -= 8;
        uint8_t c = (acc >> bits) & 0xFF;
        writeByte(c);
        if (c == 0xFF) writeByte(0x00);
      }
    }
    void flush() {
      if (bits > 0) {
        uint8_t c = (acc << (8 - bits)) & 0xFF;
        writeByte(c);
        if (c == 0xFF) writeByte(0x00);
        bits = 0; acc = 0;
      }
    }
    int pos() { return _pos; }
  };

  // ---- Build Huffman code table from bits/vals arrays ----
  // out[symbol][0] = code value, out[symbol][1] = code length
  static void buildHuffCodes(const uint8_t *bits, const uint8_t *vals,
                              int nVals, uint16_t out[][2]) {
    memset(out, 0, 256 * 2 * sizeof(uint16_t));
    int code = 0;
    int vi   = 0;
    for (int len = 1; len <= 16; len++) {
      for (int i = 0; i < bits[len-1]; i++) {
        uint8_t sym    = vals[vi++];
        out[sym][0]    = (uint16_t)code;
        out[sym][1]    = (uint16_t)len;
        code++;
        if (vi >= nVals) goto done;
      }
      code <<= 1;
    }
    done:;
  }

  // ---- Quality scale factor ----
  static int qualityScale(int q) {
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

  // ---- Extract one 16x16 MCU ----
  static void extractMCU(const uint8_t *rgb565, int w, int h,
                          int bx, int by,
                          int8_t *Y0, int8_t *Y1,
                          int8_t *Y2, int8_t *Y3,
                          int8_t *Cb, int8_t *Cr) {
    int8_t  Ytmp[16][16];
    int16_t Cbtmp[16][16], Crtmp[16][16];

    for (int dy = 0; dy < 16; dy++) {
      for (int dx = 0; dx < 16; dx++) {
        int px = bx + dx; if (px >= w) px = w - 1;
        int py = by + dy; if (py >= h) py = h - 1;

        int      idx = (py * w + px) * 2;
        uint16_t p   = (uint16_t)rgb565[idx] |
                       ((uint16_t)rgb565[idx+1] << 8);

        int R = ((p >> 11) & 0x1F) << 3;
        int G = ((p >>  5) & 0x3F) << 2;
        int B = ( p        & 0x1F) << 3;

        // RGB → YCbCr (JPEG/BT.601 full range)
        int Y  = (( 66*R + 129*G +  25*B + 128) >> 8) + 16;
        int Cbt= ((-38*R -  74*G + 112*B + 128) >> 8) + 128;
        int Crt= ((112*R -  94*G -  18*B + 128) >> 8) + 128;

        Ytmp [dy][dx] = (int8_t)(Y   - 128);
        Cbtmp[dy][dx] = Cbt;
        Crtmp[dy][dx] = Crt;
      }
    }

    for (int y = 0; y < 8; y++)
      for (int x = 0; x < 8; x++) {
        Y0[y*8+x] = Ytmp[y  ][x  ];
        Y1[y*8+x] = Ytmp[y  ][x+8];
        Y2[y*8+x] = Ytmp[y+8][x  ];
        Y3[y*8+x] = Ytmp[y+8][x+8];
      }

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

  // ---- Forward DCT (AAN) ----
  static void fdct(const int8_t *in, int16_t *out) {
    int32_t tmp[64];
    for (int i = 0; i < 8; i++) {
      const int8_t *s = in + i*8;
      int32_t *d = tmp + i*8;
      int32_t t0=s[0]+s[7], t7=s[0]-s[7];
      int32_t t1=s[1]+s[6], t6=s[1]-s[6];
      int32_t t2=s[2]+s[5], t5=s[2]-s[5];
      int32_t t3=s[3]+s[4], t4=s[3]-s[4];
      int32_t u0=t0+t3, u3=t0-t3, u1=t1+t2, u2=t1-t2;
      d[0]=u0+u1; d[4]=u0-u1;
      int32_t z1=((u2+u3)*181)>>8;
      d[2]=u3+z1; d[6]=u3-z1;
      int32_t z5=(t4-t6)*98;
      int32_t z2=(z5+t4*139)>>8, z4=(z5+t6*334)>>8, z3=(t5*181)>>8;
      int32_t z11=t7+z3, z13=t7-z3;
      d[5]=z13+z2; d[3]=z13-z2; d[1]=z11+z4; d[7]=z11-z4;
    }
    for (int i = 0; i < 8; i++) {
      int32_t *d = tmp + i;
      int32_t t0=d[0*8]+d[7*8], t7=d[0*8]-d[7*8];
      int32_t t1=d[1*8]+d[6*8], t6=d[1*8]-d[6*8];
      int32_t t2=d[2*8]+d[5*8], t5=d[2*8]-d[5*8];
      int32_t t3=d[3*8]+d[4*8], t4=d[3*8]-d[4*8];
      int32_t u0=t0+t3, u3=t0-t3, u1=t1+t2, u2=t1-t2;
      out[i+0*8]=(int16_t)((u0+u1)>>3); out[i+4*8]=(int16_t)((u0-u1)>>3);
      int32_t z1=((u2+u3)*181)>>8;
      out[i+2*8]=(int16_t)((u3+z1)>>3); out[i+6*8]=(int16_t)((u3-z1)>>3);
      int32_t z5=(t4-t6)*98;
      int32_t z2=(z5+t4*139)>>8, z4=(z5+t6*334)>>8, z3=(t5*181)>>8;
      int32_t z11=t7+z3, z13=t7-z3;
      out[i+5*8]=(int16_t)((z13+z2)>>3); out[i+3*8]=(int16_t)((z13-z2)>>3);
      out[i+1*8]=(int16_t)((z11+z4)>>3); out[i+7*8]=(int16_t)((z11-z4)>>3);
    }
  }

  // ---- Quantize in zigzag order ----
  static const uint8_t zigzag[64];

  static void quantize(int16_t *dct, const uint8_t *qtab) {
    int16_t tmp[64];
    for (int i = 0; i < 64; i++) {
      int v = dct[zigzag[i]];
      int q = qtab[i];
      tmp[i] = (int16_t)(v >= 0 ? (v + q/2) / q : -((-v + q/2) / q));
    }
    memcpy(dct, tmp, 64 * sizeof(int16_t));
  }

  // ---- VLI encoding helpers ----
  static int category(int v) {
    if (v < 0) v = -v;
    int c = 0;
    while (v) { c++; v >>= 1; }
    return c;
  }
  static int vliCode(int v, int cat) {
    return (v < 0) ? (v - 1 + (1 << cat)) : v;
  }

  // ---- Encode one 8x8 block ----
  static int encodeBlock(BitWriter &bw, const int16_t *block, int dcPred,
                          const uint16_t dc[][2],
                          const uint16_t ac[][2]) {
    // DC
    int diff = block[0] - dcPred;
    int cat  = category(diff);
    bw.writeBits(dc[cat][0], dc[cat][1]);
    if (cat) bw.writeBits(vliCode(diff, cat), cat);

    // AC
    int run = 0;
    for (int i = 1; i < 64; i++) {
      int v = block[i];
      if (v == 0) { run++; continue; }
      while (run >= 16) {
        // ZRL (run of 16 zeros)
        bw.writeBits(ac[0xF0][0], ac[0xF0][1]);
        run -= 16;
      }
      int cat2 = category(v);
      int sym  = (run << 4) | cat2;
      bw.writeBits(ac[sym][0], ac[sym][1]);
      bw.writeBits(vliCode(v, cat2), cat2);
      run = 0;
    }
    // EOB
    bw.writeBits(ac[0x00][0], ac[0x00][1]);
    return block[0];
  }

  // ---- Write JPEG header ----
  static void writeHeader(BitWriter &bw, int w, int h,
                           const uint8_t *qL, const uint8_t *qC) {
    // SOI
    bw.writeByte(0xFF); bw.writeByte(0xD8);
    // APP0
    bw.writeByte(0xFF); bw.writeByte(0xE0);
    bw.writeWord(16);
    const uint8_t app0[] = {'J','F','I','F',0,1,1,0,0,1,0,1,0,0};
    for (auto b : app0) bw.writeByte(b);

    // DQT luma
    bw.writeByte(0xFF); bw.writeByte(0xDB);
    bw.writeWord(67); bw.writeByte(0x00);
    for (int i = 0; i < 64; i++) bw.writeByte(qL[i]);

    // DQT chroma
    bw.writeByte(0xFF); bw.writeByte(0xDB);
    bw.writeWord(67); bw.writeByte(0x01);
    for (int i = 0; i < 64; i++) bw.writeByte(qC[i]);

    // SOF0
    bw.writeByte(0xFF); bw.writeByte(0xC0);
    bw.writeWord(17);
    bw.writeByte(8);
    bw.writeWord(h); bw.writeWord(w);
    bw.writeByte(3);
    bw.writeByte(1); bw.writeByte(0x22); bw.writeByte(0);
    bw.writeByte(2); bw.writeByte(0x11); bw.writeByte(1);
    bw.writeByte(3); bw.writeByte(0x11); bw.writeByte(1);

    // DHT luma DC
    bw.writeByte(0xFF); bw.writeByte(0xC4);
    bw.writeWord(2 + 1 + 16 + 12);
    bw.writeByte(0x00);
    for (int i = 0; i < 16; i++) bw.writeByte(lumDCBits[i]);
    for (int i = 0; i < 12; i++) bw.writeByte(lumDCVals[i]);

    // DHT luma AC
    bw.writeByte(0xFF); bw.writeByte(0xC4);
    bw.writeWord(2 + 1 + 16 + 162);
    bw.writeByte(0x10);
    for (int i = 0; i < 16;  i++) bw.writeByte(lumACBits[i]);
    for (int i = 0; i < 162; i++) bw.writeByte(lumACVals[i]);

    // DHT chroma DC
    bw.writeByte(0xFF); bw.writeByte(0xC4);
    bw.writeWord(2 + 1 + 16 + 12);
    bw.writeByte(0x01);
    for (int i = 0; i < 16; i++) bw.writeByte(chrDCBits[i]);
    for (int i = 0; i < 12; i++) bw.writeByte(chrDCVals[i]);

    // DHT chroma AC
    bw.writeByte(0xFF); bw.writeByte(0xC4);
    bw.writeWord(2 + 1 + 16 + 162);
    bw.writeByte(0x11);
    for (int i = 0; i < 16;  i++) bw.writeByte(chrACBits[i]);
    for (int i = 0; i < 162; i++) bw.writeByte(chrACVals[i]);

    // SOS
    bw.writeByte(0xFF); bw.writeByte(0xDA);
    bw.writeWord(12); bw.writeByte(3);
    bw.writeByte(1); bw.writeByte(0x00);
    bw.writeByte(2); bw.writeByte(0x11);
    bw.writeByte(3); bw.writeByte(0x11);
    bw.writeByte(0); bw.writeByte(63); bw.writeByte(0);
  }

  // ---- Standard tables ----
  static const uint8_t stdLumQuant[64];
  static const uint8_t stdChrQuant[64];
  static const uint8_t lumDCBits[16];
  static const uint8_t lumDCVals[12];
  static const uint8_t lumACBits[16];
  static const uint8_t lumACVals[162];
  static const uint8_t chrDCBits[16];
  static const uint8_t chrDCVals[12];
  static const uint8_t chrACBits[16];
  static const uint8_t chrACVals[162];
};

// ---- Static data ----

const uint8_t MiniJPEG::zigzag[64] = {
   0, 1, 8,16, 9, 2, 3,10,17,24,32,25,18,11, 4, 5,
  12,19,26,33,40,48,41,34,27,20,13, 6, 7,14,21,28,
  35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,
  58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63
};

const uint8_t MiniJPEG::stdLumQuant[64] = {
  16,11,10,16,24,40,51,61,12,12,14,19,26,58,60,55,
  14,13,16,24,40,57,69,56,14,17,22,29,51,87,80,62,
  18,22,37,56,68,109,103,77,24,35,55,64,81,104,113,92,
  49,64,78,87,103,121,120,101,72,92,95,98,112,100,103,99
};

const uint8_t MiniJPEG::stdChrQuant[64] = {
  17,18,24,47,99,99,99,99,18,21,26,66,99,99,99,99,
  24,26,56,99,99,99,99,99,47,66,99,99,99,99,99,99,
  99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,
  99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99
};

const uint8_t MiniJPEG::lumDCBits[16] = {0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0};
const uint8_t MiniJPEG::lumDCVals[12] = {0,1,2,3,4,5,6,7,8,9,10,11};

const uint8_t MiniJPEG::lumACBits[16] = {0,2,1,3,3,2,4,3,5,5,4,4,0,0,1,125};
const uint8_t MiniJPEG::lumACVals[162] = {
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

const uint8_t MiniJPEG::chrDCBits[16] = {0,3,1,1,1,1,1,1,1,1,1,0,0,0,0,0};
const uint8_t MiniJPEG::chrDCVals[12] = {0,1,2,3,4,5,6,7,8,9,10,11};

const uint8_t MiniJPEG::chrACBits[16] = {0,2,1,2,4,4,3,4,7,5,4,4,0,1,2,119};
const uint8_t MiniJPEG::chrACVals[162] = {
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