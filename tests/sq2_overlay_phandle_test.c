#include <assert.h>
#include <string.h>
#include "sq2-overlay-phandle.h"
int main(void) {
 unsigned offsets[]={0,4,8};
 unsigned char original[]={0,0,0,0xb5,0,0,0,0xb5,0,0,0,0xb5,0xaa,0xbb,0xcc,0xdd};
 unsigned char blob[sizeof(original)];
 memcpy(blob,original,sizeof(blob));
 assert(!sq2_overlay_rebind(blob,sizeof(blob),offsets,3,0xb5,0xb9));
 assert(blob[3]==0xb9 && blob[7]==0xb9 && blob[11]==0xb9);
 assert(!memcmp(blob+12,original+12,4));
 memcpy(blob,original,sizeof(blob));
 assert(!sq2_overlay_rebind(blob,sizeof(blob),offsets,3,0xb5,0xb5));
 assert(!memcmp(blob,original,sizeof(blob)));
 offsets[2]=16;
 assert(sq2_overlay_rebind(blob,sizeof(blob),offsets,3,0xb5,0xb9));
 assert(!memcmp(blob,original,sizeof(blob)));
 offsets[2]=4;
 assert(sq2_overlay_rebind(blob,sizeof(blob),offsets,3,0xb5,0xb9));
 offsets[2]=9;
 assert(sq2_overlay_rebind(blob,sizeof(blob),offsets,3,0xb5,0xb9));
 offsets[2]=8;
 blob[11]=0xb6;
 unsigned char bad[sizeof(blob)];memcpy(bad,blob,sizeof(bad));
 assert(sq2_overlay_rebind(blob,sizeof(blob),offsets,3,0xb5,0xb9));
 assert(!memcmp(blob,bad,sizeof(blob)));
 assert(sq2_overlay_rebind(blob,sizeof(blob),offsets,3,0xb5,0));
 assert(sq2_overlay_rebind(blob,sizeof(blob),offsets,3,0xb5,0xffffffffu));
 assert(sq2_overlay_rebind(blob,sizeof(blob),offsets,0,0xb5,0xb9));
 return 0;
}
