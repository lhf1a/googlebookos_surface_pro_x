/* Low-level public AAudio client: exercises Android AudioFlinger/HAL, not ALSA directly. */
#include <aaudio/AAudio.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

int main(void) {
    AAudioStreamBuilder *b = NULL;
    AAudioStream *s = NULL;
    aaudio_result_t r = AAudio_createStreamBuilder(&b);
    if (r != AAUDIO_OK) return 1;
    AAudioStreamBuilder_setDirection(b, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_NONE);
    AAudioStreamBuilder_setSampleRate(b, 48000);
    AAudioStreamBuilder_setChannelCount(b, 2);
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setUsage(b, AAUDIO_USAGE_MEDIA);
    AAudioStreamBuilder_setContentType(b, AAUDIO_CONTENT_TYPE_MUSIC);
    r = AAudioStreamBuilder_openStream(b, &s);
    AAudioStreamBuilder_delete(b);
    if (r != AAUDIO_OK) { fprintf(stderr,"open: %s\n",AAudio_convertResultToText(r)); return 1; }
    printf("AAudio rate=%d channels=%d format=%d device=%d burst=%d\n",
           AAudioStream_getSampleRate(s), AAudioStream_getChannelCount(s),
           AAudioStream_getFormat(s), AAudioStream_getDeviceId(s), AAudioStream_getFramesPerBurst(s));
    if (AAudioStream_getSampleRate(s)!=48000 || AAudioStream_getChannelCount(s)!=2 ||
        AAudioStream_getFormat(s)!=AAUDIO_FORMAT_PCM_I16) { AAudioStream_close(s); return 2; }
    r=AAudioStream_requestStart(s);
    if(r!=AAUDIO_OK) { fprintf(stderr,"start: %s\n",AAudio_convertResultToText(r)); AAudioStream_close(s); return 1; }
    int16_t samples[480*2];
    int total=0;
    for (int block=0; block<300; block++) {
        for(int i=0;i<480;i++) {
            double t=(block*480+i)/48000.0;
            samples[2*i]=samples[2*i+1]=0;
            int ch=(t>=1.5)?1:0;
            double start=ch?1.7:0.3, end=ch?2.7:1.3;
            if(t>=start && t<end) {
                double fade=fmin(1.0,fmin((t-start)/0.02,(end-t)/0.02));
                samples[2*i+ch]=(int16_t)(4096*fade*sin(2*3.141592653589793*(ch?660:440)*t));
            }
        }
        int offset=0;
        while(offset<480) {
            r=AAudioStream_write(s,samples+offset*2,480-offset,1000000000LL);
            if(r<=0) { fprintf(stderr,"write: %s\n",AAudio_convertResultToText(r)); AAudioStream_close(s); return 1; }
            offset+=r; total+=r;
        }
    }
    /* Separate active-write underruns from the intentional empty-queue tail.
     * Sleeping 300 ms after the final write can itself create an underrun;
     * that count must not be reported as evidence of interrupted tone audio.
     */
    printf("AAUDIO_ACTIVE_XRUNS=%d written=%d read=%lld\n",
           AAudioStream_getXRunCount(s), total,
           (long long)AAudioStream_getFramesRead(s));
    usleep(300000);
    printf("AAUDIO_WRITTEN=%d frames, read=%lld, xruns=%d\n",total,
           (long long)AAudioStream_getFramesRead(s),AAudioStream_getXRunCount(s));
    AAudioStream_requestStop(s);
    AAudioStream_close(s);
    return 0;
}
