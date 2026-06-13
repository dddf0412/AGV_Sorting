"""
接收 H753 USART3 发送的 OV2640 JPEG 帧, OpenCV 实时窗口显示。
协议: 0xAA 0x55 TYPE[2] SIZE[4] RESERVED[2] JPEG_DATA[...]
用法: python camera_viewer.py COM3 [baud]
依赖: pip install pyserial opencv-python numpy
"""

import sys
import struct

try:
    import serial
except ImportError:
    print("pip install pyserial opencv-python numpy")
    sys.exit(1)

try:
    import cv2
    import numpy as np
except ImportError:
    print("pip install opencv-python numpy")
    sys.exit(1)


def recv_one_frame(ser):
    """接收一帧 JPEG, 返回 jpeg_bytes 或 None"""
    sync = bytearray()
    while True:
        b = ser.read(1)
        if not b:
            return None
        sync.append(b[0])
        if len(sync) > 2:
            sync.pop(0)
        if len(sync) == 2 and sync[0] == 0xAA and sync[1] == 0x55:
            break

    hdr = ser.read(8)
    if len(hdr) < 8:
        return None

    # type: hdr[0:2], 1=JPEG
    dlen = struct.unpack("<I", hdr[2:6])[0]

    if dlen == 0 or dlen > 200 * 1024:
        return None

    data = bytearray()
    remaining = dlen
    while remaining > 0:
        chunk = ser.read(min(remaining, 4096))
        if not chunk:
            break
        data.extend(chunk)
        remaining -= len(chunk)

    if len(data) < dlen:
        return None
    return bytes(data)


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else "COM3"
    baud = int(sys.argv[2]) if len(sys.argv) > 2 else 3000000

    ser = serial.Serial(port, baud, timeout=30)
    print(f"打开 {port} @ {baud}, 等待 JPEG 帧...")

    cv2.namedWindow("OV2640 Camera", cv2.WINDOW_NORMAL)
    cv2.resizeWindow("OV2640 Camera", 640, 480)

    fcnt = 0
    try:
        while True:
            jpeg = recv_one_frame(ser)
            if jpeg is None:
                print("接收超时")
                break

            # JPEG → numpy → OpenCV
            nparr = np.frombuffer(jpeg, np.uint8)
            img = cv2.imdecode(nparr, cv2.IMREAD_COLOR)
            img = cv2.flip(img, 0)  # 垂直翻转

            if img is None:
                print("JPEG 解码失败")
                continue

            fcnt += 1
            if fcnt <= 3 or fcnt % 30 == 0:
                print(f"Frame {fcnt}: {len(jpeg)} bytes, {img.shape[1]}x{img.shape[0]}")

            cv2.imshow("OV2640 Camera", img)
            if cv2.waitKey(1) & 0xFF == 27:  # ESC 退出
                break

    except KeyboardInterrupt:
        print("\n中断")
    finally:
        ser.close()
        cv2.destroyAllWindows()
        print(f"结束, 共 {fcnt} 帧")


if __name__ == "__main__":
    main()
