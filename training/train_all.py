"""自动训练 3 个 KWS 模型并对比, 120 epochs each"""
import subprocess, sys, re
from pathlib import Path

MODELS = ["ds_cnn_s", "se_ds_cnn", "tc_resnet8"]
BASE = Path(__file__).parent
SCRIPT = BASE / "train_kws_pytorch.py"

results = {}

for m in MODELS:
    print(f"\n{'='*60}\n  Training: {m}\n{'='*60}")

    # Switch MODEL_TYPE
    code = SCRIPT.read_text(encoding='utf-8')
    code = re.sub(r'MODEL_TYPE = "[^"]*"', f'MODEL_TYPE = "{m}"', code)
    SCRIPT.write_text(code, encoding='utf-8')

    # Clear only best checkpoint (cache stays)
    (BASE / "kws_best.pt").unlink(missing_ok=True)

    cp = subprocess.run([sys.executable, str(SCRIPT)], cwd=str(BASE))
    if cp.returncode != 0:
        results[m] = "FAILED"
        continue

    onnx = BASE / f"kws_model_{m}.onnx"
    results[m] = f"{onnx.stat().st_size/1024:.0f} KB" if onnx.exists() else "no onnx"
    print(f"  OK: {m} — ONNX: {results[m]}")

# Restore default model type
code = SCRIPT.read_text(encoding='utf-8')
code = re.sub(r'MODEL_TYPE = "[^"]*"', 'MODEL_TYPE = "tc_resnet8"', code)
SCRIPT.write_text(code, encoding='utf-8')

print(f"\n{'='*60}")
print("  COMPARISON (120 epochs each)")
print(f"{'='*60}")
for m in MODELS:
    print(f"  {m:<15}  {results.get(m, 'N/A')}")
print(f"{'='*60}")
