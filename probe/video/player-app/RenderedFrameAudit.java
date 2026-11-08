package org.surface.sq2.videotest;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;

// Callback coverage only: a missing callback does not establish a dropped display frame.
final class RenderedFrameAudit {
    static String summarize(List<Long> queued, List<Long> callbacks) {
        Set<Long> reported = new LinkedHashSet<>(callbacks);
        Set<Long> expected = new HashSet<>(queued);
        List<Long> missing = new ArrayList<>();
        List<Long> unexpected = new ArrayList<>();
        boolean foundMissing = false, tailOnly = true;
        for (long pts : queued) {
            if (!reported.contains(pts)) {
                missing.add(pts);
                foundMissing = true;
            } else if (foundMissing) {
                tailOnly = false;
            }
        }
        for (long pts : reported) if (!expected.contains(pts)) unexpected.add(pts);
        return "RENDER_AUDIT queued=" + queued.size() + " callbacks=" + callbacks.size()
                + " unique_callbacks=" + reported.size()
                + " duplicates=" + (callbacks.size() - reported.size())
                + " missing=" + missing.size() + " missing_tail_only=" + (foundMissing && tailOnly)
                + " missing_pts_us=" + missing + " unexpected_pts_us=" + unexpected;
    }
}
