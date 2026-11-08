package org.surface.sq2.videotest;

import java.util.Arrays;
import java.util.Collections;
import java.util.List;

public final class RenderedFrameAuditTest {
    private static void has(String text, String expected) {
        if (!text.contains(expected)) throw new AssertionError(text + " lacks " + expected);
    }
    public static void main(String[] args) {
        List<Long> queued = Arrays.asList(0L, 33333L, 66666L, 99999L);
        String complete = RenderedFrameAudit.summarize(queued, queued);
        has(complete, "missing=0 missing_tail_only=false");
        String tail = RenderedFrameAudit.summarize(queued, Arrays.asList(0L, 33333L));
        has(tail, "missing=2 missing_tail_only=true");
        has(tail, "missing_pts_us=[66666, 99999]");
        String interior = RenderedFrameAudit.summarize(queued, Arrays.asList(0L, 66666L));
        has(interior, "missing=2 missing_tail_only=false");
        has(interior, "missing_pts_us=[33333, 99999]");
        String duplicates = RenderedFrameAudit.summarize(queued, Arrays.asList(0L, 0L, 123L));
        has(duplicates, "callbacks=3 unique_callbacks=2 duplicates=1");
        has(duplicates, "unexpected_pts_us=[123]");
        String none = RenderedFrameAudit.summarize(queued, Collections.emptyList());
        has(none, "missing=4 missing_tail_only=true");
        String empty = RenderedFrameAudit.summarize(Collections.emptyList(), Collections.emptyList());
        has(empty, "missing=0 missing_tail_only=false");
        System.out.println("RENDER_AUDIT_PASS: complete/tail/interior/duplicate/unexpected/empty");
    }
}
