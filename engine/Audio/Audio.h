#pragma once

#include <memory>

namespace av {

enum class Sound {
    MenuMove, MenuSelect, Footstep, Punch, Hit, Pickup, MissionStart, MissionPass, MissionFail,
    Crash, Chat, Arrested, Money, Door, Countdown, Go, Gunshot, Explosion, Swoosh, Count
};

enum class Loop { Engine, Siren, Radio, Count };

// Tiny synthesiser + mixer. Every sound is generated in code at start-up
// (no asset files); a background thread mixes one-shots and looping voices
// (car engine whose pitch follows speed, police siren, a procedural
// synthwave radio) into the output device.
//
// Windows: waveOut. Other platforms: silent (available() == false).
class Audio {
public:
    Audio();
    ~Audio();
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    bool start(float masterVolume);
    bool available() const;
    void setVolume(float masterVolume); // 0 = mute

    void play(Sound sound, float volume = 1.0f, float pitch = 1.0f);
    // Volume 0 silences the loop (smoothly). Pitch 1 = default.
    void setLoop(Loop loop, float volume, float pitch = 1.0f);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace av
