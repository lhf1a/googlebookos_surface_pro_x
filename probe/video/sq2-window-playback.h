/* Strict 30-frame presentation schedule; no ImageReader consumer pacing. */
#ifndef SQ2_WINDOW_PLAYBACK_H
#define SQ2_WINDOW_PLAYBACK_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define SQ2_WINDOW_FRAMES 30
struct sq2_window_playback {
    int64_t pts[SQ2_WINDOW_FRAMES];
    int64_t target[SQ2_WINDOW_FRAMES];
    int64_t callback_time[SQ2_WINDOW_FRAMES];
    int64_t callback_arrival[SQ2_WINDOW_FRAMES];
    int64_t anchor;
    unsigned callbacks;
    bool invalid_callback;
};
static inline bool sq2_window_init(struct sq2_window_playback *p, const uint64_t *pts, size_t count) {
    if (!p || !pts || count != SQ2_WINDOW_FRAMES) return false;
    memset(p, 0, sizeof(*p));
    for (size_t i = 0; i < count; ++i) {
        if (pts[i] > INT64_MAX / 1000) return false;
        p->pts[i] = (int64_t)pts[i];
    }
    for (size_t i = 1; i < count; ++i) {
        int64_t value = p->pts[i]; size_t j = i;
        while (j && p->pts[j - 1] > value) { p->pts[j] = p->pts[j - 1]; --j; }
        p->pts[j] = value;
    }
    for (size_t i = 1; i < count; ++i) if (p->pts[i] == p->pts[i - 1]) return false;
    return true;
}
static inline bool sq2_window_target(struct sq2_window_playback *p, unsigned index,
                                     int64_t pts, int64_t now, int64_t *target) {
    if (!p || !target || index >= SQ2_WINDOW_FRAMES || pts != p->pts[index] ||
        now <= 0 || p->target[index] || (index && !p->target[index - 1])) return false;
    if (!index) {
        if (now > INT64_MAX - 50000000) return false;
        p->anchor = now + 50000000;
    }
    if (!p->anchor) return false;
    int64_t offset = (pts - p->pts[0]) * 1000;
    if (p->anchor > INT64_MAX - offset) return false;
    p->target[index] = p->anchor + offset;
    *target = p->target[index];
    return true;
}
static inline bool sq2_window_callback(struct sq2_window_playback *p, int64_t pts, int64_t callback_ns,
                                        int64_t arrival_ns) {
    if (!p) return false;
    for (unsigned i = 0; i < SQ2_WINDOW_FRAMES; ++i) {
        if (p->pts[i] != pts) continue;
        if (!p->target[i] || p->callback_time[i] || callback_ns <= 0) break;
        p->callback_time[i] = callback_ns;
        p->callback_arrival[i] = arrival_ns; ++p->callbacks;
        return true;
    }
    p->invalid_callback = true;
    return false;
}
#endif
