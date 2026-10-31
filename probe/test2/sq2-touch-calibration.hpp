// SPDX-License-Identifier: GPL-2.0-or-later
// Optional spatial calibration. Applied only when emitting SQ2 finger coordinates.
#pragma once
#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace iptsd::apps::daemon {
class SQ2CalibrationCurve {
    std::vector<std::pair<double, double>> points;
public:
    void load(const std::string &text) {
        std::vector<std::pair<double, double>> parsed;
        std::istringstream input(text);
        std::string token;
        while (input >> token) {
            std::istringstream pair(token);
            double from, to; char separator, extra;
            if (!(pair >> from >> separator >> to) || separator != ':' || (pair >> extra) ||
                !std::isfinite(from) || !std::isfinite(to) ||
                from < 0 || from > 1 || to < 0 || to > 1)
                throw std::invalid_argument("Invalid SQ2 calibration point");
            if (!parsed.empty()) {
                const double dx = from - parsed.back().first;
                const double dy = to - parsed.back().second;
                if (dx <= 0 || dy <= 0 || dy / dx < 0.2 || dy / dx > 6)
                    throw std::invalid_argument("SQ2 calibration must be monotonic with slope 0.2..6");
            }
            parsed.emplace_back(from, to);
            if (parsed.size() > 16)
                throw std::invalid_argument("Too many SQ2 calibration points");
        }
        if (!parsed.empty() && (parsed.size() < 2 || parsed.front() != std::pair<double,double>{0,0} ||
                                parsed.back() != std::pair<double,double>{1,1}))
            throw std::invalid_argument("SQ2 calibration must preserve endpoints 0:0 and 1:1");
        points = std::move(parsed);
    }
    double map(double value) const {
        if (points.empty() || !std::isfinite(value)) return value;
        if (value <= 0) return 0;
        if (value >= 1) return 1;
        const auto hi = std::upper_bound(points.begin(), points.end(), value,
            [](double x, const auto &p) { return x < p.first; });
        const auto lo = hi - 1;
        return lo->second + (value - lo->first) * (hi->second - lo->second) / (hi->first - lo->first);
    }
};

class SQ2TouchCalibration {
    SQ2CalibrationCurve x_curve, y_curve, top_x_curve;
    bool has_top = false;
    double top_start = .1, top_end = .3;
public:
    void load(const std::string &x, const std::string &y, const std::string &top = "",
              double start = .1, double end = .3) {
        SQ2CalibrationCurve nx, ny, nt;
        nx.load(x); ny.load(y); nt.load(top);
        if (!top.empty() && (!std::isfinite(start) || !std::isfinite(end) || start < 0 || end > 1 || start >= end))
            throw std::invalid_argument("Invalid SQ2 top calibration blend interval");
        x_curve = std::move(nx); y_curve = std::move(ny); top_x_curve = std::move(nt);
        has_top = !top.empty(); top_start = start; top_end = end;
    }
    std::pair<double,double> map(double x, double y) const {
        double mapped_x = x_curve.map(x);
        if (has_top && std::isfinite(y) && y < top_end) {
            const double weight = std::clamp((y - top_start) / (top_end - top_start), 0.0, 1.0);
            mapped_x = top_x_curve.map(x) * (1 - weight) + mapped_x * weight;
        }
        return {mapped_x, y_curve.map(y)};
    }
};
}
