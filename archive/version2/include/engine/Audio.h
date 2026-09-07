#pragma once
// ---------------------------------------------------------------------------
// Audio.h — sound effects computed in code, with no files and no extra library.
//
// The usual answer is SDL_mixer, but that exists for things this project
// doesn't do: decoding OGG/MP3, fading music, managing channels. Plain SDL2
// already opens an audio device and asks you for samples; for arcade blips,
// generating those samples is a few lines of arithmetic. Same trade as the
// bitmap font in Font.h — no dependency, no assets to lose, and you can see
// exactly how it works.
//
// How digital sound works, in one paragraph: the device wants a stream of
// numbers, 44,100 of them per second per channel, each the position of the
// speaker cone at that instant. A tone is that number swinging back and forth
// at some frequency — a sine wave sounds smooth, a square wave (just +1 and
// -1) sounds like an arcade cabinet, and pure randomness sounds like an
// explosion. That is the whole of the synthesis below.
//
// Two details that matter more than they look:
//
//   - The envelope. A tone that stops instantly leaves the cone somewhere
//     other than rest, and the jump to silence is heard as a click. Every
//     voice here fades in over a millisecond or two and out over the tail,
//     which is what makes it sound like a sound and not a pop.
//   - The callback runs on SDL's own audio thread, not yours. Anything it
//     touches is shared with the game, so the voice list is behind a mutex.
//     A real engine avoids locking there (audio underruns are audible); at
//     eight voices it is not measurable.
// ---------------------------------------------------------------------------

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <vector>

namespace engine {

enum class Waveform {
    Square,  // hollow and buzzy: shots, blips, menu clicks
    Sine,    // smooth and soft: thrust, tones
    Noise,   // random: explosions, impacts
};

class AudioDevice {
public:
    AudioDevice() {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            std::cerr << "Audio unavailable (" << SDL_GetError()
                      << "); the game will run silently.\n";
            return;
        }

        SDL_AudioSpec want{};
        want.freq = kSampleRate;
        want.format = AUDIO_S16SYS;  // 16-bit signed, native byte order
        want.channels = 1;           // mono is plenty for blips
        want.samples = 512;          // ~12ms of latency
        want.callback = &AudioDevice::feed;
        want.userdata = this;

        device_ = SDL_OpenAudioDevice(nullptr, 0, &want, &spec_, 0);
        if (device_ == 0) {
            std::cerr << "Could not open an audio device (" << SDL_GetError()
                      << "); the game will run silently.\n";
            return;
        }
        SDL_PauseAudioDevice(device_, 0);  // 0 means "start playing"
    }

    ~AudioDevice() {
        if (device_ != 0) SDL_CloseAudioDevice(device_);
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }

    AudioDevice(const AudioDevice&) = delete;
    AudioDevice& operator=(const AudioDevice&) = delete;

    // Starts a sound. Returns immediately — the audio thread does the work.
    // `volume` is 0..1, `frequency` is in hertz (ignored for Noise).
    //
    // With every voice busy the quietest is replaced, so a burst of explosions
    // drops the least noticeable sound rather than refusing the newest one.
    void play(Waveform waveform, float frequency, float seconds, float volume) {
        if (device_ == 0) return;

        Voice voice;
        voice.waveform = waveform;
        voice.frequency = frequency;
        voice.volume = volume;
        voice.samplesRemaining = static_cast<int>(seconds * kSampleRate);
        voice.samplesTotal = voice.samplesRemaining;
        if (voice.samplesRemaining <= 0) return;

        // Every noise voice started from the same seed, so every explosion was
        // bit-for-bit the same burst — noticeable once you have heard a few.
        // Moving the seed on per sound makes each one different.
        voice.noiseSeed = nextNoiseSeed_;
        nextNoiseSeed_ = nextNoiseSeed_ * 1664525u + 1013904223u;

        std::lock_guard<std::mutex> lock(mutex_);
        for (Voice& slot : voices_) {
            if (slot.samplesRemaining <= 0) {
                slot = voice;
                return;
            }
        }
        Voice* quietest = &voices_[0];
        for (Voice& slot : voices_) {
            if (slot.volume < quietest->volume) quietest = &slot;
        }
        *quietest = voice;
    }

    bool working() const { return device_ != 0; }

private:
    static constexpr int kSampleRate = 44100;
    static constexpr int kVoices = 8;

    struct Voice {
        Waveform waveform = Waveform::Square;
        float frequency = 440.0f;
        float volume = 0.0f;
        float phase = 0.0f;  // 0..1 through one cycle
        int samplesRemaining = 0;
        int samplesTotal = 0;
        std::uint32_t noiseSeed = 22695477u;
    };

    // Called by SDL on its own thread whenever the device needs more sound.
    static void feed(void* userdata, Uint8* stream, int lengthBytes) {
        static_cast<AudioDevice*>(userdata)->mix(
            reinterpret_cast<Sint16*>(stream),
            lengthBytes / static_cast<int>(sizeof(Sint16)));
    }

    // Sums every live voice into the buffer. Mixing really is addition: two
    // sounds at once is the two waves added together, which is also why
    // volumes have to stay modest or the total clips.
    void mix(Sint16* out, int sampleCount) {
        std::lock_guard<std::mutex> lock(mutex_);

        for (int i = 0; i < sampleCount; ++i) {
            float total = 0.0f;

            for (Voice& voice : voices_) {
                if (voice.samplesRemaining <= 0) continue;
                total += sampleOf(voice) * voice.volume * envelopeOf(voice);
                advance(voice);
            }

            if (total > 1.0f) total = 1.0f;
            if (total < -1.0f) total = -1.0f;
            out[i] = static_cast<Sint16>(total * 32000.0f);
        }
    }

    static float sampleOf(Voice& voice) {
        switch (voice.waveform) {
            case Waveform::Square:
                return voice.phase < 0.5f ? 1.0f : -1.0f;
            case Waveform::Sine:
                return std::sin(voice.phase * 6.2831853f);
            case Waveform::Noise:
                // A cheap repeatable pseudo-random number in -1..1. Real
                // randomness is not needed; it only has to be unpredictable
                // to the ear.
                voice.noiseSeed = voice.noiseSeed * 1103515245u + 12345u;
                return static_cast<float>((voice.noiseSeed >> 16) & 0x7FFF) /
                           16384.0f - 1.0f;
        }
        return 0.0f;
    }

    // Fade in over the first couple of milliseconds and out across the last
    // third, so the sound starts and ends without a click.
    static float envelopeOf(const Voice& voice) {
        const int elapsed = voice.samplesTotal - voice.samplesRemaining;

        // The attack has to fit inside the sound. A blip shorter than the
        // fade-in would otherwise end partway up the ramp, at a value well
        // away from silence — which is precisely the click the envelope
        // exists to prevent.
        const int attack = std::min(64, voice.samplesTotal / 4);
        if (attack > 0 && elapsed < attack) {
            return static_cast<float>(elapsed) / static_cast<float>(attack);
        }
        const float remaining = static_cast<float>(voice.samplesRemaining) /
                                static_cast<float>(voice.samplesTotal);
        return remaining < 0.35f ? remaining / 0.35f : 1.0f;
    }

    static void advance(Voice& voice) {
        voice.phase += voice.frequency / static_cast<float>(kSampleRate);
        if (voice.phase >= 1.0f) voice.phase -= 1.0f;
        --voice.samplesRemaining;
    }

    SDL_AudioDeviceID device_ = 0;
    SDL_AudioSpec spec_{};
    std::mutex mutex_;
    Voice voices_[kVoices];
    std::uint32_t nextNoiseSeed_ = 22695477u;
};

}  // namespace engine
