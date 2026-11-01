#pragma once

#include <cstdint>

namespace av {

// Stateless integer hashing for deterministic procedural generation:
// the same inputs always produce the same world.
inline std::uint32_t hash32(std::uint32_t x)
{
    // lowbias32 (Chris Wellons)
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

inline std::uint32_t hashCombine(std::uint32_t seed, std::uint32_t v)
{
    return hash32(seed ^ (v + 0x9e3779b9U + (seed << 6) + (seed >> 2)));
}

inline std::uint32_t hash2i(std::uint32_t seed, int x, int y)
{
    return hashCombine(hashCombine(seed, static_cast<std::uint32_t>(x)), static_cast<std::uint32_t>(y));
}

// Small deterministic PRNG (PCG32).
class Rng {
public:
    explicit Rng(std::uint64_t seed = 0x853c49e6748fea9bULL) { reseed(seed); }

    void reseed(std::uint64_t seed)
    {
        state_ = 0;
        next();
        state_ += seed;
        next();
    }

    std::uint32_t next()
    {
        const std::uint64_t old = state_;
        state_ = old * 6364136223846793005ULL + 1442695040888963407ULL;
        const auto xorshifted = static_cast<std::uint32_t>(((old >> 18u) ^ old) >> 27u);
        const auto rot = static_cast<std::uint32_t>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
    }

    // [0, 1)
    float uniform() { return static_cast<float>(next() >> 8) * (1.0f / 16777216.0f); }
    float range(float lo, float hi) { return lo + (hi - lo) * uniform(); }
    // [lo, hi] inclusive
    int rangeInt(int lo, int hi)
    {
        if (hi <= lo) {
            return lo;
        }
        return lo + static_cast<int>(next() % static_cast<std::uint32_t>(hi - lo + 1));
    }
    bool chance(float p) { return uniform() < p; }

private:
    std::uint64_t state_ = 0;
};

} // namespace av
