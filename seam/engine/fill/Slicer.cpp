#include "Slicer.h"

#include <algorithm>
#include <cmath>

namespace seam::engine
{
    namespace
    {
        constexpr double pi = 3.14159265358979323846;

        size_t nextPowerOfTwo (size_t n)
        {
            size_t p = 1;
            while (p < n) p <<= 1;
            return p;
        }

        std::vector<float> hann (size_t n)
        {
            std::vector<float> w (n);
            for (size_t i = 0; i < n; ++i)
                w[i] = static_cast<float> (0.5 - 0.5 * std::cos (2.0 * pi * static_cast<double> (i) / static_cast<double> (n)));
            return w;
        }

        struct Features { float centroid = 0.0f, lowRatio = 0.0f, peak = 0.0f; };

        /** Power spectrum of x[from, from + n) with a half-Hann fade-out (the attack keeps full weight). */
        std::vector<double> powerSpectrum (std::span<const float> x, long long from, size_t n, size_t fftSize)
        {
            std::vector<float> re (fftSize, 0.0f), im (fftSize, 0.0f);
            for (size_t i = 0; i < n; ++i)
            {
                const long long s = from + static_cast<long long> (i);
                if (s < 0 || s >= static_cast<long long> (x.size()))
                    continue;
                const double tail = static_cast<double> (i) / static_cast<double> (n);
                const double w = tail < 0.5 ? 1.0 : 0.5 + 0.5 * std::cos (pi * (tail - 0.5) * 2.0);
                re[i] = x[static_cast<size_t> (s)] * static_cast<float> (w);
            }
            fftInPlace (re, im);
            std::vector<double> power (fftSize / 2);
            for (size_t k = 0; k < fftSize / 2; ++k)
                power[k] = static_cast<double> (re[k]) * re[k] + static_cast<double> (im[k]) * im[k];
            return power;
        }

        Features attackFeatures (std::span<const float> x, size_t start, size_t end, double sampleRate)
        {
            Features f;
            const size_t attack = std::min (end, start + static_cast<size_t> (0.06 * sampleRate));
            const size_t peakEnd = std::min (end, start + static_cast<size_t> (0.03 * sampleRate));
            for (size_t i = start; i < peakEnd; ++i)
                f.peak = std::max (f.peak, std::abs (x[i]));

            const size_t n = attack - start;
            if (n < 16)
                return f;
            const size_t fftSize = nextPowerOfTwo (std::max<size_t> (n, 2048));

            // What the hit adds: the attack's spectrum minus the spectrum just before it, so the ringing tail of the
            // previous hit (a kick under a hat, say) does not colour the label.
            const auto now = powerSpectrum (x, static_cast<long long> (start), n, fftSize);
            const auto before = powerSpectrum (x, static_cast<long long> (start) - static_cast<long long> (n), n, fftSize);

            double total = 0.0, weighted = 0.0, low = 0.0;
            const double binHz = sampleRate / static_cast<double> (fftSize);
            for (size_t k = 1; k < fftSize / 2; ++k)
            {
                const double hz = static_cast<double> (k) * binHz;
                const double power = std::max (0.0, now[k] - before[k]);
                total += power;
                weighted += power * hz;
                if (hz < 150.0) low += power;
            }
            if (total > 0.0)
            {
                f.centroid = static_cast<float> (weighted / total);
                f.lowRatio = static_cast<float> (low / total);
            }
            return f;
        }
    }

    void fftInPlace (std::vector<float>& re, std::vector<float>& im)
    {
        const size_t n = re.size();
        for (size_t i = 1, j = 0; i < n; ++i)
        {
            size_t bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) { std::swap (re[i], re[j]); std::swap (im[i], im[j]); }
        }
        for (size_t len = 2; len <= n; len <<= 1)
        {
            const double angle = -2.0 * pi / static_cast<double> (len);
            const auto wr = static_cast<float> (std::cos (angle)), wi = static_cast<float> (std::sin (angle));
            for (size_t i = 0; i < n; i += len)
            {
                float cr = 1.0f, ci = 0.0f;
                for (size_t k = 0; k < len / 2; ++k)
                {
                    const size_t a = i + k, b = i + k + len / 2;
                    const float tr = re[b] * cr - im[b] * ci;
                    const float ti = re[b] * ci + im[b] * cr;
                    re[b] = re[a] - tr; im[b] = im[a] - ti;
                    re[a] += tr;        im[a] += ti;
                    const float nr = cr * wr - ci * wi;
                    ci = cr * wi + ci * wr;
                    cr = nr;
                }
            }
        }
    }

    SliceResult sliceBreak (std::span<const float> input, double sampleRate, const SlicerOptions& options)
    {
        SliceResult result;
        if (sampleRate <= 0.0 || input.empty())
        {
            result.summary = "No audio";
            return result;
        }

        const auto x = input.first (std::min (input.size(), static_cast<size_t> (options.maxSeconds * sampleRate)));
        const size_t win = nextPowerOfTwo (static_cast<size_t> (0.023 * sampleRate));
        const size_t hop = std::max<size_t> (32, static_cast<size_t> (std::round (0.0029 * sampleRate)));
        const auto window = hann (win);
        const size_t maxBin = std::min (win / 2, static_cast<size_t> (16000.0 / sampleRate * static_cast<double> (win)));

        // --- Spectral flux.
        const size_t frames = x.size() / hop + 1;
        // SuperFlux-style: compare with a frequency max-filtered frame from two hops back, which ignores the
        // frame-to-frame flicker of noisy tails (snare and hat decays) but not a new hit.
        std::vector<float> flux (frames, 0.0f), re (win), im (win), mag (maxBin);
        std::vector<std::vector<float>> history (2, std::vector<float> (maxBin, 0.0f));
        for (size_t f = 0; f < frames; ++f)
        {
            const auto centre = static_cast<long long> (f * hop);
            for (size_t i = 0; i < win; ++i)
            {
                const long long s = centre - static_cast<long long> (win / 2) + static_cast<long long> (i);
                re[i] = (s >= 0 && s < static_cast<long long> (x.size())) ? x[static_cast<size_t> (s)] * window[i] : 0.0f;
                im[i] = 0.0f;
            }
            fftInPlace (re, im);
            for (size_t k = 0; k < maxBin; ++k)
                mag[k] = std::log1p (100.0f * std::sqrt (re[k] * re[k] + im[k] * im[k]));
            const auto& reference = history[f % 2];   // the frame from two hops back
            float sum = 0.0f;
            for (size_t k = 1; k + 1 < maxBin; ++k)
            {
                const float ref = std::max ({ reference[k - 1], reference[k], reference[k + 1] });
                sum += std::max (0.0f, mag[k] - ref);
            }
            flux[f] = sum;
            history[f % 2] = mag;
        }

        const float fluxMax = *std::max_element (flux.begin(), flux.end());
        float peakLevel = 0.0f;
        for (const float s : x)
            peakLevel = std::max (peakLevel, std::abs (s));
        if (fluxMax <= 0.0f || peakLevel < 1.0e-4f)
        {
            result.summary = "Silent: no hits found";
            return result;
        }
        for (auto& v : flux)
            v /= fluxMax;

        // --- Peak picking with an adaptive threshold.
        const auto sensitivity = static_cast<float> (std::max (0.1, options.sensitivity));
        const auto minGap = static_cast<size_t> (options.minGapSeconds * sampleRate);
        std::vector<size_t> onsets;
        std::vector<float> neighbourhood;
        for (size_t f = 0; f + 1 < frames; ++f)
        {
            // Adaptive threshold: the local median (robust against the hit's own frames) plus a margin.
            const size_t lo = f > 10 ? f - 10 : 0, hi = std::min (frames, f + 11);
            neighbourhood.assign (flux.begin() + static_cast<long> (lo), flux.begin() + static_cast<long> (hi));
            std::nth_element (neighbourhood.begin(), neighbourhood.begin() + static_cast<long> (neighbourhood.size() / 2), neighbourhood.end());
            const float median = neighbourhood[neighbourhood.size() / 2];
            if (flux[f] < 0.012f / sensitivity || flux[f] < median * 1.4f + 0.004f / sensitivity)
                continue;
            bool isMax = true;
            for (size_t g = (f > 3 ? f - 3 : 0); g <= std::min (frames - 1, f + 3) && isMax; ++g)
                if (g != f && (flux[g] > flux[f] || (flux[g] == flux[f] && g < f)))
                    isMax = false;
            if (! isMax)
                continue;

            // Refine to the sample: find the loudest point near the frame, then walk back to where the rise starts.
            const auto centre = static_cast<long long> (f * hop);
            const long long earliest = onsets.empty() ? 0 : static_cast<long long> (onsets.back() + minGap / 2);
            const auto from = static_cast<size_t> (std::max (earliest, centre - static_cast<long long> (win / 2)));
            const auto to = std::min (x.size(), static_cast<size_t> (centre + static_cast<long long> (win / 2)));
            if (from >= to)
                continue;
            // Refine on the first difference |x[n] - x[n-1]|: it follows transients and mostly ignores low, ringing
            // tails (a kick under a hat), so the onset lands on the new hit's attack.
            auto slope = [&] (size_t i) { return i == 0 ? std::abs (x[0]) : std::abs (x[i] - x[i - 1]); };
            size_t loudest = from;
            for (size_t i = from; i < to; ++i)
                if (slope (i) > slope (loudest)) loudest = i;
            const float local = slope (loudest);
            float levelPeak = 0.0f;
            for (size_t i = from; i < to; ++i)
                levelPeak = std::max (levelPeak, std::abs (x[i]));
            if (levelPeak < peakLevel * 0.02f / sensitivity)
                continue;

            // Walk back to the half-level point of the rise, then at most 2 ms further while still above 15%.
            const size_t smooth = std::max<size_t> (1, static_cast<size_t> (0.0005 * sampleRate));
            auto envelopeBefore = [&] (size_t i)
            {
                float env = 0.0f;
                for (size_t k = 0; k < smooth && i >= k + 1; ++k)
                    env = std::max (env, slope (i - 1 - k));
                return env;
            };
            size_t onset = loudest;
            while (onset > from && envelopeBefore (onset) >= local * 0.5f)
                --onset;
            const size_t halfPoint = onset;
            const auto maxWalk = static_cast<size_t> (0.002 * sampleRate);
            while (onset > from && halfPoint - onset < maxWalk && envelopeBefore (onset) >= local * 0.15f)
                --onset;

            // Small pre-roll: a hair early keeps the whole transient; late would cut it.
            const auto preRoll = static_cast<size_t> (0.0005 * sampleRate);
            onset = onset > from + preRoll ? onset - preRoll : from;

            // A real hit adds transient energy: compare the slope energy just after the onset with the 10 ms
            // before it. This rejects flux from a decaying tail (e.g. an 808's pitch glide) but keeps ghost notes.
            {
                const auto after = static_cast<size_t> (0.005 * sampleRate);
                const auto before = static_cast<size_t> (0.010 * sampleRate);
                double rise = 0.0, prior = 0.0;
                for (size_t i = onset; i < std::min (x.size(), onset + after); ++i)
                    rise += static_cast<double> (slope (i)) * slope (i);
                for (size_t i = onset > before ? onset - before : 0; i < onset; ++i)
                    prior += static_cast<double> (slope (i)) * slope (i);
                rise /= static_cast<double> (after);
                prior /= static_cast<double> (before);
                if (rise < prior * 1.6 + 1.0e-12)
                    continue;
            }

            if (! onsets.empty() && onset < onsets.back() + minGap)
                continue;
            onsets.push_back (onset);
            if (onsets.size() >= options.maxSlices)
                break;
        }

        // --- Slices and labels.
        std::vector<Features> features;
        for (size_t i = 0; i < onsets.size(); ++i)
        {
            const size_t end = i + 1 < onsets.size() ? onsets[i + 1] : x.size();
            Slice slice;
            slice.start = onsets[i];
            slice.length = std::min (end - onsets[i], static_cast<size_t> (1.5 * sampleRate));
            const auto f = attackFeatures (x, slice.start, end, sampleRate);
            slice.peak = f.peak;
            slice.centroid = f.centroid;
            slice.lowRatio = f.lowRatio;
            if (f.lowRatio > 0.35f && f.centroid < 1500.0f)
                slice.type = SliceType::kick;
            else if (f.centroid > 5000.0f && f.lowRatio < 0.05f)
                slice.type = SliceType::hat;
            else
                slice.type = SliceType::snare;
            result.slices.push_back (slice);
        }

        float loudestSnare = 0.0f;
        int snareLike = 0;
        for (const auto& s : result.slices)
            if (s.type == SliceType::snare) { loudestSnare = std::max (loudestSnare, s.peak); ++snareLike; }
        if (snareLike > 1)
            for (auto& s : result.slices)
                if (s.type == SliceType::snare && s.peak < loudestSnare * 0.45f)
                    s.type = SliceType::ghost;

        std::array<int, numSliceTypes> counts {};
        for (const auto& s : result.slices)
            ++counts[static_cast<size_t> (s.type)];
        auto plural = [] (int n, const char* word) { return std::to_string (n) + " " + word + (n == 1 ? "" : "s"); };
        result.summary = plural (static_cast<int> (result.slices.size()), "hit") + ": " + plural (counts[0], "kick") + ", "
                         + plural (counts[1], "snare") + ", " + plural (counts[2], "ghost") + ", " + plural (counts[3], "hat");
        return result;
    }

    BreakKit buildKit (std::vector<float> left, std::vector<float> right, double sampleRate, std::string name,
                       const SlicerOptions& options)
    {
        BreakKit kit;
        kit.sampleRate = sampleRate;
        kit.name = std::move (name);
        if (right.size() != left.size())
            right = left;
        std::vector<float> mono (left.size());
        for (size_t i = 0; i < mono.size(); ++i)
            mono[i] = 0.5f * (left[i] + right[i]);
        kit.slices = sliceBreak (mono, sampleRate, options).slices;
        kit.left = std::move (left);
        kit.right = std::move (right);
        kit.indexByType();
        return kit;
    }
}
