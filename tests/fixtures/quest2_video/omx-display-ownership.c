/* Exercise actual diagnostic ownership code; HWC/OMX are explicit host fakes. */
#define QUEST2_NATIVE_OUTPUT 1
#define QUEST2_NATIVE_HWC 1
#define QUEST2_OMX_SESSION_ONLY 1
#include "../../../probe/video/quest2-omx-decode.c"
#include <assert.h>
static int queued,filled,freed,finished,flush_slot=-1,flush_error,fill_error;
int quest2_capture_queue(unsigned slot) { assert(slot==0);++queued;return 0; }
int quest2_capture_display_flush(int *slot) { *slot=flush_slot;return flush_error; }
void quest2_capture_finish(void) { ++finished; }
static OMX_ERRORTYPE fill(OMX_HANDLETYPE handle,OMX_BUFFERHEADERTYPE *b) {
    (void)handle;assert(b);++filled;return fill_error ? OMX_ErrorHardware : OMX_ErrorNone;
}
static OMX_ERRORTYPE release(OMX_HANDLETYPE handle,OMX_U32 port,OMX_BUFFERHEADERTYPE *b) {
    (void)handle;assert(port==1 && b);++freed;return OMX_ErrorNone;
}
static void pool(struct client *c,OMX_BUFFERHEADERTYPE *header,enum ownership owner) {
    c->output.slots=calloc(1,sizeof(struct slot));assert(c->output.slots);
    c->output.count=1;c->output.definition.nPortIndex=1;
    c->output.slots[0]=(struct slot){.header=header,.owner=owner};
}
int main(void) {
    OMX_COMPONENTTYPE component={.FillThisBuffer=fill,.FreeBuffer=release};
    struct client c={.component=&component,.native_output=true};
    assert(!pthread_mutex_init(&c.mutex,NULL));
    OMX_BUFFERHEADERTYPE header={0};
    pool(&c,&header,DISPLAY);
    assert(!refill(&c,c.output.slots) && !queued && !filled);
    c.output.slots[0].owner=COMPONENT;
    assert(!refill(&c,c.output.slots) && !queued && !filled);
    c.output.slots[0].owner=READY;
    assert(!refill(&c,c.output.slots) && !queued && !filled);
    struct slot unrelated={.header=&header,.owner=APP};
    assert(!refill(&c,&unrelated) && !queued && !filled);
    c.output.slots[0].owner=APP;fill_error=1;
    assert(!refill(&c,c.output.slots) && queued==1 && filled==1 && c.output.slots[0].owner==APP);
    fill_error=0;
    assert(refill(&c,c.output.slots) && queued==2 && filled==2 && c.output.slots[0].owner==COMPONENT);
    assert(!free_port(&c,&c.output) && !freed && !finished && c.output.slots);
    c.output.slots[0].owner=DISPLAY;
    assert(!free_port(&c,&c.output) && !freed && !finished);
    flush_slot=0;flush_error=1;
    assert(!free_port(&c,&c.output) && !freed && !finished && c.output.slots[0].owner==DISPLAY);
    flush_error=0;flush_slot=1;
    assert(!free_port(&c,&c.output) && !freed && !finished && c.output.slots[0].owner==DISPLAY);
    flush_slot=0;
    assert(free_port(&c,&c.output) && freed==1 && finished==1 && !c.output.slots && !c.output.count);
    pool(&c,&header,APP);flush_slot=0;
    assert(!free_port(&c,&c.output) && freed==1 && c.output.slots[0].owner==APP);
    flush_slot=-1;
    assert(free_port(&c,&c.output) && freed==2 && finished==2);
    pthread_mutex_destroy(&c.mutex);
    puts("DISPLAY_OWNERSHIP: held/foreign buffers refuse refill; component/unsignaled/invalid releases refuse free; released buffer frees exactly once");
    return 0;
}
