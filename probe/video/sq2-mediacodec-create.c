#include <media/NdkMediaCodec.h>
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv) {
 const char *name = argc > 1 ? argv[1] : "c2.sq2.avc.experimental";
 setvbuf(stdout, NULL, _IONBF, 0);
 printf("CREATE_BEGIN %s\n", name);
 AMediaCodec *codec = AMediaCodec_createCodecByName(name);
 printf("CREATE_RESULT=%s\n", codec ? "nonnull" : "null");
 if (!codec) return 2;
 if (argc > 2 && !strcmp(argv[2], "--configure")) {
  AMediaFormat *format = AMediaFormat_new();
  AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "video/avc");
  AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, 320);
  AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, 240);
  media_status_t rc = AMediaCodec_configure(codec, format, NULL, NULL, 0);
  AMediaFormat_delete(format);
  printf("CONFIGURE_RESULT=%d\n", rc);
  if (rc == AMEDIA_OK) {
   rc = AMediaCodec_start(codec);
   printf("START_RESULT=%d\n", rc);
   if (rc == AMEDIA_OK) printf("STOP_RESULT=%d\n", AMediaCodec_stop(codec));
  }
  if (rc != AMEDIA_OK) { AMediaCodec_delete(codec); return 3; }
 }
 media_status_t result = AMediaCodec_delete(codec);
 printf("DELETE_RESULT=%d\n", result);
 return result != AMEDIA_OK;
}
