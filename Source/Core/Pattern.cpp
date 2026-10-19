#include "Pattern.h"

namespace bf
{

bool Pattern::isEmpty() const noexcept
{
    for (const auto& t : tracks)
        for (const auto& s : t.steps)
            if (s.on)
                return false;

    return true;
}

void Pattern::clear() noexcept
{
    for (auto& t : tracks)
        t = TrackPattern {};

    accents = {};
}

} // namespace bf
