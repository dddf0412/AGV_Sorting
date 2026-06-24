"""
PC端 KWS 实时测试 — 用电脑麦克风录音, 跑和训练一样的 Mel+模型
验证模型是否有问题
"""

import numpy as np
import torch
import torch.nn as nn
import torchaudio
import sounddevice as sd
from pathlib import Path

SAMPLE_RATE = 16000
NUM_FRAMES = 49
NUM_MELS = 40
KEYWORDS = ["stop", "go", "yes", "no", "up", "down", "left", "right",
            "_silence_", "_unknown_"]


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

    def _block(self, x, i):
        blk = lambda name: getattr(self, f'b{i}_' + name)
        residual = blk('skip')(x)
        x = torch.relu(blk('bn1')(blk('conv1')(x)))
        x = torch.relu(blk('bn2')(blk('conv2')(x)))
        x = blk('bn3')(blk('conv3')(x))
        return torch.relu(x + residual)

    def forward(self, x):
        x = x.permute(0, 2, 1)
        x = torch.relu(self.bn1(self.conv1(x)))
        x = self._block(x, 1)
        x = self._block(x, 2)
        x = self._block(x, 3)
        x = nn.functional.adaptive_avg_pool1d(x, 1).squeeze(-1)
        return self.fc(x)


def load_model():
    # Load weights from PyTorch checkpoint
    chk = torch.load(Path(__file__).parent / "kws_best.pt", weights_only=True)
    model = TC_ResNet8(10).eval()
    model.load_state_dict(chk)
    return model


def compute_mel(audio):
    mel_t = torchaudio.transforms.MelSpectrogram(
        sample_rate=16000, n_fft=512, n_mels=40,
        win_length=480, hop_length=160, f_min=20.0, f_max=8000.0,
        power=2.0,
    )
    mel = mel_t(audio).T
    mel = torch.log(mel + 1e-6)
    return mel


def predict(model, audio):
    """audio: 1D tensor, 16000 samples @ 16kHz"""
    if audio.shape[0] < 16000:
        audio = torch.nn.functional.pad(audio, (0, 16000 - audio.shape[0]))
    elif audio.shape[0] > 16000:
        audio = audio[:16000]

    mel = compute_mel(audio)
    mel = mel[:49, :]
    if mel.shape[0] < 49:
        mel = torch.nn.functional.pad(mel, (0, 0, 0, 49 - mel.shape[0]))

    with torch.no_grad():
        out = model(mel.unsqueeze(0))
        probs = torch.softmax(out, dim=1)[0]
        top = int(probs.argmax())
        conf = float(probs[top])
        print(f"\n  -> {KEYWORDS[top]} ({conf*100:.0f}%)")
        for i in range(10):
            bar = "#" * int(probs[i] * 50)
            print(f"  {KEYWORDS[i]:<12} {bar}")
        print()


def main():
    print("Loading model...")
    model = load_model()
    model.eval()
    print("Ready. Press Ctrl+C to stop.\n")

    # List audio devices
    print("Audio devices:")
    for i, d in enumerate(sd.query_devices()):
        in_ch = d['max_input_channels']
        if in_ch > 0:
            print(f"  {i}: {d['name']} (in={in_ch})")
    print()

    def callback(indata, frames, time, status):
        if status:
            print(f"Status: {status}")
        audio = torch.from_numpy(indata[:, 0].astype(np.float32))
        predict(model, audio)

    with sd.InputStream(samplerate=16000, channels=1, callback=callback,
                        blocksize=16000):
        print("Listening... say a keyword!")
        input("Press Enter to stop...\n")


if __name__ == '__main__':
    main()
