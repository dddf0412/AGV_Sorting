#include "kws_pipeline.h"
#include "kws_constants.h"
#include "app_x-cube-ai.h"
#include "kws.h"
#include "ai_platform.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *kws_labels[KWS_CLASS_COUNT] = {
    "stop", "go", "yes", "no", "up", "down", "left", "right",
    "_silence_", "_unknown_"
};

extern ai_handle kws;
extern ai_buffer *ai_input;
extern ai_buffer *ai_output;

#define MIC_SR         38400
#define TGT_SR         16000
#define RESAMPLE_RATIO 2.4f
#define CHUNK_SAMPLES  16000
#define CHUNK_OVERLAP  8000
#define N_FRAMES       49
#define N_MELS         40
#define N_FFT          512
#define WIN_LEN        480
#define HOP_LEN        160
#define VAD_THRESHOLD  0.10f
#define MIC_GAIN       1.0f

static float  audio_buf[CHUNK_SAMPLES];
static uint32_t audio_cnt;
static float  mel_spec[N_FRAMES][N_MELS];
static float  fft_real[N_FFT];
static float  fft_imag[N_FFT];
static KWS_Result_t last_result;
static uint8_t  result_ready;

/* ---------------- FFT ---------------- */
static void fft_radix2(float *real, float *imag, uint16_t n)
{
    uint16_t i, j, s, m;
    float tr, ti, wr, wi, wmr, wmi;
    j = 0;
    for (i = 0; i < n; i++) {
        if (i < j) {
            tr = real[i]; real[i] = real[j]; real[j] = tr;
            ti = imag[i]; imag[i] = imag[j]; imag[j] = ti;
        }
        m = n >> 1;
        while (m >= 1 && j >= m) { j -= m; m >>= 1; }
        j += m;
    }
    for (s = 2; s <= n; s <<= 1) {
        m = s >> 1;
        wmr = cosf(-6.283185307f / (float)s);
        wmi = sinf(-6.283185307f / (float)s);
        for (i = 0; i < n; i += s) {
            wr = 1.0f; wi = 0.0f;
            for (j = 0; j < m; j++) {
                uint16_t a = i + j, b = a + m;
                tr = wr * real[b] - wi * imag[b];
                ti = wr * imag[b] + wi * real[b];
                real[b] = real[a] - tr;
                imag[b] = imag[a] - ti;
                real[a] += tr;
                imag[a] += ti;
                tr = wr * wmr - wi * wmi;
                wi = wr * wmi + wi * wmr;
                wr = tr;
            }
        }
    }
}

/* ---------------- Mel frame ---------------- */
static void compute_mel_frame(const float *audio_480, float *mel_out)
{
    uint16_t i, j;
    for (i = 0; i < WIN_LEN; i++) {
        fft_real[i] = audio_480[i] * kws_hann_window[i];
        fft_imag[i] = 0.0f;
    }
    for (i = WIN_LEN; i < N_FFT; i++) {
        fft_real[i] = 0.0f;
        fft_imag[i] = 0.0f;
    }
    fft_radix2(fft_real, fft_imag, N_FFT);
    for (j = 0; j < N_MELS; j++) {
        float sum = 0.0f;
        for (i = 0; i <= N_FFT / 2; i++) {
            float pwr = fft_real[i] * fft_real[i] + fft_imag[i] * fft_imag[i];
            sum += pwr * kws_mel_fbank[i][j];
        }
        mel_out[j] = logf(sum + 1e-6f);
    }
}

/* ---------------- Resample ---------------- */
static uint16_t resample_normalized(const int16_t *in, uint16_t in_len,
                                    float *out, uint16_t out_max, float *pos)
{
    uint16_t cnt = 0;
    float p = *pos;
    while (cnt < out_max && (int)p < (int)in_len - 1) {
        int i = (int)p;
        float f = p - (float)i;
        float s = ((float)in[i] * (1.0f - f) + (float)in[i + 1] * f) * (MIC_GAIN / 32768.0f);
        out[cnt++] = s;
        p += RESAMPLE_RATIO;
    }
    p -= (float)in_len;
    if (p < 0.0f) p = 0.0f;
    *pos = p;
    return cnt;
}

/* ---------------- Inference ---------------- */
static void run_inference(void)
{
    float *input  = (float *)ai_input[0].data;
    float *output = (float *)ai_output[0].data;

    for (uint16_t t = 0; t < N_FRAMES; t++)
        for (uint16_t m = 0; m < N_MELS; m++)
            input[t * N_MELS + m] = mel_spec[t][m];

    if (ai_kws_run(kws, ai_input, ai_output) != 1)
        return;

    float max_logit = output[0];
    for (int i = 1; i < KWS_CLASS_COUNT; i++)
        if (output[i] > max_logit) max_logit = output[i];

    float exp_sum = 0.0f, exp_vals[KWS_CLASS_COUNT];
    for (int i = 0; i < KWS_CLASS_COUNT; i++) {
        exp_vals[i] = expf(output[i] - max_logit);
        exp_sum += exp_vals[i];
    }

    float probs[KWS_CLASS_COUNT], top_val = 0.0f;
    uint8_t top_id = 0;
    for (int i = 0; i < KWS_CLASS_COUNT; i++) {
        probs[i] = exp_sum > 0.0f ? exp_vals[i] / exp_sum : 0.0f;
        if (probs[i] > top_val) { top_val = probs[i]; top_id = i; }
    }

    /* Debounced report: same keyword ×2, >=70% confidence */
    static uint8_t last_id = 0xFF, db_cnt = 0;
    if (top_id < 8 && top_val >= 0.70f) {
        if (top_id == last_id) {
            if (++db_cnt >= 2 && !result_ready) {
                last_result.class_id   = top_id;
                last_result.confidence = (uint8_t)(top_val * 100.0f + 0.5f);
                strncpy(last_result.label, kws_labels[top_id], KWS_LABEL_LEN - 1);
                last_result.label[KWS_LABEL_LEN - 1] = '\0';
                result_ready = 1;
                db_cnt = 0;
            }
        } else {
            last_id = top_id;
            db_cnt = 1;
        }
    } else {
        db_cnt = 0;
    }
}

/* ---------------- Public API ---------------- */

void KWS_Init(void)
{
    audio_cnt  = 0;
    result_ready = 0;
    memset(audio_buf, 0, sizeof(audio_buf));
    memset(mel_spec,  0, sizeof(mel_spec));
    memset(&last_result, 0, sizeof(last_result));
}

void KWS_Feed(int16_t *buf, uint16_t len)
{
    static float rs_pos = 0.0f;
    float tmp[112];
    uint16_t n16 = resample_normalized(buf, len, tmp, 112, &rs_pos);
    for (uint16_t i = 0; i < n16 && audio_cnt < CHUNK_SAMPLES; i++)
        audio_buf[audio_cnt++] = tmp[i];
    if (audio_cnt < CHUNK_SAMPLES) return;

    float rms_sq = 0.0f;
    for (uint16_t i = 0; i < CHUNK_SAMPLES; i++)
        rms_sq += audio_buf[i] * audio_buf[i];
    float rms = sqrtf(rms_sq / (float)CHUNK_SAMPLES);

    static uint32_t chunk_n = 0;
    chunk_n++;

    for (uint16_t f = 0; f < N_FRAMES; f++) {
        float win_audio[WIN_LEN];
        int start = (int)f * HOP_LEN - WIN_LEN / 2;
        for (int i = 0; i < WIN_LEN; i++) {
            int idx = start + i;
            if (idx < 0) idx = -idx;
            else if (idx >= CHUNK_SAMPLES) idx = 2 * CHUNK_SAMPLES - 2 - idx;
            if (idx < 0) idx = 0;
            if (idx >= CHUNK_SAMPLES) idx = CHUNK_SAMPLES - 1;
            win_audio[i] = audio_buf[idx];
        }
        compute_mel_frame(win_audio, mel_spec[f]);
    }

    if (rms >= VAD_THRESHOLD && chunk_n > 2)
        run_inference();

    memmove(audio_buf, audio_buf + CHUNK_OVERLAP, CHUNK_OVERLAP * sizeof(float));
    audio_cnt = CHUNK_OVERLAP;
}

uint8_t KWS_IsResultReady(void)  { return result_ready; }
KWS_Result_t KWS_GetResult(void) { result_ready = 0; return last_result; }
