/* Thin JNI owner; the existing diagnostic retains the codec/bitstream loop. */
#include <android/native_window_jni.h>
#include <jni.h>
#include <stdio.h>
extern int sq2_mediacodec_frames_run(int argc,char **argv,ANativeWindow *window);
#ifdef SQ2_HEVC_VECTOR
#define SQ2_WINDOW_CODEC "OMX.qcom.video.decoder.hevc"
#define SQ2_WINDOW_LABEL "HEVC"
#else
#define SQ2_WINDOW_CODEC "OMX.qcom.video.decoder.avc"
#define SQ2_WINDOW_LABEL "AVC"
#endif
JNIEXPORT jstring JNICALL Java_Sq2NativeVideoWindow_codecLabel(JNIEnv *env,jclass type) {
    (void)type;return (*env)->NewStringUTF(env,SQ2_WINDOW_LABEL);
}
JNIEXPORT jint JNICALL Java_Sq2NativeVideoWindow_decode(JNIEnv *env,jclass type,jobject surface) {
    (void)type;
    ANativeWindow *window=ANativeWindow_fromSurface(env,surface);
    if(!window)return 2;
#if defined(SQ2_WINDOW_FULL_AU) || defined(SQ2_HEVC_VECTOR)
    char *args[]={"sq2-native-video-window",SQ2_WINDOW_CODEC,"--surface-full-au"};
#else
    char *args[]={"sq2-native-video-window",SQ2_WINDOW_CODEC,"--surface-csd"};
#endif
    int result=sq2_mediacodec_frames_run(3,args,window);
#ifdef SQ2_WINDOW_DATASPACE_TRACE
    printf("NATIVE_VIDEO_WINDOW_DATASPACE value=0x%x\n",ANativeWindow_getBuffersDataSpace(window));
#endif
    ANativeWindow_release(window);
    printf("NATIVE_VIDEO_WINDOW_RELEASED status=%d\n",result);
    return result;
}
