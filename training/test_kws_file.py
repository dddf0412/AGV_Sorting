"""
用 Speech Commands WAV 文件测试模型准确率
"""
import glob, random
import numpy as np
import torch
import torch.nn as nn
import torchaudio
from scipy.io import wavfile
from pathlib import Path

SAMPLE_RATE = 16000
NUM_FRAMES = 49
NUM_MELS = 40
KEYWORDS = ["stop", "go", "yes", "no", "up", "down", "left", "right",
            "_silence_", "_unknown_"]
DATA = Path(__file__).parent / "data"
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


def infer(model, audio):
    if audio.shape[0] < 16000:
        audio = torch.nn.functional.pad(audio, (0, 16000 - audio.shape[0]))
    elif audio.shape[0] > 16000:
        audio = audio[:16000]
    mel_t = torchaudio.transforms.MelSpectrogram(
        sample_rate=16000, n_fft=512, n_mels=40,
        win_length=480, hop_length=160, f_min=20.0, f_max=8000.0, power=2.0)
    mel = mel_t(audio).T
    mel = torch.log(mel + 1e-6)[:NUM_FRAMES, :]
    if mel.shape[0] < NUM_FRAMES:
        mel = torch.nn.functional.pad(mel, (0, 0, 0, NUM_FRAMES - mel.shape[0]))
    with torch.no_grad():
        out = model(mel.unsqueeze(0))
        prob = torch.softmax(out, dim=1)[0]
        pred = int(prob.argmax())
    return pred, float(prob[pred])


def main():
    print("Loading model...")
    model = TC_ResNet8(10).eval()
    model.load_state_dict(torch.load(MODEL_PT, weights_only=True))
    print("Testing on Speech Commands files...\n")

    correct, total = 0, 0
    per_kw = {k: [0, 0] for k in KEYWORDS[:8]}

    for kw in KEYWORDS[:8]:
        wavs = glob.glob(str(DATA / kw / "*.wav"))
        if not wavs:
            print(f"  {kw}: no files"); continue
        samples = random.sample(wavs, min(50, len(wavs)))
        for w in samples:
            sr, data = wavfile.read(w)
            if data.ndim > 1: data = data[:, 0]
            audio = torch.from_numpy(data.astype(np.float32) / 32768.0)
            if sr != 16000:
                audio = torchaudio.functional.resample(audio, sr, 16000)
            pred, conf = infer(model, audio)
            label = KEYWORDS.index(kw)
            total += 1; per_kw[kw][1] += 1
            if pred == label:
                correct += 1; per_kw[kw][0] += 1

    print(f"\nOverall: {correct}/{total} ({100*correct/total:.1f}%)")
    for k in KEYWORDS[:8]:
        c, t = per_kw[k]
        print(f"  {k:<10} {c}/{t} ({100*c/t:.1f}%)" if t else f"  {k:<10} N/A")


if __name__ == '__main__':
    main()
