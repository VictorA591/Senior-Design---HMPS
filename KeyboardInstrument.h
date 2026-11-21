#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <BinaryData.h>
#include <array>
#include <atomic>
#include <cmath>

// The same sound bank and voices are compiled into HMPS and the HMPS Keys VST3.
namespace hmps
{
inline constexpr const char* pianoCredit =
    "Piano: Salamander Grand Piano v3 — Alexander Holm (CC BY 3.0). "
    "Five original samples; playback limited to six seconds. "
    "https://creativecommons.org/licenses/by/3.0/";
inline const char* soundId (int index) { return index == 1 ? "synthLead" : "piano"; }
inline int soundIndex (const juce::String& id) { return id == "synthLead" ? 1 : 0; }

class KeyboardSound : public juce::SynthesiserSound
{
public:
    struct Sample { juce::AudioBuffer<float> audio; double rate = 48000; int root = 60; int start = 0; };
    std::array<Sample, 5> samples;
    explicit KeyboardSound (bool allChannels = false) : all (allChannels)
    {
        const char* data[] { BinaryData::C4_flac, BinaryData::Ds4_flac, BinaryData::Fs4_flac,
                            BinaryData::A4_flac, BinaryData::C5_flac };
        const int sizes[] { BinaryData::C4_flacSize, BinaryData::Ds4_flacSize, BinaryData::Fs4_flacSize,
                            BinaryData::A4_flacSize, BinaryData::C5_flacSize };
        const int roots[] { 60, 63, 66, 69, 72 };
        juce::FlacAudioFormat format;
        for (int i = 0; i < 5; ++i)
        {
            auto stream = std::make_unique<juce::MemoryInputStream> (data[i], static_cast<size_t> (sizes[i]), false);
            std::unique_ptr<juce::AudioFormatReader> reader (format.createReaderFor (stream.release(), true));
            if (! reader) continue;
            auto& sample = samples[i];
            sample.root = roots[i];
            sample.rate = reader->sampleRate;
            const int length = static_cast<int> (juce::jmin<juce::int64> (reader->lengthInSamples,
                static_cast<juce::int64> (sample.rate * 6)));
            sample.audio.setSize (2, length + 1);
            sample.audio.clear();
            reader->read (&sample.audio, 0, length, 0, true, true);
            // Remove only very quiet leading frames; keep the hammer transient.
            while (sample.start < juce::jmin (length - 1, static_cast<int> (sample.rate * .08))
                && std::abs (sample.audio.getSample (0, sample.start)) < .0001f
                && std::abs (sample.audio.getSample (1, sample.start)) < .0001f)
                ++sample.start;
            const int fade = juce::jmin (length, static_cast<int> (sample.rate * .1));
            sample.audio.applyGainRamp (length - fade, fade, 1, 0);
        }
    }
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int channel) override { return all || channel < 10 || channel > 12; }
    bool loaded() const
    {
        for (const auto& sample : samples) if (sample.audio.getNumSamples() <= 1) return false;
        return true;
    }
private:
    bool all;
};

class KeyboardVoice : public juce::SynthesiserVoice
{
public:
    KeyboardVoice (std::array<std::atomic<float>, 16>& levels,
                   std::array<std::atomic<int>, 16>& sounds) : gains (levels), choices (sounds) {}
    bool canPlaySound (juce::SynthesiserSound* sound) override { return dynamic_cast<KeyboardSound*> (sound) != nullptr; }
    void startNote (int note, float velocity, juce::SynthesiserSound* base, int pitchWheel) override
    {
        bank = dynamic_cast<KeyboardSound*> (base);
        channel = 0;
        for (int i = 1; i <= 16; ++i) if (isPlayingChannel (i)) { channel = i - 1; break; }
        preset = choices[channel].load();
        gain = gains[channel].load();
        smooth = static_cast<float> (1 - std::exp (-1 / (getSampleRate() * .005)));
        amplitude = velocity * (preset == 0 ? .32f : .06f);
        phase = 0;
        secondPhase = 0;
        frequency = juce::MidiMessage::getMidiNoteInHertz (note);
        sampleIndex = juce::jlimit (0, 4, static_cast<int> (std::lround ((note - 60) / 3.0)));
        position = bank->samples[sampleIndex].start;
        ratio = bank->samples[sampleIndex].rate / getSampleRate()
              * std::pow (2.0, (note - bank->samples[sampleIndex].root) / 12.0);
        pitchWheelMoved (pitchWheel);
        envelope.setSampleRate (getSampleRate());
        envelope.setParameters (preset == 0 ? juce::ADSR::Parameters { .001f, .01f, 1.0f, .20f }
                                           : juce::ADSR::Parameters { .003f, .02f, 1.0f, .10f });
        envelope.noteOn();
    }
    void stopNote (float, bool tail) override
    {
        if (tail) envelope.noteOff();
        else { envelope.reset(); clearCurrentNote(); }
    }
    void pitchWheelMoved (int wheel) override { bend = std::pow (2.0, ((wheel - 8192) / 8192.0) * 2.0 / 12.0); }
    void controllerMoved (int, int) override {}
    void renderNextBlock (juce::AudioBuffer<float>& output, int start, int count) override
    {
        if (! isVoiceActive() || bank == nullptr) return;
        const float target = gains[channel].load();
        const auto& sample = bank->samples[sampleIndex].audio;
        for (int i = start; i < start + count; ++i)
        {
            gain += (target - gain) * smooth;
            const float level = amplitude * envelope.getNextSample() * gain;
            if (preset == 0)
            {
                const int frame = static_cast<int> (position);
                if (frame + 1 >= sample.getNumSamples()) { clearCurrentNote(); break; }
                const float fraction = static_cast<float> (position - frame);
                for (int c = 0; c < output.getNumChannels(); ++c)
                {
                    const auto* data = sample.getReadPointer (juce::jmin (c, 1));
                    output.addSample (c, i, (data[frame] + fraction * (data[frame + 1] - data[frame])) * level);
                }
                position += ratio * bend;
            }
            else
            {
                const double sampleRate = getSampleRate();
                const double twoPi = juce::MathConstants<double>::twoPi;

                // Second oscillator is approximately six cents sharper.
                // This is subtle detuning, not a different musical note.
                constexpr double detuneRatio = 1.0034717485;

                const double firstFrequency = frequency * bend;
                const double secondFrequency = firstFrequency * detuneRatio;

                const float firstOscillator =
                    leadWave (phase, firstFrequency, sampleRate);

                const float secondOscillator =
                    leadWave (secondPhase, secondFrequency, sampleRate);

                // Average the oscillators so adding another does not double gain.
                const float value =
                    0.8f * firstOscillator + 0.2f * secondOscillator;

                for (int c = 0; c < output.getNumChannels(); ++c)
                    output.addSample (c, i, value * level);

                phase = std::fmod (
                    phase + twoPi * firstFrequency / sampleRate,
                    twoPi);

                secondPhase = std::fmod (
                    secondPhase + twoPi * secondFrequency / sampleRate,
                    twoPi);
            }
            if (! envelope.isActive()) { clearCurrentNote(); break; }
        }
    }
private:
    static float leadWave (
        double oscillatorPhase,
        double oscillatorFrequency,
        double sampleRate)
    {
        constexpr double dutyCycle = 0.25;
        constexpr int maxHarmonics = 16;

        const double pi = juce::MathConstants<double>::pi;
        double value = 0.0;

        for (int harmonic = 1; harmonic <= maxHarmonics; ++harmonic)
        {
            if (harmonic * oscillatorFrequency >= sampleRate * 0.45)
                break;

            const double coefficient =
                4.0 * std::sin (pi * harmonic * dutyCycle)
                / (pi * harmonic);

            value += coefficient
                * std::cos (harmonic * oscillatorPhase);
        }

        return static_cast<float> (value * 0.6);
    }
    std::array<std::atomic<float>, 16>& gains;
    std::array<std::atomic<int>, 16>& choices;
    KeyboardSound* bank = nullptr;
    juce::ADSR envelope;
    int channel = 0, preset = 0, sampleIndex = 0;
    double phase = 0;
    double secondPhase = 0;
    double frequency = 440;
    double position = 0;
    double ratio = 1;
    double bend = 1;
    float amplitude = 0, gain = 1, smooth = .004f;
};

}
