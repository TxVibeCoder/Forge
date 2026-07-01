/*
    LoudnessAnalyzer — offline integrated-loudness (LUFS) + true-peak analyzer per ITU-R BS.1770-4.

    WHY OFFLINE. Live master-bus LUFS is not reachable without forking the read-only
    tracktion_engine submodule (te::LevelMeasurer exposes only reduced dB, not sample buffers; the
    master-tap node is inserted internally with no app hook; JUCE's AudioDeviceManager sums secondary
    callbacks rather than letting them observe engine output). Integrated loudness is inherently a
    whole-program measurement, so measuring OFFLINE on the rendered WAV — which already carries the
    full master chain post-fader — is the correct tool, not a compromise.

    SELF-CONTAINED. This class has NO tracktion dependency: you feed it raw float buffers (the
    selftest feeds a synthesized tone; the Exporter feeds the rendered file via analyzeFile). It is
    an OFFLINE analyzer — normal heap allocation in prepare() is fine; it is NEVER on the audio/RT
    thread and must not be used there.

    ALGORITHM (BS.1770-4).
      - K-weighting = two biquads in series applied per channel:
          stage 1 — a high-shelf (~+4 dB above ~1.5 kHz),
          stage 2 — an RLB high-pass (~ -3 dB at ~38 Hz).
        Canonical 48 kHz coefficients are taken from the standard; other sample rates are
        re-derived by the standard bilinear transform (see the .cpp for the exact source + math).
      - Gating: per-channel mean-square is accumulated over 400 ms blocks with 75 % overlap
        (100 ms step). Channel-weighted sum uses G = 1.0 for L and R (stereo). A block's loudness is
          L_k = -0.691 + 10*log10( sum_ch G_ch * meanSquare_ch ).
      - Integrated loudness: an ABSOLUTE gate discards blocks below -70 LUFS; a RELATIVE gate then
        discards blocks more than 10 LU below the mean loudness of the absolute-gated survivors; the
        integrated value is the loudness of the mean-square average of the relative-gated survivors.
      - True peak: 4x oversampled peak (a short polyphase FIR interpolator) → dBTP.

    Message/worker-thread only; not thread-safe against concurrent processBlock calls.
*/

#pragma once

#include <JuceHeader.h>

#include <array>
#include <limits>
#include <vector>

namespace forge::dsp
{

class LoudnessAnalyzer
{
public:
    LoudnessAnalyzer() = default;

    //==============================================================================
    /** Sentinel returned when there is no gated program material (silence / too short). Declared
        before Result so Result's default member initialisers can reference it (a nested class's
        initialisers cannot forward-reference an enclosing-class member declared later on MSVC). */
    static constexpr float kSilenceLufs = -std::numeric_limits<float>::infinity();

    /** Taps per phase of the 4x true-peak oversampling FIR. Public so the coefficient table in the
        .cpp can static_assert it stays in lock-step with the per-channel history ring. */
    static constexpr int kTpTaps = 12;

    //==============================================================================
    /** The measured result. Fields are LUFS / LU / dBTP as noted.

        integratedLufs is the required deliverable. When too little audio was fed to form even one
        gating block (< 400 ms) or every block fell below the absolute gate (i.e. effective
        silence), integratedLufs is a sentinel of -infinity (kSilenceLufs). */
    struct Result
    {
        float integratedLufs = kSilenceLufs;   // BS.1770-4 integrated (gated) loudness, LUFS
        float truePeakDb      = -144.0f;        // 4x-oversampled true peak, dBTP
        float momentaryLufs   = kSilenceLufs;   // loudest 400 ms window (ungated), LUFS
        float shortTermLufs   = kSilenceLufs;   // loudest 3 s window (ungated), LUFS
    };

    //==============================================================================
    /** Allocates weighting-filter state for numChannels channels at sampleRate and clears all
        accumulators. Call once before feeding processBlock. numChannels >= 1; sampleRate > 0. */
    void prepare (double sampleRate, int numChannels);

    /** Feeds one block of de-interleaved audio. channels is an array of numChannels pointers, each
        numSamples long. Safe to call repeatedly until the whole program has been fed. If numChannels
        exceeds the prepared channel count the extra channels are ignored; if it is fewer, the missing
        channels contribute silence. */
    void processBlock (const float* const* channels, int numChannels, int numSamples);

    /** Finalises the gating and returns the measurement. Const: repeated calls yield the same value
        and do not disturb the accumulated state. */
    Result getResult() const;

    /** Clears all state so the object can be reused (keeps the prepared sample rate / channel
        count and filter coefficients — call prepare() again to change those). */
    void reset();

    //==============================================================================
    /** Convenience: reads a WAV (or any format JUCE can decode) fully off disk and returns its
        integrated loudness + true peak. Reads in chunks so a long file doesn't need to be resident
        all at once. On any read failure returns a default Result (kSilenceLufs) and logs a warning.
        Message/worker-thread; performs blocking file IO — never call on the audio/RT thread. */
    static Result analyzeFile (const juce::File& wav);

private:
    //==============================================================================
    // A single biquad (Transposed Direct Form II) with per-channel state. TDF-II keeps two state
    // words per channel (z1, z2) and is the numerically well-behaved form for these coefficients.
    struct Biquad
    {
        // Coefficients (a0 normalised to 1).
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

        // Per-channel delay lines (z1, z2), sized to the channel count in prepare().
        std::vector<double> z1, z2;

        void prepareChannels (int numChannels);
        void reset();
        // Processes one sample for channel ch, returns the filtered sample.
        forcedinline double processSample (double x, int ch) noexcept
        {
            const double y = b0 * x + z1[(size_t) ch];
            z1[(size_t) ch] = b1 * x - a1 * y + z2[(size_t) ch];
            z2[(size_t) ch] = b2 * x - a2 * y;
            return y;
        }
    };

    // Fills stage1 (high-shelf) + stage2 (RLB high-pass) with BS.1770-4 coefficients for sr.
    void computeKWeightingCoeffs (double sr);

    double sampleRate = 48000.0;
    int    channels   = 0;

    Biquad stage1, stage2;   // K-weighting: pre-filter (high-shelf) then RLB (high-pass)

    // --- Gating-block accumulation (100 ms sub-blocks; a 400 ms block = 4 sub-blocks) ---
    // Per sample we accumulate the channel-weighted sum of K-weighted squares, i.e.
    // sum_ch (G_ch * y_ch^2). Because the channel weights are constant, a 400 ms block's
    // channel-weighted mean-square equals the mean over its 4 sub-blocks of these per-sample sums,
    // so a single scalar accumulator per sub-block is exact (see the algorithm note in the .cpp).
    int    subBlockLenSamples = 0;   // samples per 100 ms sub-block
    int    subBlockPos        = 0;   // sample index within the current sub-block
    double subBlockSumSq      = 0.0; // running sum of channel-weighted K-weighted squares this sub-block

    // Completed 100 ms sub-block *mean* squares (weighted sum-of-squares divided by sub-block length).
    // We keep the whole list so the integrated two-pass gating can revisit every overlapping 400 ms
    // block; memory is tiny (10 doubles per second of audio).
    std::vector<double> subBlockMeanSq;

    // True-peak tracking: the max of the 4x-oversampled |x| over all channels, tracked in linear.
    // A per-channel ring of the most recent input samples (kTpTaps long) feeds a 4-phase polyphase
    // FIR; the coefficient table lives in the .cpp.
    double truePeakLinear = 0.0;
    std::vector<std::array<float, kTpTaps>> tpHistory;   // [channel] -> newest-first sample ring

    void updateTruePeak (const float* const* channels, int numChannels, int numSamples);
    void pushSubBlock();   // finalises the current 100 ms sub-block into subBlockMeanSq

    JUCE_LEAK_DETECTOR (LoudnessAnalyzer)
};

} // namespace forge::dsp
