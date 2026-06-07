import socket
import numpy as np
import cv2
import time

UDP_PORT = 5005

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1024 * 1024)
sock.bind(("", UDP_PORT))
sock.settimeout(2.0)

frame_buf = {}
fps_count = 0
fps_time  = time.time()

print(f"Listening for JPEG on UDP port {UDP_PORT}...")
print("Press ESC to quit.")

while True:
    try:
        data, _ = sock.recvfrom(2048)
        if len(data) < 6:
            continue

        frame_num   = data[0]
        offset      = (data[1] << 16) | (data[2] << 8) | data[3]
        total_bytes = (data[4] <<  8) | data[5]
        payload     = data[6:]

        if frame_num not in frame_buf:
            frame_buf[frame_num] = bytearray(total_bytes)

        end = offset + len(payload)
        if end <= total_bytes:
            frame_buf[frame_num][offset:end] = payload

        # Complete frame received
        if end >= total_bytes:
            jpeg_data = bytes(frame_buf.pop(frame_num))

            # Decode JPEG directly with OpenCV
            arr   = np.frombuffer(jpeg_data, dtype=np.uint8)
            frame = cv2.imdecode(arr, cv2.IMREAD_COLOR)

            if frame is not None:
                display = cv2.resize(frame, (640, 480),
                                     interpolation=cv2.INTER_NEAREST)
                cv2.imshow("OV7670 JPEG Stream", display)

                fps_count += 1
                now = time.time()
                if now - fps_time >= 1.0:
                    print(f"FPS: {fps_count} | "
                          f"JPEG: {total_bytes} bytes | "
                          f"frame={frame_num}")
                    fps_count = 0
                    fps_time  = now
            else:
                print(f"Failed to decode JPEG frame {frame_num} "
                      f"({total_bytes} bytes)")

            # Drop old incomplete frames
            frame_buf = {k: v for k, v in frame_buf.items()
                         if k >= frame_num}

        if cv2.waitKey(1) == 27:
            break

    except socket.timeout:
        print("No data — check ESP32 is running and PC_IP is correct")

sock.close()
cv2.destroyAllWindows()
