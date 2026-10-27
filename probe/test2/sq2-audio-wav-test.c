/* Play the same bounded 48 kHz stereo PCM WAV used by tinyplay through AAudio. */
#include <aaudio/AAudio.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static uint32_t le32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t le16(const unsigned char *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "usage: %s reference.wav\n", argv[0]); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror("WAV"); return 1; }
    unsigned char h[16];
    uint32_t bytes = 0;
    int format_ok = 0, result = 1;
    AAudioStreamBuilder *b = NULL;
    AAudioStream *s = NULL;
    if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h+8, "WAVE", 4)) goto done;
    while (fread(h, 1, 8, f) == 8) {
        uint32_t n = le32(h+4);
        if (n > 48000U * 4 * 120) goto done;
        if (!memcmp(h, "fmt ", 4)) {
            if (n < 16 || fread(h, 1, 16, f) != 16) goto done;
            format_ok = le16(h)==1 && le16(h+2)==2 && le32(h+4)==48000 &&
                        le32(h+8)==192000 && le16(h+12)==4 && le16(h+14)==16;
            if (fseek(f, (long)(n-16+(n&1)), SEEK_CUR)) goto done;
        } else if (!memcmp(h, "data", 4)) {
            if (!format_ok || !n || n % 4) goto done;
            bytes = n;
            break;
        } else if (fseek(f, (long)(n+(n&1)), SEEK_CUR)) goto done;
    }
    if (!bytes || AAudio_createStreamBuilder(&b) != AAUDIO_OK) goto done;
    AAudioStreamBuilder_setDirection(b, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_NONE);
    AAudioStreamBuilder_setSampleRate(b, 48000);
    AAudioStreamBuilder_setChannelCount(b, 2);
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setUsage(b, AAUDIO_USAGE_MEDIA);
    AAudioStreamBuilder_setContentType(b, AAUDIO_CONTENT_TYPE_MUSIC);
    if (AAudioStreamBuilder_openStream(b, &s) != AAUDIO_OK) goto done;
    if (AAudioStream_getSampleRate(s)!=48000 || AAudioStream_getChannelCount(s)!=2 ||
        AAudioStream_getFormat(s)!=AAUDIO_FORMAT_PCM_I16) goto done;
    printf("AAUDIO_WAV rate=48000 channels=2 device=%d frames=%u\n",
           AAudioStream_getDeviceId(s), bytes/4);
    if (AAudioStream_requestStart(s) != AAUDIO_OK) goto done;
    int16_t samples[480*2];
    int64_t total = 0;
    while (bytes) {
        uint32_t n = bytes < sizeof(samples) ? bytes : sizeof(samples);
        if (fread(samples, 1, n, f) != n) goto done;
        int frames = (int)n / 4, offset = 0;
        while (offset < frames) {
            int r = AAudioStream_write(s, samples+offset*2, frames-offset, 1000000000LL);
            if (r <= 0) { fprintf(stderr, "write: %s\n", AAudio_convertResultToText(r)); goto done; }
            offset += r;
            total += r;
        }
        bytes -= n;
    }
    printf("AAUDIO_ACTIVE_XRUNS=%d written=%lld\n", AAudioStream_getXRunCount(s), (long long)total);
    /* Reference files contain a silent tail. Wait at most two seconds for drain. */
    for (int i=0; i<200 && AAudioStream_getFramesRead(s)<total; i++) usleep(10000);
    if (AAudioStream_getFramesRead(s) < total) goto done;
    result = 0;
done:
    if (s) { AAudioStream_requestStop(s); AAudioStream_close(s); }
    if (b) AAudioStreamBuilder_delete(b);
    fclose(f);
    if (result) fprintf(stderr, "WAV/AAudio check failed\n");
    return result;
}
