"""
KWS 训练 — TC-ResNet8, PyTorch + GPU, Speech Commands 数据集
输出: kws_best.pt + kws_model_tc_resnet8.onnx → Cube.AI
"""

import glob
import numpy as np
from pathlib import Path

import torch
import torch.nn as nn
import torch.nn.functional as F
import torch.optim as optim
from torch.utils.data import Dataset, DataLoader
import torchaudio
from scipy.io import wavfile

# ===================== 配置 =====================

KEYWORDS = ["stop", "go", "yes", "no", "up", "down", "left", "right"]
SILENCE  = "_silence_"
UNKNOWN  = "_unknown_"

SAMPLE_RATE   = 16000
CLIP_SAMPLES  = SAMPLE_RATE
NUM_FRAMES    = 49
NUM_MELS      = 40
BATCH_SIZE    = 256
EPOCHS        = 120
LR            = 0.001

OUTPUT_DIR = Path(__file__).parent
DATA_ROOT  = OUTPUT_DIR / "data"
CACHE_FILE = OUTPUT_DIR / "spectrograms.pt"

device = torch.device("cuda" if torch.cuda.is_available() else "cpu")


# ===================== 频谱预计算 =====================

def compute_mel(audio_1d):
    mel_t = torchaudio.transforms.MelSpectrogram(
        sample_rate=SAMPLE_RATE, n_fft=512, n_mels=NUM_MELS,
        win_length=480, hop_length=160, f_min=20.0, f_max=8000.0, power=2.0,
    ).to(audio_1d.device)
    mel = mel_t(audio_1d).T
    mel = torch.log(mel + 1e-6)
    return mel


def precompute(filepaths, labels_arr, max_sil):
    print("[1/4] Precomputing Mel spectrograms...")
    mels_list, lbls = [], []
    for i, fp in enumerate(filepaths):
        if i % 5000 == 0:
            print(f"  {i}/{len(filepaths)}...")
        sr, data = wavfile.read(fp)
        if data.ndim > 1:
            data = data[:, 0]
        audio = torch.from_numpy(data.astype(np.float32) / 32768.0)
        if sr != SAMPLE_RATE:
            audio = torchaudio.functional.resample(audio, sr, SAMPLE_RATE)
        if audio.shape[0] < CLIP_SAMPLES:
            audio = F.pad(audio, (0, CLIP_SAMPLES - audio.shape[0]))
        mel = compute_mel(audio.to(device))
        mels_list.append(mel.cpu())
        lbls.append(labels_arr[i])

    print(f"  {max_sil} silence...")
    smel = compute_mel(torch.zeros(CLIP_SAMPLES, device=device)).cpu()
    for _ in range(max_sil):
        mels_list.append(smel.clone())
        lbls.append(len(KEYWORDS))

    return mels_list, torch.tensor(lbls, dtype=torch.long)


class SpecDataset(Dataset):
    def __init__(self, mels, y, is_train):
        self.mels, self.y, self.is_train = mels, y, is_train
    def __len__(self):
        return len(self.mels)
    def __getitem__(self, idx):
        mel = self.mels[idx]
        T = mel.shape[0]
        if self.is_train and T > NUM_FRAMES:
            start = torch.randint(0, T - NUM_FRAMES + 1, (1,)).item()
        else:
            start = max(0, (T - NUM_FRAMES) // 2)
        mel = mel[start:start + NUM_FRAMES]
        if mel.shape[0] < NUM_FRAMES:
            mel = F.pad(mel, (0, 0, 0, NUM_FRAMES - mel.shape[0]))
        return mel, self.y[idx]


# ===================== TC-ResNet8 =====================

class TC_ResNet8(nn.Module):
    def __init__(self, num_classes):
        super().__init__()
        self.conv1 = nn.Conv1d(NUM_MELS, 16, 3, padding=1, bias=False)
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

    def _residual(self, x, conv1, bn1, conv2, bn2, conv3, bn3, skip):
        r = skip(x)
        x = F.relu(bn1(conv1(x)))
        x = F.relu(bn2(conv2(x)))
        x = bn3(conv3(x))
        return F.relu(x + r)

    def forward(self, x):
        x = x.permute(0, 2, 1)
        x = F.relu(self.bn1(self.conv1(x)))
        x = self._residual(x, self.b1_conv1, self.b1_bn1, self.b1_conv2,
                           self.b1_bn2, self.b1_conv3, self.b1_bn3, self.b1_skip)
        x = self._residual(x, self.b2_conv1, self.b2_bn1, self.b2_conv2,
                           self.b2_bn2, self.b2_conv3, self.b2_bn3, self.b2_skip)
        x = self._residual(x, self.b3_conv1, self.b3_bn1, self.b3_conv2,
                           self.b3_bn2, self.b3_conv3, self.b3_bn3, self.b3_skip)
        x = F.adaptive_avg_pool1d(x, 1).squeeze(-1)
        return self.fc(x)


# ===================== Training =====================

def run_epoch(model, loader, opt, crit, is_train):
    model.train() if is_train else model.eval()
    total_loss, correct, total = 0.0, 0, 0
    for x, y in loader:
        x, y = x.to(device), y.to(device)
        if is_train:
            opt.zero_grad()
            out = model(x)
            loss = crit(out, y)
            loss.backward()
            opt.step()
        else:
            with torch.no_grad():
                out = model(x)
                loss = crit(out, y)
        total_loss += loss.item()
        correct += (out.argmax(1) == y).sum().item()
        total += y.size(0)
    return total_loss / len(loader), correct / total


def export_onnx(model, filepath):
    model.eval()
    dummy = torch.randn(1, NUM_FRAMES, NUM_MELS).to(device)
    torch.onnx.export(model, dummy, str(filepath),
                      input_names=["input"], output_names=["output"],
                      dynamic_axes={"input": {0: "batch"}, "output": {0: "batch"}},
                      opset_version=12, do_constant_folding=True)
    print(f"  ONNX: {filepath} ({filepath.stat().st_size/1024:.0f} KB)")


# ===================== Main =====================

def main():
    print(f"=== KWS Training | {device} ===")
    all_labels = KEYWORDS + [SILENCE, UNKNOWN]
    lmap = {n: i for i, n in enumerate(all_labels)}

    if CACHE_FILE.exists():
        print("[1/4] Loading cached spectrograms...")
        data = torch.load(CACHE_FILE, weights_only=True)
        mels_list, y = data['mels'], data['y']
    else:
        fps, lbls = [], []
        for kw in KEYWORDS:
            d = DATA_ROOT / kw
            if d.is_dir():
                ws = sorted(glob.glob(str(d / "*.wav")))[:3000]
                fps.extend(ws)
                lbls.extend([lmap[kw]] * len(ws))
                print(f"  {kw}: {len(ws)}")

        unk = 0
        for sd in sorted(DATA_ROOT.iterdir()):
            if not sd.is_dir() or sd.name in KEYWORDS or sd.name.startswith("_"):
                continue
            for w in sorted(glob.glob(str(sd / "*.wav")))[:200]:
                fps.append(w); lbls.append(lmap[UNKNOWN]); unk += 1
                if unk >= 2000: break
            if unk >= 2000: break

        max_sil = 3000
        print(f"  unknown: {unk}, silence: {max_sil}")
        mels_list, y = precompute(fps, np.array(lbls, dtype=np.int64), max_sil)
        torch.save({'mels': mels_list, 'y': y}, CACHE_FILE)

    num_classes = len(all_labels)
    n = len(mels_list)
    perm = torch.randperm(n)
    split = int(n * 0.8)
    train_mels = [mels_list[i] for i in perm[:split]]
    train_y = y[perm[:split]]
    val_mels   = [mels_list[i] for i in perm[split:]]
    val_y = y[perm[split:]]

    train_ds = SpecDataset(train_mels, train_y, True)
    val_ds   = SpecDataset(val_mels,   val_y,   False)
    train_ld = DataLoader(train_ds, BATCH_SIZE, shuffle=True,  num_workers=0, pin_memory=True)
    val_ld   = DataLoader(val_ds,   BATCH_SIZE, shuffle=False, num_workers=0, pin_memory=True)

    print(f"\n[2/4] Model: TC-ResNet8")
    model = TC_ResNet8(num_classes).to(device)
    print(f"  Params: {sum(p.numel() for p in model.parameters()):,}")

    opt   = optim.Adam(model.parameters(), lr=LR, weight_decay=1e-4)
    crit  = nn.CrossEntropyLoss()
    sched = optim.lr_scheduler.CosineAnnealingLR(opt, T_max=EPOCHS)

    print("\n[3/4] Training...")
    best_acc = 0.0
    for ep in range(EPOCHS):
        t_loss, t_acc = run_epoch(model, train_ld, opt, crit, True)
        v_loss, v_acc = run_epoch(model, val_ld,   None, crit, False)
        sched.step()
        star = ""
        if v_acc > best_acc:
            best_acc = v_acc
            torch.save(model.state_dict(), OUTPUT_DIR / "kws_best.pt")
            star = " *"
        print(f"  Ep {ep+1:2d}/{EPOCHS} | tr_loss={t_loss:.3f} tr_acc={t_acc*100:.1f}%"
              f" | va_loss={v_loss:.3f} va_acc={v_acc*100:.1f}%{star}")

    print(f"\n  Best val acc: {best_acc*100:.2f}%")

    print("\n[4/4] Exporting ONNX...")
    model.load_state_dict(torch.load(OUTPUT_DIR / "kws_best.pt", weights_only=True))
    export_onnx(model, OUTPUT_DIR / "kws_model_tc_resnet8.onnx")
    with open(OUTPUT_DIR / "labels.txt", 'w') as f:
        f.write('\n'.join(all_labels) + '\n')
    print("  labels.txt saved")
    print(f"\nDone. → kws_model_tc_resnet8.onnx → Cube.AI")


if __name__ == '__main__':
    main()
