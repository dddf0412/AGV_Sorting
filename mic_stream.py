"""
实时麦克风流式播放工具
接收 STM32 USART3 的 PCM 音频帧，通过扬声器实时播放

用法:
    pip install pyserial pyaudio
    python mic_stream.py COM3

按 Ctrl+C 停止。
"""

import sys
import struct
import serial
import pyaudio
import queue
import threading

# 音频参数 (与 STM32 一致)
SAMPLE_RATE = 38400
CHANNELS    = 1
BITS        = 16
CHUNK_SIZE  = 256  # 每帧样本数


def serial_reader(ser, q):
    """从串口读取 PCM 帧，放入队列"""
    while True:
        # 同步头
        while True:
            b = ser.read(1)
            if not b:
                return
            if b[0] == 0xAA:
                b2 = ser.read(1)
                if b2 and b2[0] == 0x55:
                    break

        hdr = ser.read(10)
        if len(hdr) < 10:
            return

        ftype   = hdr[0]
        ch      = hdr[1]
        sr      = hdr[2] | (hdr[3] << 8)
        bits    = hdr[4] | (hdr[5] << 8)
        dlen    = hdr[6] | (hdr[7] << 8) | (hdr[8] << 16) | (hdr[9] << 24)

        if ftype != 2:
            # 跳过非 PCM 帧
            while dlen > 0:
                ser.read(min(dlen, 512))
                dlen -= 512
            continue

        data = bytearray()
        while len(data) < dlen:
            chunk = ser.read(min(dlen - len(data), 4096))
            if not chunk:
                return
            data.extend(chunk)

        q.put(bytes(data))


def audio_player(q, stop_event):
    """从队列取数据，实时播放"""
    p = pyaudio.PyAudio()
    stream = p.open(format=pyaudio.paInt16,
                    channels=CHANNELS,
                    rate=SAMPLE_RATE,
                    output=True,
                    frames_per_buffer=CHUNK_SIZE)

    # 预缓冲 4 帧防止欠载
    buffer = b''
    min_bytes = CHUNK_SIZE * 2 * 4  # 4 frames

    try:
        while not stop_event.is_set():
            try:
                data = q.get(timeout=0.1)
                buffer += data
            except queue.Empty:
                continue

            # 缓冲够了就开始播放
            while len(buffer) >= CHUNK_SIZE * 2:
                stream.write(buffer[:CHUNK_SIZE * 2])
                buffer = buffer[CHUNK_SIZE * 2:]
    finally:
        stream.stop_stream()
        stream.close()
        p.terminate()


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else 'COM3'

    print(f"Mic Live Stream — {port} @ 3Mbps")
    print(f"Audio: {SAMPLE_RATE}Hz / {CHANNELS}ch / {BITS}bit | chunk={CHUNK_SIZE}")
    print("Press Ctrl+C to stop.\n")

    ser = serial.Serial(port, 3000000, timeout=2)

    q = queue.Queue(maxsize=64)
    stop = threading.Event()

    reader_thread = threading.Thread(target=serial_reader, args=(ser, q), daemon=True)
    player_thread = threading.Thread(target=audio_player, args=(q, stop), daemon=True)

    reader_thread.start()
    player_thread.start()

    try:
        while True:
            reader_thread.join(0.5)
            if not reader_thread.is_alive():
                print("\nSerial disconnected.")
                break
    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        stop.set()
        ser.close()


if __name__ == '__main__':
    main()
