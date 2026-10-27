#pragma once

namespace benchmark::dsp
{
    /** Gain offsets (in dB, always <= 0) applied to each source for a fair comparison. */
    struct MatchGains
    {
        float mixDb = 0.0f;
        float referenceDb = 0.0f;
    };

    /**
        Loudness matching: the louder source is turned down to the quieter one.
        Nothing is ever boosted, so matching can never push a signal into clipping.
        Returns 0 dB for both when either measurement is unavailable (silence,
        not yet measured, or below the -70 LUFS absolute gate).
    */
    MatchGains computeMatchGains (float mixLufs, float referenceLufs, float maxAttenuationDb = 30.0f) noexcept;
}
