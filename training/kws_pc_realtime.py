"""
PC端 KWS 实时推理 — USART3 mic + TC-ResNet8 model
"""

import sys, struct, time
import numpy as np
import torch
import torch.nn as nn
import torchaudio
import serial
from pathlib import Path

MIC_SR = 38400
TARGET_SR = 16000
NUM_FRAMES = 49
NUM_MELS = 40
KEYWORDS = ["stop", "go", "yes", "no", "up", "down", "left", "right",
            "_silence_", "_unknown_"]
MODEL_PT = Path(__file__).parent / "kws_best.pt"


class TC_ResNet8(nn.Module):
    def __init__(self, num_classes):
        super().__init__()
        self.conv1 = nn.Conv1d(40, 16, 3, padding=1, bias=False)
        self.bn1   = nn.BatchNorm1d(16)
        self.b1_conv1 = nn.Conv1d(16, 24, 3, padding=1, bias=False)
        self.b1_bn1   = nn.BatchNorm1d(24)
        self.b1_conv2 = nn.Conv1d(24, 24, 3, padding=1, bias=False)
        self.b1_bn2   = nn.BatchNorm1d(24)
        self.b1_conv3 = nn.Conv1d(24, 24, 1, bias=False)
        self.b1_bn3   = nn.BatchNorm1d(24)
        self.b1_skip  = nn.Conv1d(16, 24, 1, bias=False)
        self.b2_conv1 = nn.Conv1d(24, 32, 3, stride=2, padding=1, bias=False)
        self.b2_bn1   = nn.BatchNorm1d(32)
        self.b2_conv2 = nn.Conv1d(32, 32, 3, padding=1, bias=False)
        self.b2_bn2   = nn.BatchNorm1d(32)
        self.b2_conv3 = nn.Conv1d(32, 32, 1, bias=False)
        self.b2_bn3   = nn.BatchNorm1d(32)
        self.b2_skip  = nn.Conv1d(24, 32, 1, stride=2, bias=False)
        self.b3_conv1 = nn.Conv1d(32, 48, 3, stride=2, padding=1, bias=False)
        self.b3_bn1   = nn.BatchNorm1d(48)
        self.b3_conv2 = nn.Conv1d(48, 48, 3, padding=1, bias=False)
        self.b3_bn2   = nn.BatchNorm1d(48)
        self.b3_conv3 = nn.Conv1d(48, 48, 1, bias=False)
        self.b3_bn3   = nn.BatchNorm1d(48)
        self.b3_skip  = nn.Conv1d(32, 48, 1, stride=2, bias=False)
        self.fc = nn.Linear(48, num_classes)

    def _block(self, x, bi):
        blk = lambda n: getattr(self, f'b{bi}_' + n)
        r = blk('skip')(x)
        x = torch.relu(blk('bn1')(blk('conv1')(x)))
        x = torch.relu(blk('bn2')(blk('conv2')(x)))
        x = blk('bn3')(blk('conv3')(x))
        return torch.relu(x + r)

    def forward(self, x):
        x = x.permute(0, 2, 1)
        x = torch.relu(self.bn1(self.conv1(x)))
        x = self._block(x, 1); x = self._block(x, 2); x = self._block(x, 3)
        x = nn.functional.adaptive_avg_pool1d(x, 1).squeeze(-1)
        return self.fc(x)


def compute_mel(audio_16k):
    if audio_16k.shape[0] < 16000:
        audio_16k = nn.functional.pad(audio_16k, (0, 16000 - audio_16k.shape[0]))
    audio_16k = audio_16k[:16000]
    mel_t = torchaudio.transforms.MelSpectrogram(
        sample_rate=16000, n_fft=512, n_mels=40,
        win_length=480, hop_length=160, f_min=20.0, f_max=8000.0, power=2.0)
    mel = mel_t(audio_16k).T
    mel = torch.log(mel + 1e-6)[:NUM_FRAMES, :]
    if mel.shape[0] < NUM_FRAMES:
        mel = nn.functional.pad(mel, (0, 0, 0, NUM_FRAMES - mel.shape[0]))
    return mel


def resample_linear(src, src_sr, dst_sr):
    ratio = src_sr / dst_sr
    dst_len = int(len(src) / ratio)
    out = np.zeros(dst_len, dtype=np.float32)
    for i in range(dst_len):
        pos = i * ratio
        idx = int(pos)
        frac = pos - idx
        a = src[idx] if idx < len(src) else 0
        b = src[idx + 1] if idx + 1 < len(src) else 0
        out[i] = a + (b - a) * frac
    return out


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else "COM6"
    print("Loading model...")
    model = TC_ResNet8(10).eval()
    model.load_state_dict(torch.load(MODEL_PT, weights_only=True))
    print(f"Model ready. {port} @ 3Mbps. Speak!\n")

    ser = serial.Serial(port, 3000000, timeout=0.5)
    buf_384 = np.array([], dtype=np.int16)
    chunk_samples = MIC_SR  # 1 second = 38400

    last_label = -1
    debounce = 0
    infer_n = 0

    while True:
        raw = ser.read(8192)
        if len(raw) < 12:
            continue

        # Search for 0xAA55 header; extract PCM payloads
        pos = 0
        while pos < len(raw) - 12:
            if raw[pos] == 0xAA and raw[pos+1] == 0x55 and raw[pos+2] == 2:
                dlen = struct.unpack('<I', raw[pos+8:pos+12])[0]
                if pos + 12 + dlen <= len(raw):
                    pcm = np.frombuffer(raw[pos+12:pos+12+dlen], dtype=np.int16)
                    buf_384 = np.concatenate([buf_384, pcm])
                    pos += 12 + dlen
                else:
                    break
            else:
                pos += 1

        # Inference when enough data
        if len(buf_384) >= chunk_samples:
            chunk = buf_384[:chunk_samples]
            buf_384 = buf_384[chunk_samples // 2:]

            audio_f = chunk.astype(np.float32) / 32768.0
            rms = float(np.sqrt(np.mean(audio_f ** 2)))

            # VAD: skip quiet audio entirely (no mel/inference)
            if rms < 0.1:
                if infer_n % 30 == 0:
                    print(f"  [silent, rms={rms:.4f}]")
                infer_n += 1
                continue

            # Audio loud enough — run inference
            audio_16k = resample_linear(audio_f, MIC_SR, TARGET_SR)
            audio_t = torch.from_numpy(audio_16k.copy())
            mel = compute_mel(audio_t)

            with torch.no_grad():
                logits = model(mel.unsqueeze(0))[0]
                probs = torch.softmax(logits, dim=0)
                top_idx = int(probs.argmax())
                top_conf = float(probs[top_idx])

            infer_n += 1
            if infer_n % 3 == 0:
                t = time.strftime("%H:%M:%S")
                bars = " | ".join(f"{KEYWORDS[i]:<10} {probs[i]*100:4.0f}%" for i in range(10))
                print(f"[{t}] rms={rms:.4f}")
                print(f"  {bars}")

            # Debounce + threshold: only keywords with high confidence
            if top_idx >= 8 or top_conf < 0.70:
                last_label = -1
                debounce = 0
            else:
                if top_idx == last_label:
                    debounce += 1
                    if debounce == 2:
                        print(f"\n  >>> {KEYWORDS[top_idx]} ({top_conf*100:.0f}%) <<<\n")
                else:
                    last_label = top_idx
                    debounce = 1


if __name__ == '__main__':
    main()
