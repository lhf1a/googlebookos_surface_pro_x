// SPDX-License-Identifier: GPL-2.0-only
#include "spx-capture.h"
#include <cstdio>
#include <exception>
int main() {
    try {
        for (bool front : {true, false, true}) {
            spx::Capture capture(front, front ? 1296 : 2112, front ? 972 : 1568);
            uint64_t previous = 0;
            for (unsigned i = 0; i < 5; ++i) {
                auto f = capture.next();
                if (i && f.timestampNs <= previous) return 2;
                previous = f.timestampNs;
                unsigned sum = 0;
                for (auto v : f.raw10) sum += v;
                printf("%s %ux%u sequence=%u timestamp=%llu bytes=%zu sum=%u\n",
                       front ? "front" : "rear", f.width, f.height, f.sequence,
                       (unsigned long long)f.timestampNs, f.raw10.size(), sum);
            }
        }
    } catch (const std::exception& e) {
        fprintf(stderr, "%s\n", e.what()); return 1;
    }
}
