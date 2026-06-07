import socket
import numpy as np
import cv2
import struct
import time

UDP_PORT  = 5005
FRAME_W   = 160
FRAME_H   = 120
FRAME_BYTES = FRAME_W * FRAME_H * 2

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
sock.bind(("", UDP_PORT))
sock.settimeout(2.0)

# Buffer to assemble frames
# Key = frameNum, Value = bytearray
frame_buf = {}

fps_count = 0
fps_time  = time.time()

print(f"Listening on UDP port {UDP_PORT}...")
print("Press ESC to quit.")

while True:
    try:
        data, addr = sock.recvfrom(2048)

        if len(data) < 6:
            continue

        # Parse 6-byte header
        frame_num   = data[0]
        offset      = (data[1] << 16) | (data[2] << 8) | data[3]
        total_bytes = (data[4] <<  8) | data[5]
        payload     = data[6:]

        # Start new frame buffer if needed
        if frame_num not in frame_buf:
            frame_buf[frame_num] = bytearray(total_bytes)

        # Copy payload into correct position
        end = offset + len(payload)
        if end <= total_bytes:
            frame_buf[frame_num][offset:end] = payload

        # Check if frame is complete
        if end >= total_bytes:
            raw = frame_buf.pop(frame_num)

            # Decode RGB565 — byte order A confirmed:
            # b0 = low byte, b1 = high byte → uint16 = b0 | (b1 << 8)
            arr = np.frombuffer(raw, dtype=np.uint8).reshape(FRAME_H, FRAME_W, 2)
            uint16 = arr[:,:,0].astype(np.uint16) | \
                     (arr[:,:,1].astype(np.uint16) << 8)

            r = ((uint16 >> 11) & 0x1F).astype(np.uint8) << 3
            g = ((uint16 >>  5) & 0x3F).astype(np.uint8) << 2
            b = ( uint16        & 0x1F).astype(np.uint8) << 3

            bgr = np.stack([b, g, r], axis=2)
            display = cv2.resize(bgr, (640, 480),
                                 interpolation=cv2.INTER_NEAREST)
            cv2.imshow("OV7670 Live", display)

            fps_count += 1
            now = time.time()
            if now - fps_time >= 1.0:
                print(f"FPS: {fps_count} | frame_num={frame_num}")
                fps_count = 0
                fps_time  = now

            # Clean up old incomplete frames
            frame_buf = {k: v for k, v in frame_buf.items()
                         if k >= frame_num}

        if cv2.waitKey(1) == 27:  # ESC
            break

    except socket.timeout:
        print("No data received — check ESP32 is running and PC_IP is correct")

sock.close()
cv2.destroyAllWindows()