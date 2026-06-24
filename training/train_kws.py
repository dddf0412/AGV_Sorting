"""
KWS (Keyword Spotting) 训练脚本
DS-CNN-S 模型, Hugging Face datasets (hf-mirror.com 国内加速)
输出: kws_model_int8.tflite (INT8 量化, 适配 STM32H753 + Cube.AI)

用法:
  set HF_ENDPOINT=https://hf-mirror.com
  conda activate kws
  python train_kws.py
"""

import os
import numpy as np
import tensorflow as tf
from tensorflow import keras
from pathlib import Path

# ===================== 配置 =====================

KEYWORDS = ["stop", "go", "yes", "no", "up", "down", "left", "right"]
SILENCE = "_silence_"
UNKNOWN = "_unknown_"

SAMPLE_RATE = 16000
CLIP_DURATION_MS = 1000
CLIP_SAMPLES = SAMPLE_RATE * CLIP_DURATION_MS // 1000

MFCC_WINDOW_SIZE_MS = 30
MFCC_WINDOW_STRIDE_MS = 10
MFCC_NUM_COEFF = 40
NUM_FRAMES = 49

BATCH_SIZE = 100
EPOCHS = 30
LEARNING_RATE = 0.001

OUTPUT_DIR = Path(__file__).parent
MODEL_H5     = OUTPUT_DIR / "kws_model.h5"
MODEL_TFLITE      = OUTPUT_DIR / "kws_model.tflite"
MODEL_TFLITE_INT8 = OUTPUT_DIR / "kws_model_int8.tflite"


# ===================== 数据准备 =====================

def load_dataset_hf():
    """
    从 Hugging Face 加载 Speech Commands (通过 hf-mirror.com 加速)
    https://huggingface.co/datasets/MLCommons/mlperf_speech_commands
    """
    import glob

    print("[1/4] Loading Speech Commands from local data/ ...")
    data_root = OUTPUT_DIR / "data"
    if not data_root.is_dir():
        raise FileNotFoundError(f"data/ not found at {data_root}")

    all_labels = KEYWORDS + [SILENCE, UNKNOWN]
    label_to_index = {name: i for i, name in enumerate(all_labels)}

    audio_list = []
    label_list = []

    max_per_kw = 3000
    max_unk = 2000
    max_sil = 3000

    # 收集每个关键词的 WAV 文件
    for kw in KEYWORDS:
        kw_dir = data_root / kw
        if kw_dir.is_dir():
            wavs = glob.glob(str(kw_dir / "*.wav"))
            wavs = wavs[:max_per_kw]
            for w in wavs:
                audio_list.append((w, SAMPLE_RATE))
                label_list.append(label_to_index[kw])
            print(f"  {kw}: {len(wavs)} samples")
        else:
            print(f"  {kw}: NOT FOUND (skip)")

    # unknown: 从其它词各取一点
    print("  collecting unknown samples...")
    unk_count = 0
    for subdir in sorted(data_root.iterdir()):
        if not subdir.is_dir():
            continue
        name = subdir.name
        if name in KEYWORDS or name.startswith("_"):
            continue
        wavs = glob.glob(str(subdir / "*.wav"))
        n = min(len(wavs), 200)
        for w in wavs[:n]:
            audio_list.append((w, SAMPLE_RATE))
            label_list.append(label_to_index[UNKNOWN])
            unk_count += 1
        if unk_count >= max_unk:
            break
    print(f"  unknown: {unk_count} samples")

    # silence: 用背景噪声
    print("  generating silence from background noise...")
    bg_dir = data_root / "_background_noise_"
    noise_file = bg_dir / "doing_the_dishes.wav" if (bg_dir / "doing_the_dishes.wav").exists() else (bg_dir / "white_noise.wav" if (bg_dir / "white_noise.wav").exists() else None)
    if noise_file:
        for _ in range(max_sil):
            audio_list.append((str(noise_file), SAMPLE_RATE))
            label_list.append(label_to_index[SILENCE])
    else:
        for _ in range(max_sil):
            audio_list.append((SILENCE_MARKER, SAMPLE_RATE))  # pure silence
            label_list.append(label_to_index[SILENCE])
    print(f"  silence: {max_sil} samples")

    indices = np.array(label_list, dtype=np.int32)

    print(f"  Total: {len(audio_list)}, unknown: {unk_count}, silence: {max_sil}")
    print(f"  Classes: {all_labels} ({len(all_labels)})")

    return audio_list, indices, all_labels, label_to_index


# ===================== 音频处理 =====================

def load_wav(filepath):
    """读取 WAV 文件 → float32 tensor → 1 秒"""
    audio_bin = tf.io.read_file(filepath)
    audio, _ = tf.audio.decode_wav(audio_bin, desired_channels=1)
    audio = tf.squeeze(audio, axis=-1)

    # 裁剪或补齐到 1 秒
    audio_len = tf.shape(audio)[0]
    if audio_len > CLIP_SAMPLES:
        offset = tf.random.uniform([], 0, audio_len - CLIP_SAMPLES, dtype=tf.int32)
        audio = audio[offset:offset + CLIP_SAMPLES]
    else:
        audio = tf.pad(audio, [[0, CLIP_SAMPLES - audio_len]])

    return audio


SILENCE_MARKER = "__silence__"

def preprocess_audio(filepath):
    """filepath (str or bytes tensor) → float32 tensor (16000,)"""
    # Decode bytes to str inside py_function
    if isinstance(filepath, bytes):
        filepath = filepath.decode('utf-8')
    elif not isinstance(filepath, str):
        filepath = filepath.numpy()
        if isinstance(filepath, bytes):
            filepath = filepath.decode('utf-8')
    if filepath == SILENCE_MARKER:
        return tf.zeros([CLIP_SAMPLES], dtype=tf.float32)
    return load_wav(filepath)


def get_spectrogram(audio):
    """log-Mel 频谱图: 1s音频 → (49, 40)"""
    frame_len = int(MFCC_WINDOW_SIZE_MS / 1000 * SAMPLE_RATE)
    frame_step = int(MFCC_WINDOW_STRIDE_MS / 1000 * SAMPLE_RATE)

    stft = tf.signal.stft(audio, frame_length=frame_len,
                          frame_step=frame_step, fft_length=512)
    spectrogram = tf.abs(stft)

    num_mel_bins = MFCC_NUM_COEFF
    mel_matrix = tf.signal.linear_to_mel_weight_matrix(
        num_mel_bins, tf.shape(spectrogram)[-1], SAMPLE_RATE, 20.0, SAMPLE_RATE / 2)
    mel = tf.tensordot(spectrogram, mel_matrix, 1)
    mel = tf.math.log(mel + 1e-6)
    mel = mel[..., :NUM_FRAMES, :]
    mel = tf.ensure_shape(mel, [NUM_FRAMES, MFCC_NUM_COEFF])
    return mel


def prepare_dataset(audio_list, indices, is_training):
    """构建 tf.data 管道"""
    # audio_list 元素: (filepath_str, sr_int) 或 (None, sr_int) for silence
    filepaths = [item[0] for item in audio_list]

    ds = tf.data.Dataset.from_tensor_slices((filepaths, indices))

    def load_and_mel(fp, lbl):
        audio = tf.ensure_shape(
            tf.py_function(func=preprocess_audio, inp=[fp], Tout=tf.float32),
            [CLIP_SAMPLES])
        mel = get_spectrogram(audio)
        return mel, lbl

    ds = ds.map(load_and_mel, num_parallel_calls=tf.data.AUTOTUNE)

    if is_training:
        ds = ds.shuffle(10000).repeat()

    ds = ds.batch(BATCH_SIZE).prefetch(tf.data.AUTOTUNE)
    return ds


# ===================== DS-CNN 模型 =====================

def build_ds_cnn(input_shape, num_classes):
    """
    DS-CNN-S — ~50K params, 适配 STM32H753 Cortex-M7
    """
    model = keras.Sequential(name="DS_CNN_S")
    model.add(keras.layers.Reshape(input_shape + (1,), input_shape=input_shape))

    # Block 1: conv
    model.add(keras.layers.Conv2D(64, (4, 10), strides=(2, 2), padding='same'))
    model.add(keras.layers.BatchNormalization())
    model.add(keras.layers.ReLU())

    # Block 2-4: depthwise separable conv
    for _ in range(3):
        model.add(keras.layers.DepthwiseConv2D((3, 3), strides=1, padding='same'))
        model.add(keras.layers.BatchNormalization())
        model.add(keras.layers.ReLU())
        model.add(keras.layers.Conv2D(64, (1, 1), strides=1, padding='same'))
        model.add(keras.layers.BatchNormalization())
        model.add(keras.layers.ReLU())

    model.add(keras.layers.AveragePooling2D(pool_size=(25, 1)))
    model.add(keras.layers.Flatten())
    model.add(keras.layers.Dense(num_classes, activation='softmax'))

    return model


# ===================== 训练主流程 =====================

def main():
    print("=" * 55)
    print("  KWS Training — DS-CNN-S (Hugging Face)")
    print(f"  Keywords: {KEYWORDS}")
    print(f"  SR: {SAMPLE_RATE} Hz, Frames: {NUM_FRAMES}, Mel: {MFCC_NUM_COEFF}")
    print("=" * 55)

    audio_list, indices, all_labels, label_map = load_dataset_hf()
    num_classes = len(all_labels)
    input_shape = (NUM_FRAMES, MFCC_NUM_COEFF)

    # 80/20 划分
    n_total = len(audio_list)
    split = int(n_total * 0.8)

    train_audio = audio_list[:split]
    train_labels = indices[:split]
    val_audio   = audio_list[split:]
    val_labels  = indices[split:]

    train_ds = prepare_dataset(train_audio, train_labels, is_training=True)
    val_ds   = prepare_dataset(val_audio,   val_labels,   is_training=False)

    train_steps = max(1, len(train_audio) // BATCH_SIZE)
    val_steps   = max(1, len(val_audio)   // BATCH_SIZE)

    # 构建
    print("\n[2/4] Building DS-CNN-S model...")
    model = build_ds_cnn(input_shape, num_classes)
    model.compile(
        optimizer=keras.optimizers.Adam(LEARNING_RATE),
        loss='sparse_categorical_crossentropy',
        metrics=['accuracy']
    )
    model.summary()

    # 训练
    print("\n[3/4] Training...")
    callbacks = [
        keras.callbacks.ReduceLROnPlateau(factor=0.5, patience=3, verbose=1),
        keras.callbacks.EarlyStopping(patience=8, restore_best_weights=True, verbose=1),
    ]
    model.fit(
        train_ds,
        steps_per_epoch=train_steps,
        epochs=EPOCHS,
        validation_data=val_ds,
        validation_steps=val_steps,
        callbacks=callbacks,
        verbose=2,
    )

    # 评估
    print("\nEvaluating...")
    loss, acc = model.evaluate(val_ds, steps=val_steps, verbose=0)
    print(f"  Validation Accuracy: {acc*100:.2f}%")

    # 导出
    print("\n[4/4] Exporting models...")

    # H5 给 Cube.AI
    model.save(MODEL_H5, include_optimizer=False)
    print(f"  H5: {MODEL_H5} ({MODEL_H5.stat().st_size / 1024:.0f} KB)")

    # TFLite 动态量化 (快速验证用)
    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    converter.optimizations = [tf.lite.Optimize.DEFAULT]
    tflite_model = converter.convert()
    with open(MODEL_TFLITE, 'wb') as f:
        f.write(tflite_model)
    print(f"  TFLite: {MODEL_TFLITE} ({len(tflite_model)/1024:.0f} KB)")

    # TFLite INT8 全量化 (推荐给 Cube.AI)
    def representative_dataset():
        for batch_start in range(0, 100, 10):
            batch_audio = []
            for i in range(batch_start, min(batch_start + 10, len(val_audio))):
                fp, _ = val_audio[i]
                a = preprocess_audio(fp)
                batch_audio.append(get_spectrogram(a))
            if batch_audio:
                yield [tf.stack(batch_audio)]

    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    converter.optimizations = [tf.lite.Optimize.DEFAULT]
    converter.representative_dataset = representative_dataset
    converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    converter.inference_input_type = tf.int8
    converter.inference_output_type = tf.int8

    tflite_int8 = converter.convert()
    with open(MODEL_TFLITE_INT8, 'wb') as f:
        f.write(tflite_int8)
    print(f"  TFLite INT8: {MODEL_TFLITE_INT8} ({len(tflite_int8)/1024:.0f} KB)")

    # 标签
    label_file = OUTPUT_DIR / "labels.txt"
    with open(label_file, 'w') as f:
        f.write('\n'.join(all_labels))
    print(f"  Labels: {label_file}")

    print("\nDone. 把 kws_model_int8.tflite 导入 Cube.AI。")


if __name__ == '__main__':
    main()
