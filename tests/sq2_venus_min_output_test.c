#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
typedef uint32_t u32;
enum hfi_version { V1=1,V4=4 };
#define INST_UNINIT 0
#define HFI_BUFFER_INPUT 1
struct res { enum hfi_version hfi_version; };
struct core { struct res *res; };
struct venus_inst { struct core *core; int state; };
struct v4l2_ctrl { int val; long long maximum; };
struct hfi_buffer_requirements { u32 count_min, count_min_host; };
#define HFI_BUFREQ_COUNT_MIN(r,v) ((v)==V4?(r)->count_min_host:(r)->count_min)
static int calls,error;static u32 fw_count;
static int venus_helper_get_bufreq(struct venus_inst *i,u32 t,struct hfi_buffer_requirements *r)
{(void)i;assert(t==HFI_BUFFER_INPUT);calls++;r->count_min=fw_count;r->count_min_host=fw_count;return error;}
#include "../probe/video/venus-sq2-min-output.h"
int main(void){struct res r={V4};struct core co={&r};struct venus_inst i={&co,0};struct v4l2_ctrl c={123,INT_MAX};
 assert(venus_sq2_min_output_ctrl(&i,&c)==-EAGAIN&&!calls&&c.val==123);
 i.state=1;error=-ETIMEDOUT;assert(venus_sq2_min_output_ctrl(&i,&c)==-ETIMEDOUT&&c.val==123);
 error=-EIO;assert(venus_sq2_min_output_ctrl(&i,&c)==-EIO&&c.val==123);
 error=0;fw_count=7;assert(!venus_sq2_min_output_ctrl(&i,&c)&&c.val==7);
 fw_count=13;assert(!venus_sq2_min_output_ctrl(&i,&c)&&c.val==13);
 fw_count=UINT32_MAX;assert(venus_sq2_min_output_ctrl(&i,&c)==-ERANGE&&c.val==13);
 puts("MIN_OUTPUT_PASS: uninitialized EAGAIN/no query; real callback counts 7 then13; EIO/timeout/overflow preserved; mock HFI, not hardware proof");return 0;}
