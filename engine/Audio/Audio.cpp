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
        const float attack = std::min(1.0f, t * 400.0f);
        b.samples[s0 + i] += v * volume * attack * std::exp(-decay * t);
    }
}

void addNoise(Buffer& b, float start, float duration, float volume, float decay, float lowpass, std::uint32_t seed)
{
    const auto s0 = static_cast<std::size_t>(start * kRate);
    const auto n = static_cast<std::size_t>(duration * kRate);
    if (b.samples.size() < s0 + n) b.samples.resize(s0 + n, 0.0f);
    std::minstd_rand rng(seed);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    float lp = 0.0f;
    for (std::size_t i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / kRate;
        lp += (dist(rng) - lp) * lowpass;
        b.samples[s0 + i] += lp * volume * std::exp(-decay * t);
    }
}

void addSweep(Buffer& b, float start, float duration, float hz0, float hz1, float volume, float decay)
{
    const auto s0 = static_cast<std::size_t>(start * kRate);
    const auto n = static_cast<std::size_t>(duration * kRate);
    if (b.samples.size() < s0 + n) b.samples.resize(s0 + n, 0.0f);
    float phase = 0.0f;
    for (std::size_t i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / kRate;
        const float hz = hz0 + (hz1 - hz0) * (t / duration);
        phase += hz / kRate;
        b.samples[s0 + i] += std::sin(phase * kTau) * volume * std::exp(-decay * t);
    }
}

std::vector<Buffer> synthesise()
{
    std::vector<Buffer> s(static_cast<std::size_t>(Sound::Count));
    auto at = [&s](Sound id) -> Buffer& { return s[static_cast<std::size_t>(id)]; };
    addTone(at(Sound::MenuMove), 0, 0.03f, 1800, 0.25f, 3, 60);
    addTone(at(Sound::MenuSelect), 0, 0.06f, noteHz(79), 0.3f, 1, 25);
    addTone(at(Sound::MenuSelect), 0.06f, 0.12f, noteHz(86), 0.3f, 1, 18);
    addNoise(at(Sound::Footstep), 0, 0.06f, 0.35f, 70, 0.25f, 1);
    addSweep(at(Sound::Punch), 0, 0.16f, 160, 55, 0.8f, 18);
    addNoise(at(Sound::Punch), 0, 0.06f, 0.4f, 50, 0.5f, 2);
    addNoise(at(Sound::Hit), 0, 0.25f, 0.7f, 14, 0.35f, 3);
    addSweep(at(Sound::Hit), 0, 0.2f, 120, 50, 0.6f, 10);
    for (int i = 0; i < 4; ++i) {
        addTone(at(Sound::Pickup), 0.06f * static_cast<float>(i), 0.18f, noteHz(84 + (i == 1 ? 4 : i == 2 ? 7 : i == 3 ? 12 : 0)), 0.3f, 1, 10);
    }
    for (const int n : {57, 64, 69}) addTone(at(Sound::MissionStart), 0, 0.6f, noteHz(n), 0.25f, 2, 4);
    {
        const int melody[] = {69, 72, 76, 81, 84};
        for (int i = 0; i < 5; ++i) addTone(at(Sound::MissionPass), 0.11f * static_cast<float>(i), 0.35f, noteHz(melody[i]), 0.3f, 1, 6);
        addTone(at(Sound::MissionPass), 0.55f, 0.8f, noteHz(69), 0.2f, 2, 3);
    }
    {
        const int melody[] = {64, 60, 55};
        for (int i = 0; i < 3; ++i) addTone(at(Sound::MissionFail), 0.2f * static_cast<float>(i), 0.4f, noteHz(melody[i]), 0.3f, 2, 5);
    }
    addNoise(at(Sound::Crash), 0, 0.6f, 0.9f, 6, 0.4f, 4);
    addTone(at(Sound::Crash), 0.02f, 0.4f, 380, 0.25f, 3, 9);
    addTone(at(Sound::Crash), 0.05f, 0.4f, 910, 0.15f, 3, 11);
    addTone(at(Sound::Chat), 0, 0.05f, 1300, 0.2f, 0, 40);
    addTone(at(Sound::Chat), 0.05f, 0.06f, 1700, 0.2f, 0, 40);
    addTone(at(Sound::Arrested), 0, 0.3f, noteHz(50), 0.4f, 2, 4);
    addTone(at(Sound::Arrested), 0.3f, 0.6f, noteHz(45), 0.4f, 2, 3);
    addTone(at(Sound::Money), 0, 0.08f, noteHz(83), 0.3f, 1, 20);
    addTone(at(Sound::Money), 0.08f, 0.3f, noteHz(88), 0.3f, 1, 12);
    addSweep(at(Sound::Door), 0, 0.12f, 300, 140, 0.5f, 25);
    addNoise(at(Sound::Door), 0.05f, 0.08f, 0.4f, 60, 0.5f, 5);
    addTone(at(Sound::Countdown), 0, 0.25f, noteHz(72), 0.35f, 1, 8);
    addTone(at(Sound::Go), 0, 0.5f, noteHz(84), 0.35f, 1, 4);
    addNoise(at(Sound::Gunshot), 0, 0.12f, 1.0f, 30, 0.2f, 2);
    addSweep(at(Sound::Gunshot), 0, 0.08f, 220, 70, 0.6f, 30);
    addNoise(at(Sound::Explosion), 0, 1.2f, 1.0f, 3, 0.6f, 6);
    addSweep(at(Sound::Explosion), 0, 0.9f, 90, 30, 0.9f, 3);
    addNoise(at(Sound::Swoosh), 0, 0.35f, 0.5f, 8, 0.8f, 1);
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
        float v = (bassPhase * 2.0f - 1.0f) * 0.18f * static_cast<float>(std::exp(-eighth * 3.0));
        // Arpeggio: sixteenth notes through the chord.
        const int step = static_cast<int>(beat * 4.0) % 3;
        const double sixteenth = beat * 4.0 - std::floor(beat * 4.0);
        const float arpHz = noteHz(ch[step] + 12);
        v += (std::fmod(t * arpHz, 1.0) < 0.5 ? 0.06f : -0.06f) * static_cast<float>(std::exp(-sixteenth * 5.0));
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
            v += (static_cast<float>(h >> 16) / 32768.0f - 1.0f) * 0.12f * static_cast<float>(std::exp(-inBeat * 18.0));
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
            // Engine: two detuned saws with a rough edge; pitch follows speed.
            LoopState& e = loops[static_cast<int>(Loop::Engine)];
            e.volume += (e.targetVolume - e.volume) * 0.002f;
            if (e.volume > 0.001f) {
                const float hz = 38.0f * e.pitch;
                e.phase = std::fmod(e.phase + hz / kRate, 1.0f);
                e.phase2 = std::fmod(e.phase2 + hz * 1.51f / kRate, 1.0f);
                noiseState = noiseState * 1664525u + 1013904223u;
                const float noise = static_cast<float>(noiseState >> 16) / 32768.0f - 1.0f;
                v += ((e.phase * 2.0f - 1.0f) * 0.6f + (e.phase2 * 2.0f - 1.0f) * 0.3f + noise * 0.1f) * e.volume * 0.25f;
            }
            // Siren: wailing sine.
            LoopState& s = loops[static_cast<int>(Loop::Siren)];
            s.volume += (s.targetVolume - s.volume) * 0.002f;
            if (s.volume > 0.001f) {
                s.phase2 = std::fmod(s.phase2 + 0.45f / kRate, 1.0f);
                const float hz = 700.0f + 450.0f * std::sin(s.phase2 * kTau);
                s.phase = std::fmod(s.phase + hz / kRate, 1.0f);
                v += std::sin(s.phase * kTau) * s.volume * 0.18f;
            }
            LoopState& r = loops[static_cast<int>(Loop::Radio)];
            r.volume += (r.targetVolume - r.volume) * 0.001f;
            if (r.volume > 0.001f) {
                v += radio.sample() * r.volume;
            }
            v = std::tanh(v * m * 1.2f); // soft clip
            out[i] = static_cast<std::int16_t>(v * 30000.0f);
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
