#include "Audio/Audio.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#endif

namespace av {

namespace {

constexpr int kRate = 22050;
constexpr float kTau = 6.28318530718f;

// ---- synthesis helpers ---------------------------------------------------

struct Buffer {
    std::vector<float> samples;
};

float noteHz(int midi) { return 440.0f * std::pow(2.0f, (static_cast<float>(midi) - 69.0f) / 12.0f); }

void addTone(Buffer& b, float start, float duration, float hz, float volume, int wave, float decay = 6.0f)
{
    const auto s0 = static_cast<std::size_t>(start * kRate);
    const auto n = static_cast<std::size_t>(duration * kRate);
    if (b.samples.size() < s0 + n) b.samples.resize(s0 + n, 0.0f);
    float phase = 0.0f;
    for (std::size_t i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / kRate;
        phase += hz / kRate;
        const float p = phase - std::floor(phase);
        float v;
        switch (wave) {
        case 0: v = std::sin(p * kTau); break;              // sine
        case 1: v = p < 0.5f ? 0.6f : -0.6f; break;         // square
        case 2: v = (p * 2.0f - 1.0f) * 0.7f; break;        // saw
        default: v = 1.0f - 4.0f * std::fabs(p - 0.5f); break; // triangle
        }
        const float attack = std::min(1.0f, t * 150.0f); // ~7 ms fade-in: no clicks
        b.samples[s0 + i] += v * volume * attack * std::exp(-decay * t);
    }
}

// A soft low "body" thump: a sine that drops in pitch, with a quick fade-in.
void addThump(Buffer& b, float start, float duration, float hz0, float hz1, float volume, float decay)
{
    const auto s0 = static_cast<std::size_t>(start * kRate);
    const auto n = static_cast<std::size_t>(duration * kRate);
    if (b.samples.size() < s0 + n) b.samples.resize(s0 + n, 0.0f);
    float phase = 0.0f;
    for (std::size_t i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / kRate;
        const float hz = hz1 + (hz0 - hz1) * std::exp(-t * 40.0f);
        phase += hz / kRate;
        const float attack = std::min(1.0f, t * 500.0f);
        b.samples[s0 + i] += std::sin(phase * kTau) * volume * attack * std::exp(-decay * t);
    }
}

// Two-pole low-passed noise (darker than addNoise), with a fade-in.
void addSoftNoise(Buffer& b, float start, float duration, float volume, float decay, float cutoff, std::uint32_t seed,
                  float attackTime = 0.003f)
{
    const auto s0 = static_cast<std::size_t>(start * kRate);
    const auto n = static_cast<std::size_t>(duration * kRate);
    if (b.samples.size() < s0 + n) b.samples.resize(s0 + n, 0.0f);
    std::minstd_rand rng(seed);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    const float k = 1.0f - std::exp(-kTau * cutoff / kRate);
    float a = 0.0f, c = 0.0f;
    for (std::size_t i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / kRate;
        a += (dist(rng) - a) * k;
        c += (a - c) * k;
        const float attack = std::min(1.0f, t / attackTime);
        b.samples[s0 + i] += c * 2.2f * volume * attack * std::exp(-decay * t);
    }
}

std::vector<Buffer> synthesise()
{
    std::vector<Buffer> s(static_cast<std::size_t>(Sound::Count));
    auto at = [&s](Sound id) -> Buffer& { return s[static_cast<std::size_t>(id)]; };
    // UI: soft sine / triangle blips.
    addTone(at(Sound::MenuMove), 0, 0.05f, 1100, 0.10f, 0, 70);
    addTone(at(Sound::MenuSelect), 0, 0.08f, noteHz(76), 0.13f, 3, 25);
    addTone(at(Sound::MenuSelect), 0.06f, 0.14f, noteHz(83), 0.13f, 3, 18);
    // Footsteps: a dull heel thump plus a short, dark scuff of grit.
    addThump(at(Sound::Footstep), 0, 0.09f, 110, 55, 0.30f, 45);
    addSoftNoise(at(Sound::Footstep), 0.004f, 0.07f, 0.16f, 55, 900, 1);
    // Punch: body thud with a muffled slap.
    addThump(at(Sound::Punch), 0, 0.18f, 140, 50, 0.45f, 22);
    addSoftNoise(at(Sound::Punch), 0, 0.06f, 0.22f, 60, 1800, 2);
    addThump(at(Sound::Hit), 0, 0.25f, 120, 45, 0.45f, 14);
    addSoftNoise(at(Sound::Hit), 0, 0.2f, 0.25f, 20, 900, 3);
    // Pickup: a gentle rising chime.
    for (int i = 0; i < 3; ++i) {
        addTone(at(Sound::Pickup), 0.07f * static_cast<float>(i), 0.3f, noteHz(79 + (i == 1 ? 4 : i == 2 ? 7 : 0)), 0.11f, 0, 9);
    }
    for (const int n : {57, 64, 69}) addTone(at(Sound::MissionStart), 0, 0.8f, noteHz(n), 0.09f, 3, 3);
    {
        const int melody[] = {69, 72, 76, 81};
        for (int i = 0; i < 4; ++i) addTone(at(Sound::MissionPass), 0.12f * static_cast<float>(i), 0.45f, noteHz(melody[i]), 0.12f, 3, 5);
        for (const int n : {69, 76}) addTone(at(Sound::MissionPass), 0.5f, 1.0f, noteHz(n), 0.08f, 0, 2.5f);
    }
    {
        const int melody[] = {64, 60, 55};
        for (int i = 0; i < 3; ++i) addTone(at(Sound::MissionFail), 0.22f * static_cast<float>(i), 0.5f, noteHz(melody[i]), 0.11f, 3, 4);
    }
    // Crash: a heavy thud, crunch and a little tinkle of glass.
    addThump(at(Sound::Crash), 0, 0.5f, 90, 35, 0.55f, 7);
    addSoftNoise(at(Sound::Crash), 0, 0.55f, 0.45f, 7, 1400, 4);
    for (int i = 0; i < 4; ++i) addTone(at(Sound::Crash), 0.05f + 0.04f * static_cast<float>(i), 0.12f, 2500.0f + 700.0f * static_cast<float>(i), 0.025f, 0, 30);
    addTone(at(Sound::Chat), 0, 0.07f, 1200, 0.08f, 0, 40);
    addTone(at(Sound::Chat), 0.06f, 0.08f, 1600, 0.08f, 0, 40);
    addTone(at(Sound::Arrested), 0, 0.35f, noteHz(55), 0.14f, 3, 4);
    addTone(at(Sound::Arrested), 0.3f, 0.7f, noteHz(50), 0.14f, 3, 3);
    // Money: a soft coin "ching".
    addTone(at(Sound::Money), 0, 0.25f, noteHz(88), 0.08f, 0, 14);
    addTone(at(Sound::Money), 0.07f, 0.4f, noteHz(95), 0.07f, 0, 10);
    // Car door: latch click and a dull thunk.
    addSoftNoise(at(Sound::Door), 0, 0.03f, 0.18f, 90, 3500, 5);
    addThump(at(Sound::Door), 0.04f, 0.18f, 95, 50, 0.35f, 25);
    addTone(at(Sound::Countdown), 0, 0.3f, noteHz(72), 0.12f, 3, 7);
    addTone(at(Sound::Go), 0, 0.6f, noteHz(84), 0.12f, 3, 4);
    // Gunshot: a sharp (but filtered) crack, a low boom and a short tail.
    addSoftNoise(at(Sound::Gunshot), 0, 0.05f, 0.5f, 70, 4000, 6, 0.0008f);
    addThump(at(Sound::Gunshot), 0, 0.22f, 160, 45, 0.45f, 18);
    addSoftNoise(at(Sound::Gunshot), 0.02f, 0.35f, 0.12f, 9, 700, 7);
    // Explosion: deep rumble that rolls off.
    addThump(at(Sound::Explosion), 0, 1.0f, 70, 25, 0.6f, 4);
    addSoftNoise(at(Sound::Explosion), 0, 1.6f, 0.55f, 2.8f, 500, 8, 0.01f);
    addSoftNoise(at(Sound::Explosion), 0, 0.3f, 0.25f, 12, 2500, 9);
    // Swoosh (rocket / throw): filtered air.
    addSoftNoise(at(Sound::Swoosh), 0, 0.45f, 0.22f, 5, 1200, 10, 0.08f);
    return s;
}

// ---- procedural radio: a 4-bar synthwave loop ----------------------------
struct Radio {
    double t = 0.0;
    float sample()
    {
        constexpr double bpm = 108.0;
        const double beat = t * bpm / 60.0;
        const int bar = static_cast<int>(beat / 4.0) % 4;
        const double inBeat = beat - std::floor(beat);
        static const int chords[4][3] = {{57, 60, 64}, {53, 57, 60}, {48, 52, 55}, {55, 59, 62}}; // Am F C G
        const int* ch = chords[bar];
        // Bass: root, eighth notes.
        const double eighth = beat * 2.0 - std::floor(beat * 2.0);
        const float bassHz = noteHz(ch[0] - 24);
        const float bassPhase = static_cast<float>(std::fmod(t * bassHz, 1.0));
        float v = std::sin(bassPhase * kTau) * 0.2f * static_cast<float>(std::exp(-eighth * 3.0));
        // Arpeggio: sixteenth notes through the chord.
        const int step = static_cast<int>(beat * 4.0) % 3;
        const double sixteenth = beat * 4.0 - std::floor(beat * 4.0);
        const float arpHz = noteHz(ch[step] + 12);
        const float arp = static_cast<float>(std::fmod(t * arpHz, 1.0));
        v += (1.0f - 4.0f * std::fabs(arp - 0.5f)) * 0.05f * static_cast<float>(std::exp(-sixteenth * 5.0));
        // Pad.
        for (int i = 0; i < 3; ++i) {
            v += static_cast<float>(std::sin(t * noteHz(ch[i]) * kTau)) * 0.035f;
        }
        // Drums: kick on every beat, snare on 2 and 4.
        v += static_cast<float>(std::sin(kTau * 50.0 * inBeat * (1.0 + 3.0 * std::exp(-inBeat * 30.0)))) * 0.3f *
             static_cast<float>(std::exp(-inBeat * 12.0));
        const int beatInBar = static_cast<int>(beat) % 4;
        if (beatInBar == 1 || beatInBar == 3) {
            const auto h = static_cast<std::uint32_t>(t * kRate) * 2654435761u;
            v += (static_cast<float>(h >> 16) / 32768.0f - 1.0f) * 0.06f * static_cast<float>(std::exp(-inBeat * 18.0));
        }
        t += 1.0 / kRate;
        return v;
    }
};

struct Voice {
    const Buffer* buffer;
    double position;
    float rate;
    float volume;
};

struct LoopState {
    float volume = 0, targetVolume = 0, pitch = 1, phase = 0, phase2 = 0;
    float lp = 0, lp2 = 0; // filter state
};

} // namespace

struct Audio::Impl {
    std::vector<Buffer> sounds = synthesise();
    std::mutex mutex;
    std::vector<Voice> voices;
    LoopState loops[static_cast<int>(Loop::Count)];
    Radio radio;
    std::atomic<float> master{0.7f};
    std::atomic<bool> running{false};
    std::thread thread;
    std::uint32_t noiseState = 12345;
    std::vector<float> echo_ = std::vector<float>(static_cast<std::size_t>(kRate * 0.045f), 0.0f);
    std::size_t echoPos_ = 0;
    float outLp_ = 0.0f;

    void mix(std::int16_t* out, int count)
    {
        std::lock_guard<std::mutex> lock(mutex);
        const float m = master.load();
        for (int i = 0; i < count; ++i) {
            float v = 0.0f;
            for (Voice& voice : voices) {
                const auto idx = static_cast<std::size_t>(voice.position);
                if (idx < voice.buffer->samples.size()) {
                    v += voice.buffer->samples[idx] * voice.volume;
                }
                voice.position += voice.rate;
            }
            noiseState = noiseState * 1664525u + 1013904223u;
            const float noise = static_cast<float>(noiseState >> 16) / 32768.0f - 1.0f;
            // Engine: the firing pulse of a four-cylinder (a few harmonics,
            // throbbing at half the firing rate) through a low-pass, with
            // some intake rumble. Pitch follows the revs.
            LoopState& e = loops[static_cast<int>(Loop::Engine)];
            e.volume += (e.targetVolume - e.volume) * 0.002f;
            if (e.volume > 0.001f) {
                const float hz = 26.0f * e.pitch;
                e.phase = std::fmod(e.phase + hz / kRate, 1.0f);
                e.phase2 = std::fmod(e.phase2 + hz * 0.5f / kRate, 1.0f);
                const float p = e.phase * kTau;
                float x = std::sin(p) * 0.55f + std::sin(2.0f * p) * 0.3f + std::sin(3.0f * p) * 0.12f + std::sin(e.phase2 * kTau) * 0.25f;
                x *= 0.75f + 0.25f * std::sin(e.phase2 * kTau);
                x += noise * 0.12f;
                const float cutoff = std::min(0.25f, 0.04f + 0.02f * e.pitch); // brighter at high revs
                e.lp += (x - e.lp) * cutoff;
                e.lp2 += (e.lp - e.lp2) * cutoff;
                v += e.lp2 * e.volume * 0.5f;
            }
            // Helicopter rotor: noise chopped by each passing blade, plus a low drone.
            LoopState& rt = loops[static_cast<int>(Loop::Rotor)];
            rt.volume += (rt.targetVolume - rt.volume) * 0.002f;
            if (rt.volume > 0.001f) {
                rt.phase = std::fmod(rt.phase + 5.5f * rt.pitch / kRate, 1.0f);
                rt.phase2 = std::fmod(rt.phase2 + 48.0f * rt.pitch / kRate, 1.0f);
                const float chop = std::exp(-rt.phase * 9.0f);
                rt.lp += (noise - rt.lp) * 0.08f;
                v += (rt.lp * 1.6f * chop + std::sin(rt.phase2 * kTau) * 0.12f) * rt.volume * 0.5f;
            }
            // Jetpack: a steady hiss with a little flutter.
            LoopState& jt = loops[static_cast<int>(Loop::Jet)];
            jt.volume += (jt.targetVolume - jt.volume) * 0.003f;
            if (jt.volume > 0.001f) {
                jt.phase = std::fmod(jt.phase + 13.0f / kRate, 1.0f);
                jt.lp += (noise - jt.lp) * 0.18f;
                jt.lp2 += (jt.lp - jt.lp2) * 0.35f;
                v += jt.lp2 * (0.85f + 0.15f * std::sin(jt.phase * kTau)) * jt.volume * 0.45f;
            }
            // Siren: wailing sine.
            LoopState& s = loops[static_cast<int>(Loop::Siren)];
            s.volume += (s.targetVolume - s.volume) * 0.002f;
            if (s.volume > 0.001f) {
                s.phase2 = std::fmod(s.phase2 + 0.45f / kRate, 1.0f);
                const float hz = 700.0f + 450.0f * std::sin(s.phase2 * kTau);
                s.phase = std::fmod(s.phase + hz / kRate, 1.0f);
                v += std::sin(s.phase * kTau) * s.volume * 0.09f;
            }
            LoopState& r = loops[static_cast<int>(Loop::Radio)];
            r.volume += (r.targetVolume - r.volume) * 0.001f;
            if (r.volume > 0.001f) {
                v += radio.sample() * r.volume;
            }
            // A short room echo (makes things sound less dry), a gentle
            // low-pass that takes the edge off, then a soft limiter.
            const float echo = echo_[echoPos_];
            echo_[echoPos_] = v + echo * 0.25f;
            echoPos_ = (echoPos_ + 1) % echo_.size();
            v += echo * 0.18f;
            outLp_ += (v - outLp_) * 0.55f;
            v = std::tanh(outLp_ * m * 0.9f);
            out[i] = static_cast<std::int16_t>(v * 26000.0f);
        }
        voices.erase(std::remove_if(voices.begin(), voices.end(),
                                    [](const Voice& vc) { return vc.position >= static_cast<double>(vc.buffer->samples.size()); }),
                     voices.end());
    }

#ifdef _WIN32
    HWAVEOUT device = nullptr;

    bool open()
    {
        WAVEFORMATEX fmt{};
        fmt.wFormatTag = WAVE_FORMAT_PCM;
        fmt.nChannels = 1;
        fmt.nSamplesPerSec = kRate;
        fmt.wBitsPerSample = 16;
        fmt.nBlockAlign = 2;
        fmt.nAvgBytesPerSec = kRate * 2;
        if (waveOutOpen(&device, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
            device = nullptr;
            return false;
        }
        running = true;
        thread = std::thread([this] { pump(); });
        return true;
    }

    void pump()
    {
        constexpr int kBuffers = 4, kSamples = 1024; // ~46 ms each
        std::vector<std::int16_t> data(static_cast<std::size_t>(kBuffers * kSamples));
        WAVEHDR headers[kBuffers]{};
        for (int b = 0; b < kBuffers; ++b) {
            headers[b].lpData = reinterpret_cast<LPSTR>(data.data() + b * kSamples);
            headers[b].dwBufferLength = kSamples * 2;
            mix(data.data() + b * kSamples, kSamples);
            waveOutPrepareHeader(device, &headers[b], sizeof(WAVEHDR));
            waveOutWrite(device, &headers[b], sizeof(WAVEHDR));
        }
        while (running) {
            bool any = false;
            for (int b = 0; b < kBuffers; ++b) {
                if (headers[b].dwFlags & WHDR_DONE) {
                    mix(data.data() + b * kSamples, kSamples);
                    waveOutWrite(device, &headers[b], sizeof(WAVEHDR));
                    any = true;
                }
            }
            if (!any) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
        waveOutReset(device);
        for (int b = 0; b < kBuffers; ++b) {
            waveOutUnprepareHeader(device, &headers[b], sizeof(WAVEHDR));
        }
        waveOutClose(device);
    }
#else
    bool open() { return false; }
#endif

    ~Impl()
    {
        running = false;
        if (thread.joinable()) {
            thread.join();
        }
    }
};

Audio::Audio() : impl_(std::make_unique<Impl>()) {}
Audio::~Audio() = default;

bool Audio::start(float masterVolume)
{
    impl_->master = masterVolume;
    return impl_->open();
}

bool Audio::available() const { return impl_->running.load(); }

void Audio::setVolume(float masterVolume) { impl_->master = std::clamp(masterVolume, 0.0f, 1.0f); }

void Audio::play(Sound sound, float volume, float pitch)
{
    if (!available() || volume <= 0.0f) {
        return;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->voices.size() > 24) {
        impl_->voices.erase(impl_->voices.begin()); // drop the oldest rather than pile up
    }
    impl_->voices.push_back({&impl_->sounds[static_cast<std::size_t>(sound)], 0.0, pitch, volume});
}

void Audio::setLoop(Loop loop, float volume, float pitch)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    LoopState& l = impl_->loops[static_cast<int>(loop)];
    l.targetVolume = std::clamp(volume, 0.0f, 1.0f);
    l.pitch = pitch;
}

} // namespace av
