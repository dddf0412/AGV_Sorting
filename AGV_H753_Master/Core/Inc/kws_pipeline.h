#ifndef __KWS_H
#define __KWS_H

#include <stdint.h>

#define KWS_CLASS_COUNT 10
#define KWS_LABEL_LEN   16

typedef struct {
    uint8_t class_id;
    uint8_t confidence;       /* 0-100, gap-based */
    char    label[KWS_LABEL_LEN];
} KWS_Result_t;

void KWS_Init(void);
void KWS_Feed(int16_t *buf, uint16_t len);  /* 38.4kHz PCM chunk */
uint8_t KWS_IsResultReady(void);
KWS_Result_t KWS_GetResult(void);

#endif
