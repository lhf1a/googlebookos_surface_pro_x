"""ASan/UBSan tests of the real presentation scheduler; no codec/hardware claims."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
C_SOURCE = r"""
#include "sq2-window-playback.h"
#include <assert.h>
#include <stdlib.h>

static void fixture(uint64_t *pts) {
    const unsigned order[] = {0, 3, 1, 2, 5, 4};
    for (unsigned i = 0; i < SQ2_WINDOW_FRAMES; ++i)
        pts[i] = 123 + (i / 6 * 6 + order[i % 6]) * 33333;
}
static void initialize(struct sq2_window_playback *p) {
    uint64_t pts[SQ2_WINDOW_FRAMES]; fixture(pts);
    assert(sq2_window_init(p, pts, SQ2_WINDOW_FRAMES));
}
static void schedule(struct sq2_window_playback *p) {
    for (unsigned i = 0; i < SQ2_WINDOW_FRAMES; ++i) {
        int64_t target = 0;
        assert(sq2_window_target(p, i, p->pts[i], 1000000000 + i, &target));
        assert(target == 1050000000 + (int64_t)i * 33333000);
    }
}
int main(int argc, char **argv) {
    assert(argc == 2);
    int test = atoi(argv[1]);
    struct sq2_window_playback p;
    uint64_t pts[SQ2_WINDOW_FRAMES]; fixture(pts);
    int64_t target = 0;
    initialize(&p);
    if (test == 1) {
        for (unsigned i = 0; i < SQ2_WINDOW_FRAMES; ++i)
            assert(p.pts[i] == 123 + (int64_t)i * 33333);
        schedule(&p); /* All 30 releases can be scheduled without a consumer callback. */
        assert(p.callbacks == 0 && !p.invalid_callback);
        for (unsigned i = SQ2_WINDOW_FRAMES; i-- > 0;)
            assert(sq2_window_callback(&p, p.pts[i], p.target[i] + 500, 2000000000));
        assert(p.callbacks == SQ2_WINDOW_FRAMES && !p.invalid_callback);
        for (unsigned i = 0; i < SQ2_WINDOW_FRAMES; ++i) {
            assert(p.callback_time[i] == p.target[i] + 500);
            assert(p.callback_arrival[i] == 2000000000);
        }
    } else if (test == 2) {
        assert(!sq2_window_init(NULL, pts, SQ2_WINDOW_FRAMES));
        assert(!sq2_window_init(&p, NULL, SQ2_WINDOW_FRAMES));
        assert(!sq2_window_init(&p, pts, 0));
        assert(!sq2_window_init(&p, pts, SQ2_WINDOW_FRAMES - 1));
        pts[4] = pts[0];
        assert(!sq2_window_init(&p, pts, SQ2_WINDOW_FRAMES));
        fixture(pts); pts[29] = (uint64_t)INT64_MAX / 1000 + 1;
        assert(!sq2_window_init(&p, pts, SQ2_WINDOW_FRAMES));
    } else if (test == 3) {
        assert(!sq2_window_target(&p, 1, p.pts[1], 1, &target));
        assert(!sq2_window_target(&p, 0, p.pts[1], 1, &target));
        assert(!sq2_window_target(&p, 0, p.pts[0], 0, &target));
        assert(!sq2_window_target(&p, 0, p.pts[0], -1, &target));
        assert(!sq2_window_target(NULL, 0, 0, 1, &target));
        assert(!sq2_window_target(&p, 0, p.pts[0], 1, NULL));
        assert(!sq2_window_target(&p, SQ2_WINDOW_FRAMES, 0, 1, &target));
        assert(sq2_window_target(&p, 0, p.pts[0], 1, &target));
        assert(!sq2_window_target(&p, 0, p.pts[0], 1, &target));
        assert(!sq2_window_target(&p, 2, p.pts[2], 1, &target));
        assert(sq2_window_target(&p, 1, p.pts[1], 1, &target));
    } else if (test == 4) {
        assert(!sq2_window_target(&p, 0, p.pts[0], INT64_MAX - 49999999, &target));
        assert(sq2_window_target(&p, 0, p.pts[0], INT64_MAX - 50000000, &target));
        assert(target == INT64_MAX);
        assert(!sq2_window_target(&p, 1, p.pts[1], 1, &target));
        /* Very large absolute PTS are valid when their deltas fit. */
        for (unsigned i = 0; i < SQ2_WINDOW_FRAMES; ++i)
            pts[i] = (uint64_t)INT64_MAX / 1000 - 29 + i;
        assert(sq2_window_init(&p, pts, SQ2_WINDOW_FRAMES));
        for (unsigned i = 0; i < SQ2_WINDOW_FRAMES; ++i) {
            assert(sq2_window_target(&p, i, p.pts[i], 1, &target));
            assert(target == 50000001 + (int64_t)i * 1000);
        }
    } else if (test == 5) {
        assert(!sq2_window_callback(&p, p.pts[0], 1, 2000000000));
        assert(p.invalid_callback && !p.callbacks);
        initialize(&p); schedule(&p);
        assert(!sq2_window_callback(&p, -1, 1, 2000000000));
        assert(p.invalid_callback && !p.callbacks);
        initialize(&p); schedule(&p);
        assert(!sq2_window_callback(&p, p.pts[0], 0, 2000000000));
        assert(p.invalid_callback && !p.callbacks);
        initialize(&p); schedule(&p);
        assert(!sq2_window_callback(&p, p.pts[0], -1, 2000000000));
        assert(p.invalid_callback && !p.callbacks);
        assert(!sq2_window_callback(NULL, 1, 1, 2000000000));
    } else if (test == 6) {
        schedule(&p);
        for (unsigned i = 0; i < SQ2_WINDOW_FRAMES - 1; ++i)
            assert(sq2_window_callback(&p, p.pts[i], p.target[i], 2000000000));
        assert(p.callbacks == 29 && !p.invalid_callback);
        assert(!sq2_window_callback(&p, p.pts[0], p.target[0] + 1, 2000000000));
        assert(p.callbacks == 29 && p.invalid_callback);
        assert(sq2_window_callback(&p, p.pts[29], p.target[29], 2000000000));
        assert(p.callbacks == 30 && p.invalid_callback); /* Failure stays latched. */
    } else if (test == 7) {
        /* Nonuniform presentation times must not be replaced by a fixed cadence. */
        for (unsigned i = 0; i < SQ2_WINDOW_FRAMES; ++i)
            pts[i] = 8000 + (uint64_t)i * i * 1234;
        assert(sq2_window_init(&p, pts, SQ2_WINDOW_FRAMES));
        for (unsigned i = 0; i < SQ2_WINDOW_FRAMES; ++i) {
            assert(sq2_window_target(&p, i, p.pts[i], 1000000000, &target));
            assert(target == 1050000000 + (int64_t)i * i * 1234000);
        }
    } else assert(0);
    return 0;
}
"""


class WindowPlaybackTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.build.cleanup)
        source = Path(cls.build.name) / "test.c"
        source.write_text(C_SOURCE)
        cls.binary = Path(cls.build.name) / "test"
        subprocess.run(["cc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                        "-I", str(ROOT / "probe/video"), str(source), "-o", str(cls.binary)],
                       check=True)

    def run_case(self, number):
        result = subprocess.run([str(self.binary), str(number)],
                                capture_output=True, text=True, timeout=3)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_b_frame_order_and_thirty_releases_without_callbacks(self):
        self.run_case(1)

    def test_bad_fixture_bounds_and_duplicate_pts(self):
        self.run_case(2)

    def test_unexpected_duplicate_and_out_of_order_outputs(self):
        self.run_case(3)

    def test_clock_overflow_and_large_pts(self):
        self.run_case(4)

    def test_unknown_unsubmitted_and_invalid_callbacks(self):
        self.run_case(5)

    def test_missing_duplicate_callback_and_latched_failure(self):
        self.run_case(6)

    def test_real_nonuniform_pts_deltas(self):
        self.run_case(7)


if __name__ == "__main__":
    unittest.main()
