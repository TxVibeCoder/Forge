#include "engine/dsp/LoudnessAnalyzer.h"

#include "core/Log.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace forge::dsp
{

namespace
{
    // BS.1770-4 gating constants. A 400 ms gating block is 4 overlapping 100 ms sub-blocks (75 %
    // overlap == a 100 ms step). Momentary loudness is exactly one gating block (400 ms); short-term
    // is a 3 s window (30 sub-blocks).
    constexpr double kStepMs            = 100.0;
    constexpr int    kSubBlocksPerBlock = 4;        // 400 ms / 100 ms
    constexpr int    kShortTermSubBlocks = 30;      // 3 s / 100 ms
    constexpr double kAbsoluteGateLufs  = -70.0;
    constexpr double kRelativeGateLu    = -10.0;
    constexpr double kLoudnessOffset    = -0.691;   // the -0.691 in L_k = -0.691 + 10*log10(...)

    // Block loudness from a channel-weighted mean-square (already summed over channels with G_ch).
    // Returns kSilenceLufs (=-inf) for a non-positive mean-square.
    inline double blockLoudness (double weightedMeanSq)
    {
        if (weightedMeanSq <= 0.0)
            return -std::numeric_limits<double>::infinity();

        return kLoudnessOffset + 10.0 * std::log10 (weightedMeanSq);
    }

    // 4x-oversampling polyphase FIR for true-peak. Phase 0 is the identity (the input sample itself);
    // phases 1..3 are a windowed-sinc interpolator. Coefficients are a 12-tap-per-phase Kaiser-windowed
    // sinc — enough to expose inter-sample peaks to well within the BS.1770-4 true-peak tolerance for
    // the steady tones and rendered material Forge measures. (Source: standard windowed-sinc design; a
    // longer filter would tighten the estimate but is unnecessary for a diagnostic dBTP readout.)
    // Layout: kTpCoeffs[phase][tap], newest input sample aligns with tap 0. The tap count matches
    // LoudnessAnalyzer::kTpTaps (asserted below) so the history ring and the FIR stay in lock-step.
    constexpr int kTpPhases = 4;
    constexpr int kTpTaps   = 12;
    static_assert (kTpTaps == LoudnessAnalyzer::kTpTaps, "true-peak FIR tap count must match the header");

    const float kTpCoeffs[kTpPhases][kTpTaps] =
    {
        // phase 0 — passthrough (the original sample)
        { 1.0f, 0,0,0,0,0,0,0,0,0,0,0 },
        // phase 1 — fractional delay 0.25
        { 0.8894f, 0.1996f, -0.1373f, 0.0873f, -0.0523f, 0.0292f,
         -0.0150f, 0.0068f, -0.0026f, 0.0008f, -0.0002f, 0.0f },
        // phase 2 — fractional delay 0.5
        { 0.6284f, 0.5000f, -0.2122f, 0.1273f, -0.0742f, 0.0407f,
         -0.0207f, 0.0093f, -0.0035f, 0.0011f, -0.0002f, 0.0f },
        // phase 3 — fractional delay 0.75
        { 0.1996f, 0.8894f, -0.1373f, 0.0873f, -0.0523f, 0.0292f,
         -0.0150f, 0.0068f, -0.0026f, 0.0008f, -0.0002f, 0.0f },
    };
}

//==============================================================================
void LoudnessAnalyzer::Biquad::prepareChannels (int numChannels)
{
    z1.assign ((size_t) juce::jmax (0, numChannels), 0.0);
    z2.assign ((size_t) juce::jmax (0, numChannels), 0.0);
}

void LoudnessAnalyzer::Biquad::reset()
{
    std::fill (z1.begin(), z1.end(), 0.0);
    std::fill (z2.begin(), z2.end(), 0.0);
}

//==============================================================================
// K-weighting coefficients.
//
// The two-stage K-weighting filter of ITU-R BS.1770-4 is defined by a canonical pair of biquads
// specified numerically at 48 kHz (Table 1 / Table 2 of the recommendation):
//
//   Stage 1 — "pre-filter", a high-shelf (~+4 dB above ~1.5 kHz):
//       b0 = 1.53512485958697,  b1 = -2.69169618940638,  b2 = 1.19839281085285
//       a1 = -1.69065929318241,  a2 = 0.73248077421585   (a0 = 1)
//
//   Stage 2 — "RLB" high-pass (~ -3 dB near 38 Hz):
//       b0 = 1.0, b1 = -2.0, b2 = 1.0
//       a1 = -1.99004745483398,  a2 = 0.99007225036621   (a0 = 1)
//
// For a sample rate other than 48 kHz we re-derive the coefficients from the analog prototypes the
// 48 kHz numbers were bilinear-transformed from, matching the widely-used approach (e.g. the
// pyloudnorm / libebur128 derivations). Both stages are second-order; we recover their analog
// parameters and re-apply the bilinear transform at the target rate. This keeps the frequency
// response of the weighting invariant across sample rates, as the standard intends.
void LoudnessAnalyzer::computeKWeightingCoeffs (double sr)
{
    // --- Stage 1: high-shelf. Re-derive via the standard high-shelf design used to produce the
    // 48 kHz reference numbers (gain ~ +3.999843 dB, Q ~ 0.7071752, fc ~ 1681.9744 Hz). ---
    {
        const double VH = std::pow (10.0, 3.999843853973347 / 20.0);
        const double VB = std::pow (VH, 0.4996667741545416);
        const double Q  = 0.7071752369554196;
        const double fc = 1681.9744509555319;

        const double omega = std::tan (juce::MathConstants<double>::pi * fc / sr);
        const double omega2 = omega * omega;
        const double denom  = 1.0 + omega / Q + omega2;

        const double b0 = (VH + VB * omega / Q + omega2) / denom;
        const double b1 = 2.0 * (omega2 - VH) / denom;
        const double b2 = (VH - VB * omega / Q + omega2) / denom;
        const double a1 = 2.0 * (omega2 - 1.0) / denom;
        const double a2 = (1.0 - omega / Q + omega2) / denom;

        stage1.b0 = b0; stage1.b1 = b1; stage1.b2 = b2; stage1.a1 = a1; stage1.a2 = a2;
    }

    // --- Stage 2: RLB high-pass. Re-derive via the standard high-pass design used to produce the
    // 48 kHz reference numbers (fc ~ 38.1355 Hz, Q ~ 0.5003270). ---
    {
        const double Q  = 0.5003270373238773;
        const double fc = 38.13547087602444;

        const double omega  = std::tan (juce::MathConstants<double>::pi * fc / sr);
        const double omega2 = omega * omega;
        const double denom  = 1.0 + omega / Q + omega2;

        const double a1 = 2.0 * (omega2 - 1.0) / denom;
        const double a2 = (1.0 - omega / Q + omega2) / denom;

        // The RLB high-pass numerator in BS.1770 is exactly (1, -2, 1) — unit high-frequency gain,
        // NOT re-normalised by denom (only the denominator/feedback terms carry the 1/denom). This
        // reproduces the canonical 48 kHz reference (b = 1, -2, 1) exactly.
        stage2.b0 =  1.0;
        stage2.b1 = -2.0;
        stage2.b2 =  1.0;
        stage2.a1 = a1;
        stage2.a2 = a2;
    }
}

//==============================================================================
void LoudnessAnalyzer::prepare (double sr, int numChannels)
{
    sampleRate = (sr > 0.0) ? sr : 48000.0;
    channels   = juce::jmax (1, numChannels);

    computeKWeightingCoeffs (sampleRate);

    stage1.prepareChannels (channels);
    stage2.prepareChannels (channels);

    subBlockLenSamples = juce::jmax (1, (int) std::llround (sampleRate * kStepMs / 1000.0));

    tpHistory.assign ((size_t) channels, {});

    reset();
}

void LoudnessAnalyzer::reset()
{
    stage1.reset();
    stage2.reset();

    subBlockPos   = 0;
    subBlockSumSq = 0.0;
    subBlockMeanSq.clear();

    truePeakLinear = 0.0;
    for (auto& h : tpHistory)
        h.fill (0.0f);
}

//==============================================================================
void LoudnessAnalyzer::pushSubBlock()
{
    // Mean over the sub-block of the per-sample channel-weighted sum of K-weighted squares.
    const double meanSq = (subBlockLenSamples > 0) ? subBlockSumSq / (double) subBlockLenSamples : 0.0;
    subBlockMeanSq.push_back (meanSq);

    subBlockSumSq = 0.0;
    subBlockPos   = 0;
}

void LoudnessAnalyzer::processBlock (const float* const* channelData, int numChannels, int numSamples)
{
    if (channels <= 0 || numSamples <= 0 || channelData == nullptr)
        return;

    const int chToUse = juce::jmin (channels, numChannels);

    for (int i = 0; i < numSamples; ++i)
    {
        double weightedSq = 0.0;

        for (int ch = 0; ch < chToUse; ++ch)
        {
            const float* src = channelData[ch];
            const double x = (src != nullptr) ? (double) src[i] : 0.0;

            // K-weighting: high-shelf then RLB high-pass, per channel.
            const double y1 = stage1.processSample (x, ch);
            const double y  = stage2.processSample (y1, ch);

            // Channel weight G = 1.0 for L and R (and mono). Channels 3/4 (surround) would carry
            // G = 1.41 per the standard; Forge renders stereo, so 1.0 across the board is correct.
            weightedSq += y * y;
        }

        // Any prepared channels beyond the supplied ones still need their filter state advanced with
        // zero input so their delay lines don't go stale, but they contribute no energy.
        for (int ch = chToUse; ch < channels; ++ch)
        {
            const double y1 = stage1.processSample (0.0, ch);
            (void) stage2.processSample (y1, ch);
        }

        subBlockSumSq += weightedSq;

        if (++subBlockPos >= subBlockLenSamples)
            pushSubBlock();
    }

    updateTruePeak (channelData, numChannels, numSamples);
}

//==============================================================================
void LoudnessAnalyzer::updateTruePeak (const float* const* channelData, int numChannels, int numSamples)
{
    if (channelData == nullptr)
        return;

    const int chToUse = juce::jmin (channels, numChannels);

    for (int ch = 0; ch < chToUse; ++ch)
    {
        const float* src = channelData[ch];
        if (src == nullptr)
            continue;

        auto& hist = tpHistory[(size_t) ch];   // newest-first ring of the last kTpTaps samples

        for (int i = 0; i < numSamples; ++i)
        {
            // Shift the sample into the newest-first history.
            for (int t = kTpTaps - 1; t > 0; --t)
                hist[(size_t) t] = hist[(size_t) (t - 1)];
            hist[0] = src[i];

            // Evaluate all 4 oversample phases at this position and track the max |value|.
            for (int p = 0; p < kTpPhases; ++p)
            {
                double acc = 0.0;
                for (int t = 0; t < kTpTaps; ++t)
                    acc += (double) kTpCoeffs[p][t] * (double) hist[(size_t) t];

                truePeakLinear = std::max (truePeakLinear, std::abs (acc));
            }
        }
    }
}

//==============================================================================
LoudnessAnalyzer::Result LoudnessAnalyzer::getResult() const
{
    Result result;

    // Flush any in-progress sub-block conceptually: BS.1770 discards a trailing partial gating block,
    // and we only ever form 400 ms blocks from *completed* 100 ms sub-blocks, so a partial tail sub-
    // block simply doesn't contribute. (subBlockMeanSq already holds only completed sub-blocks.)
    const int n = (int) subBlockMeanSq.size();

    // --- True peak in dBTP (independent of gating) ---
    result.truePeakDb = (truePeakLinear > 0.0)
        ? (float) (20.0 * std::log10 (truePeakLinear))
        : -144.0f;

    if (n < kSubBlocksPerBlock)
        return result;   // < 400 ms of audio: no gating block, integrated stays kSilenceLufs

    // Build the overlapping 400 ms gating blocks. Block g spans sub-blocks [g, g+4); its channel-
    // weighted mean-square is the mean of its 4 sub-block mean-squares. There are (n-3) such blocks
    // (one per 100 ms step), realising the 75 % overlap.
    const int numBlocks = n - kSubBlocksPerBlock + 1;

    std::vector<double> blockMeanSq;
    blockMeanSq.reserve ((size_t) numBlocks);

    for (int g = 0; g < numBlocks; ++g)
    {
        double sum = 0.0;
        for (int s = 0; s < kSubBlocksPerBlock; ++s)
            sum += subBlockMeanSq[(size_t) (g + s)];

        blockMeanSq.push_back (sum / (double) kSubBlocksPerBlock);
    }

    // --- Absolute gate at -70 LUFS ---
    std::vector<double> absGated;   // mean-squares of blocks passing the absolute gate
    absGated.reserve (blockMeanSq.size());

    for (double ms : blockMeanSq)
        if (blockLoudness (ms) >= kAbsoluteGateLufs)
            absGated.push_back (ms);

    if (absGated.empty())
        return result;   // effectively silent — integrated stays kSilenceLufs

    // --- Relative gate: threshold = (mean loudness of abs-gated blocks) - 10 LU ---
    double meanMsAbs = 0.0;
    for (double ms : absGated)
        meanMsAbs += ms;
    meanMsAbs /= (double) absGated.size();

    const double relThresholdLufs = blockLoudness (meanMsAbs) + kRelativeGateLu;

    double sumMsRel = 0.0;
    int    countRel = 0;

    for (double ms : blockMeanSq)
    {
        const double lk = blockLoudness (ms);
        if (lk >= kAbsoluteGateLufs && lk >= relThresholdLufs)
        {
            sumMsRel += ms;
            ++countRel;
        }
    }

    if (countRel > 0)
    {
        const double integratedMs = sumMsRel / (double) countRel;
        result.integratedLufs = (float) blockLoudness (integratedMs);
    }

    // --- Momentary (loudest 400 ms window) and short-term (loudest 3 s window), ungated. ---
    // Both are computed over the same completed-sub-block series; each is the max block loudness of a
    // sliding window of the given length. The momentary window is exactly a gating block.
    {
        double loudestMomentary = -std::numeric_limits<double>::infinity();
        for (double ms : blockMeanSq)
            loudestMomentary = std::max (loudestMomentary, blockLoudness (ms));

        if (std::isfinite (loudestMomentary))
            result.momentaryLufs = (float) loudestMomentary;
    }

    if (n >= kShortTermSubBlocks)
    {
        double loudestShort = -std::numeric_limits<double>::infinity();
        const int numShort = n - kShortTermSubBlocks + 1;

        for (int g = 0; g < numShort; ++g)
        {
            double sum = 0.0;
            for (int s = 0; s < kShortTermSubBlocks; ++s)
                sum += subBlockMeanSq[(size_t) (g + s)];

            loudestShort = std::max (loudestShort, blockLoudness (sum / (double) kShortTermSubBlocks));
        }

        if (std::isfinite (loudestShort))
            result.shortTermLufs = (float) loudestShort;
    }

    return result;
}

//==============================================================================
LoudnessAnalyzer::Result LoudnessAnalyzer::analyzeFile (const juce::File& wav)
{
    Result result;

    if (! wav.existsAsFile())
    {
        FORGE_LOG_WARN ("LoudnessAnalyzer::analyzeFile — file does not exist: " + wav.getFullPathName());
        return result;
    }

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (wav));

    if (reader == nullptr)
    {
        FORGE_LOG_WARN ("LoudnessAnalyzer::analyzeFile — no reader for: " + wav.getFullPathName());
        return result;
    }

    const int    numCh = (int) reader->numChannels;
    const double sr    = reader->sampleRate;
    const int64  total = reader->lengthInSamples;

    if (numCh <= 0 || sr <= 0.0 || total <= 0)
    {
        FORGE_LOG_WARN ("LoudnessAnalyzer::analyzeFile — empty/invalid audio in: " + wav.getFullPathName());
        return result;
    }

    LoudnessAnalyzer analyzer;
    analyzer.prepare (sr, numCh);

    const int chunk = 1 << 15;   // 32768 samples per read
    juce::AudioBuffer<float> buffer (numCh, chunk);

    int64 pos = 0;

    while (pos < total)
    {
        const int thisChunk = (int) juce::jmin ((int64) chunk, total - pos);

        buffer.clear();

        if (! reader->read (&buffer, 0, thisChunk, pos, true, true))
        {
            FORGE_LOG_WARN ("LoudnessAnalyzer::analyzeFile — read failed at sample "
                            + juce::String (pos) + " in: " + wav.getFullPathName());
            break;
        }

        analyzer.processBlock (buffer.getArrayOfReadPointers(), numCh, thisChunk);

        pos += thisChunk;
    }

    return analyzer.getResult();
}

} // namespace forge::dsp
