#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct quest2_omx_session;
/* Pixels are borrowed only for the duration of the synchronous callback. */
struct quest2_omx_frame {
    const uint8_t *data;
    size_t bytes;
    uint32_t width, height, stride, slice;
    uint64_t token;
};
typedef int (*quest2_omx_deliver)(void *, const struct quest2_omx_frame *);
int quest2_omx_open(struct quest2_omx_session **, const char *library,
                    unsigned width, unsigned height, quest2_omx_deliver, void *);
int quest2_omx_submit(struct quest2_omx_session *, const uint8_t *, size_t, uint64_t);
int quest2_omx_pump(struct quest2_omx_session *, unsigned timeout_ms);
int quest2_omx_drain(struct quest2_omx_session *);
int quest2_omx_ended(const struct quest2_omx_session *);
int quest2_omx_close(struct quest2_omx_session *);
#ifdef __cplusplus
}
#endif
