#pragma once

#include <DesignSystem/Core/TokenType.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace DesignSystem {

/**
 * Per-token value restriction.
 *
 * A token of type Float or Int can declare a constraint that the override
 * UI uses to render the right widget (slider with min/max, drag with step,
 * one-of dropdown for discrete sets) and that OverrideManager uses to clamp
 * or reject an incoming value.
 *
 * The constraint is intentionally simple: it covers what the spec mentions
 * (positive-only float, int in [-32, 32] excluding zero, intervals, allowed
 * value sets, step). For Color and Vec2 it is a no-op today; sub-channel
 * constraints (per-component range, alpha-only restrictions, …) belong here
 * later but no existing token needs them.
 */
struct ValueConstraint {
    /// Half-open intervals [min, max] that the value must fall in. Empty
    /// vector means "no interval restriction".
    struct Interval {
        double min;
        double max;
        bool includesMin;
        bool includesMax;
    };

    std::vector<Interval> intervals;

    /// Discrete set of allowed values. If non-empty the value must match one
    /// of these exactly (intervals are ignored when this is set).
    std::vector<double> allowedValues;

    /// Step / quantisation for sliders and drag widgets. 0 = continuous.
    double step = 0.0;

    /// Optional human-readable summary shown next to the field.
    std::string description;

    // ── Factories ────────────────────────────────────────────────────────────

    static ValueConstraint Range(double lo, double hi, double step = 0.0,
                                 std::string description = {}) {
        ValueConstraint c;
        c.intervals.push_back({lo, hi, true, true});
        c.step = step;
        c.description = std::move(description);
        return c;
    }

    static ValueConstraint PositiveFloat(double max = 1e6) {
        return Range(0.0, max, 0.0, "positive");
    }

    static ValueConstraint AlphaRange() {
        return Range(0.0, 1.0, 0.0, "alpha in [0..1]");
    }

    static ValueConstraint OneOf(std::vector<double> values,
                                 std::string description = {}) {
        ValueConstraint c;
        c.allowedValues = std::move(values);
        c.description = std::move(description);
        return c;
    }

    // ── Predicates ───────────────────────────────────────────────────────────

    bool IsEmpty() const {
        return intervals.empty() && allowedValues.empty();
    }

    /// Lowest finite lower bound, useful for slider widgets.
    /// Returns std::nullopt if the constraint is open on the low side.
    std::optional<double> Min() const {
        if (!allowedValues.empty())
            return *std::min_element(allowedValues.begin(), allowedValues.end());
        if (intervals.empty()) return std::nullopt;
        double m = intervals.front().min;
        for (const auto& i : intervals) m = std::min(m, i.min);
        return m;
    }

    std::optional<double> Max() const {
        if (!allowedValues.empty())
            return *std::max_element(allowedValues.begin(), allowedValues.end());
        if (intervals.empty()) return std::nullopt;
        double m = intervals.front().max;
        for (const auto& i : intervals) m = std::max(m, i.max);
        return m;
    }

    bool Accepts(double v) const {
        if (!allowedValues.empty()) {
            for (double a : allowedValues)
                if (a == v) return true;
            return false;
        }
        if (intervals.empty()) return true;
        for (const auto& i : intervals) {
            bool lowOk = i.includesMin ? v >= i.min : v > i.min;
            bool hiOk  = i.includesMax ? v <= i.max : v < i.max;
            if (lowOk && hiOk) return true;
        }
        return false;
    }

    /// Snap a value to the nearest acceptable one.
    /// - one-of: nearest allowed value.
    /// - intervals: clamp into the closest interval.
    /// - step: snap to the grid relative to the lowest min.
    double Clamp(double v) const {
        if (!allowedValues.empty()) {
            double best = allowedValues.front();
            double bestDist = std::numeric_limits<double>::infinity();
            for (double a : allowedValues) {
                double d = std::abs(a - v);
                if (d < bestDist) { bestDist = d; best = a; }
            }
            return best;
        }
        if (intervals.empty()) {
            return ApplyStep(v);
        }
        // Pick the closest interval to v, then clamp into it.
        const Interval* best = &intervals.front();
        double bestDist = std::numeric_limits<double>::infinity();
        for (const auto& i : intervals) {
            double d = (v < i.min) ? (i.min - v)
                     : (v > i.max) ? (v - i.max)
                                   : 0.0;
            if (d < bestDist) { bestDist = d; best = &i; }
        }
        double c = std::clamp(v, best->min, best->max);
        return ApplyStep(c);
    }

private:
    double ApplyStep(double v) const {
        if (step <= 0.0) return v;
        auto lo = Min();
        double origin = lo.value_or(0.0);
        double n = std::round((v - origin) / step);
        return origin + n * step;
    }
};

} // namespace DesignSystem
