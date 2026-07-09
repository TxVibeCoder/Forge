/*
    TapTempo — a pure, engine-free tap-tempo estimator behind the tempo popup (W?? 1.4).

    Follows the pure-model style of LcdModel.h: plain doubles in (millisecond timestamps),
    a std::optional<double> BPM out, no JUCE component / no te:: types — so the selftest gate
    drives it headlessly with synthetic timestamps (four taps 500 ms apart -> 120.0 BPM; a
    >2000 ms gap starts a fresh sequence).

    Keeps the last up-to-4 tap timestamps and averages the adjacent intervals; currentBpm()
    is nullopt until at least three taps exist, so the first reported estimate already averages
    two intervals rather than reporting a single-interval guess. Deterministic — no clock reads
    of its own; the caller supplies the timestamp (the real UI passes
    juce::Time::getMillisecondCounterHiRes()).

    Header-only. Not thread-safe: message-thread use only (the popup owns one instance).
*/

#pragma once

#include <array>
#include <optional>

namespace forge::transport
{

class TapTempo
{
public:
    /** Records a tap at `nowMs`. A gap of more than 2000 ms from the previous tap is treated
        as the start of a new tapping sequence (the stale buffer is cleared first). */
    void tap (double nowMs)
    {
        if (count > 0 && (nowMs - taps[(head + capacity - 1) % capacity]) > gapMs)
            reset();

        taps[head] = nowMs;
        head = (head + 1) % capacity;

        if (count < capacity)
            ++count;
    }

    /** Mean-interval BPM across the buffered adjacent taps, clamped to [20, 300]; nullopt
        until at least three taps have been recorded (so the estimate always averages at
        least two intervals, never a single-interval guess). */
    std::optional<double> currentBpm() const
    {
        if (count < 3)
            return std::nullopt;

        // Sum the adjacent intervals in chronological order. The buffer is a ring of `count`
        // entries whose oldest sits at (head - count); walking forward gives increasing ts.
        const int oldest = (head + capacity - count) % capacity;

        double totalMs = 0.0;
        for (int i = 1; i < count; ++i)
        {
            const double prev = taps[(oldest + i - 1) % capacity];
            const double curr = taps[(oldest + i)     % capacity];
            totalMs += (curr - prev);
        }

        const double meanIntervalMs = totalMs / (double) (count - 1);

        if (meanIntervalMs <= 0.0)
            return std::nullopt;   // degenerate (identical timestamps) — no meaningful tempo

        const double bpm = 60000.0 / meanIntervalMs;
        return clamp (bpm, 20.0, 300.0);
    }

    /** Discards all buffered taps (also invoked implicitly on a >2000 ms gap). */
    void reset()
    {
        head = 0;
        count = 0;
    }

private:
    static constexpr int    capacity = 4;        // last up-to-4 tap timestamps
    static constexpr double gapMs    = 2000.0;   // idle gap that starts a fresh sequence

    static double clamp (double v, double lo, double hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    std::array<double, capacity> taps { { 0.0, 0.0, 0.0, 0.0 } };
    int head  = 0;   // next write slot (ring buffer)
    int count = 0;   // number of valid entries (0..capacity)
};

} // namespace forge::transport
