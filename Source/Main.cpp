#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_osc/juce_osc.h>
#include "FsmModel.h"
#include "SuperColliderHost.h"

#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <future>
#include <limits>
#include <memory>
#include <mutex>
#include <numeric>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace
{
juce::Colour backgroundTop() { return juce::Colour (0xff12161b); }
juce::Colour backgroundBottom() { return juce::Colour (0xff181d23); }
juce::Colour ink() { return juce::Colour (0xfff3efe6); }
juce::Colour mutedInk() { return juce::Colour (0xffabb3bd); }
juce::Colour accentA() { return juce::Colour (0xffffd84d); }
juce::Colour accentB() { return juce::Colour (0xff35e7f2); }
juce::Colour accentC() { return juce::Colour (0xffff6f96); }
juce::Colour inspectedFill() { return juce::Colour (0xff1a2428); }
juce::Colour panelFill() { return juce::Colour (0xff171c22); }
juce::Colour rowFill() { return juce::Colour (0xff222a31); }
juce::Colour hairline() { return juce::Colour (0xff3a4650); }
bool colourblindSafePalette = false;

void setColourblindSafePalette (bool shouldUse)
{
    colourblindSafePalette = shouldUse;
}

juce::Colour paletteColour (int index)
{
    static constexpr juce::uint32 rainbowColours[] =
    {
        0xffffd23f, 0xff49cfff, 0xff77e58d, 0xffff9a52,
        0xffff6f9a, 0xff7bb7ff, 0xfff2ea57, 0xff48e0b6
    };

    // Okabe-Ito inspired colours, chosen to stay distinct for common colour vision deficiencies.
    static constexpr juce::uint32 safeColours[] =
    {
        0xffe69f00, 0xff56b4e9, 0xff009e73, 0xfff0e442,
        0xff0072b2, 0xffd55e00, 0xffcc79a7, 0xff999999
    };

    const auto* colours = colourblindSafePalette ? safeColours : rainbowColours;
    const auto count = static_cast<int> (std::size (rainbowColours));
    const auto wrapped = (index % count + count) % count;
    return juce::Colour (colours[static_cast<size_t> (wrapped)]);
}

juce::Colour graphColour (int index, int offset = 0)
{
    return paletteColour (index + offset);
}

juce::Colour transitionColourFor (int index)
{
    return graphColour (index).interpolatedWith (mutedInk(), 0.54f);
}

bool isLaneDeleteKey (const juce::KeyPress& key)
{
    const auto keyCode = key.getKeyCode();
    return keyCode == juce::KeyPress::backspaceKey || keyCode == juce::KeyPress::deleteKey;
}

class OfLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    OfLookAndFeel()
    {
        setColour (juce::TextButton::buttonColourId, rowFill().withAlpha (0.92f));
        setColour (juce::TextButton::buttonOnColourId, rowFill().interpolatedWith (accentA(), 0.14f));
        setColour (juce::TextButton::textColourOffId, mutedInk().withAlpha (0.92f));
        setColour (juce::TextButton::textColourOnId, ink());
        setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff101418));
        setColour (juce::ComboBox::outlineColourId, hairline().withAlpha (0.70f));
        setColour (juce::ComboBox::textColourId, ink().withAlpha (0.92f));
        setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff101418));
        setColour (juce::TextEditor::outlineColourId, hairline().withAlpha (0.65f));
        setColour (juce::TextEditor::focusedOutlineColourId, accentB().withAlpha (0.74f));
        setColour (juce::TextEditor::textColourId, ink());
        setColour (juce::Slider::thumbColourId, accentB().withAlpha (0.76f));
        setColour (juce::Slider::trackColourId, hairline().withAlpha (0.55f));
        setColour (juce::Slider::backgroundColourId, juce::Colour (0xff0f1317));
    }

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
    {
        return juce::Font (juce::FontOptions (juce::jlimit (10.5f, 13.0f, static_cast<float> (buttonHeight) * 0.42f),
                                             juce::Font::bold));
    }

    void drawButtonBackground (juce::Graphics& g,
                               juce::Button& button,
                               const juce::Colour& backgroundColour,
                               bool highlighted,
                               bool down) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced (0.75f);
        const auto active = button.getToggleState();
        const auto base = backgroundColour
            .interpolatedWith (ink(), highlighted ? 0.045f : 0.0f)
            .interpolatedWith (juce::Colour (0xff07090c), down ? 0.12f : 0.0f);

        g.setColour (base.withAlpha (active ? 0.98f : 0.88f));
        g.fillRoundedRectangle (bounds, 4.0f);
        g.setColour ((active ? accentA() : hairline()).withAlpha (highlighted || active ? 0.68f : 0.46f));
        g.drawRoundedRectangle (bounds, 4.0f, active ? 1.05f : 0.75f);
    }

    void drawComboBox (juce::Graphics& g, int width, int height, bool buttonDown,
                       int, int, int, int, juce::ComboBox& box) override
    {
        auto bounds = juce::Rectangle<float> (0.75f, 0.75f, static_cast<float> (width) - 1.5f, static_cast<float> (height) - 1.5f);
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId).withAlpha (buttonDown ? 0.98f : 0.90f));
        g.fillRoundedRectangle (bounds, 4.0f);
        g.setColour (box.findColour (juce::ComboBox::outlineColourId).withAlpha (buttonDown ? 0.88f : 0.62f));
        g.drawRoundedRectangle (bounds, 4.0f, 0.75f);

        const auto arrow = juce::Rectangle<float> (static_cast<float> (width - 18), static_cast<float> (height) * 0.5f - 3.0f, 8.0f, 6.0f);
        juce::Path chevron;
        chevron.startNewSubPath (arrow.getX(), arrow.getY());
        chevron.lineTo (arrow.getCentreX(), arrow.getBottom());
        chevron.lineTo (arrow.getRight(), arrow.getY());
        g.setColour (mutedInk().withAlpha (0.72f));
        g.strokePath (chevron, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& editor) override
    {
        auto bounds = juce::Rectangle<float> (0.75f, 0.75f, static_cast<float> (width) - 1.5f, static_cast<float> (height) - 1.5f);
        g.setColour (editor.findColour (juce::TextEditor::backgroundColourId).withAlpha (0.92f));
        g.fillRoundedRectangle (bounds, 3.5f);
    }

    void drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor) override
    {
        auto bounds = juce::Rectangle<float> (0.75f, 0.75f, static_cast<float> (width) - 1.5f, static_cast<float> (height) - 1.5f);
        g.setColour ((editor.hasKeyboardFocus (true) ? editor.findColour (juce::TextEditor::focusedOutlineColourId)
                                                     : editor.findColour (juce::TextEditor::outlineColourId))
                         .withAlpha (editor.hasKeyboardFocus (true) ? 0.88f : 0.62f));
        g.drawRoundedRectangle (bounds, 3.5f, editor.hasKeyboardFocus (true) ? 1.05f : 0.75f);
    }
};

juce::String laneDurationModeToString (LaneDurationMode mode)
{
    switch (mode)
    {
        case LaneDurationMode::endOfBeat: return "beat";
        case LaneDurationMode::endOfBar: return "bar";
        case LaneDurationMode::fixedBars: return "bars";
        case LaneDurationMode::fixedSeconds: return "seconds";
        case LaneDurationMode::natural: return "natural";
    }

    return "natural";
}

LaneDurationMode laneDurationModeFromString (const juce::String& value)
{
    if (value == "beat") return LaneDurationMode::endOfBeat;
    if (value == "bar") return LaneDurationMode::endOfBar;
    if (value == "bars") return LaneDurationMode::fixedBars;
    if (value == "seconds") return LaneDurationMode::fixedSeconds;
    return LaneDurationMode::natural;
}

juce::String orbitConnectionActionToString (OrbitConnectionAction action)
{
    switch (action)
    {
        case OrbitConnectionAction::start: return "start";
        case OrbitConnectionAction::pause: return "pause";
        case OrbitConnectionAction::restart: return "restart";
        case OrbitConnectionAction::reverse: return "reverse";
        case OrbitConnectionAction::programmable: return "programmable";
    }

    return "start";
}

OrbitConnectionAction orbitConnectionActionFromString (const juce::String& value)
{
    if (value == "stop" || value == "pause") return OrbitConnectionAction::pause;
    if (value == "restart") return OrbitConnectionAction::restart;
    if (value == "reverse") return OrbitConnectionAction::reverse;
    if (value == "programmable") return OrbitConnectionAction::programmable;
    return OrbitConnectionAction::start;
}

juce::String orbitConnectionActionLabel (OrbitConnectionAction action)
{
    switch (action)
    {
        case OrbitConnectionAction::start: return "Start";
        case OrbitConnectionAction::pause: return "Pause";
        case OrbitConnectionAction::restart: return "Restart";
        case OrbitConnectionAction::reverse: return "Reverse";
        case OrbitConnectionAction::programmable: return "Fabric";
    }

    return "Start";
}

class RenderedAudioPlayer final : public juce::AudioIODeviceCallback
{
public:
    RenderedAudioPlayer()
    {
        formatManager.registerBasicFormats();
    }

    void audioDeviceAboutToStart (juce::AudioIODevice* device) override
    {
        const juce::ScopedLock lock (audioLock);
        deviceSampleRate = device != nullptr ? juce::jmax (1.0, device->getCurrentSampleRate()) : 44100.0;
        transportSample = 0;
        voices.clear();
    }

    void audioDeviceStopped() override
    {
        const juce::ScopedLock lock (audioLock);
        voices.clear();
        running = false;
        transportSample = 0;
    }

    void audioDeviceIOCallbackWithContext (const float* const*, int,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override
    {
        for (int channel = 0; channel < numOutputChannels; ++channel)
            if (outputChannelData[channel] != nullptr)
                juce::FloatVectorOperations::clear (outputChannelData[channel], numSamples);

        const juce::ScopedLock lock (audioLock);
        const auto blockStart = transportSample;
        transportSample += numSamples;

        if (! running)
            return;

        const auto blockEnd = blockStart + numSamples;
        for (auto& voice : voices)
        {
            if (! voice.active || voice.file == nullptr || blockEnd <= voice.startSample)
                continue;

            const auto offset = static_cast<int> (juce::jmax<juce::int64> (0, voice.startSample - blockStart));
            const auto available = numSamples - offset;
            if (available <= 0)
                continue;

            const auto remaining = static_cast<int> (juce::jmin<juce::int64> (available, voice.maxOutputSamples - voice.outputSamplesPlayed));
            if (remaining <= 0)
            {
                voice.active = false;
                continue;
            }

            const auto leftGain = voice.gain * masterGain * (voice.pan <= 0.0f ? 1.0f : 1.0f - voice.pan);
            const auto rightGain = voice.gain * masterGain * (voice.pan >= 0.0f ? 1.0f : 1.0f + voice.pan);
            const auto sourceChannels = voice.file->buffer.getNumChannels();
            const auto sourceSamples = voice.file->buffer.getNumSamples();

            for (int i = 0; i < remaining; ++i)
            {
                const auto outputIndex = voice.outputSamplesPlayed;
                if (voice.sourcePosition < 0.0 || voice.sourcePosition >= static_cast<double> (sourceSamples))
                {
                    voice.active = false;
                    break;
                }

                const auto sourceIndex = static_cast<int> (voice.sourcePosition);
                if (sourceIndex < 0 || sourceIndex >= sourceSamples)
                {
                    voice.active = false;
                    break;
                }

                const auto nextIndex = voice.reverse ? juce::jmax (0, sourceIndex - 1)
                                                     : juce::jmin (sourceSamples - 1, sourceIndex + 1);
                const auto alpha = static_cast<float> (voice.sourcePosition - static_cast<double> (sourceIndex));
                const auto left = interpolatedSample (voice.file->buffer, 0, sourceIndex, nextIndex, alpha);
                const auto right = sourceChannels > 1 ? interpolatedSample (voice.file->buffer, 1, sourceIndex, nextIndex, alpha) : left;
                const auto out = offset + i;
                auto fadeGain = 1.0f;
                if (voice.fadeInSamples > 0)
                    fadeGain = juce::jmin (fadeGain, juce::jlimit (0.0f, 1.0f, static_cast<float> (outputIndex) / static_cast<float> (voice.fadeInSamples)));
                if (voice.fadeOutSamples > 0)
                    fadeGain = juce::jmin (fadeGain, juce::jlimit (0.0f, 1.0f, static_cast<float> (voice.maxOutputSamples - outputIndex) / static_cast<float> (voice.fadeOutSamples)));

                if (numOutputChannels > 0 && outputChannelData[0] != nullptr)
                    outputChannelData[0][out] += left * leftGain * fadeGain;

                if (numOutputChannels > 1 && outputChannelData[1] != nullptr)
                    outputChannelData[1][out] += right * rightGain * fadeGain;
                else if (numOutputChannels > 0 && outputChannelData[0] != nullptr)
                    outputChannelData[0][out] += right * rightGain * fadeGain;

                voice.sourcePosition += voice.sourceIncrement;
                ++voice.outputSamplesPlayed;
            }

            if (voice.outputSamplesPlayed >= voice.maxOutputSamples
                || (! voice.reverse && voice.sourcePosition >= sourceSamples)
                || (voice.reverse && voice.sourcePosition < 0.0))
                voice.active = false;
        }

        voices.erase (std::remove_if (voices.begin(), voices.end(), [] (const Voice& voice)
        {
            return ! voice.active;
        }), voices.end());
    }

    void start()
    {
        const juce::ScopedLock lock (audioLock);
        voices.clear();
        transportSample = 0;
        running = true;
    }

    void stopAll()
    {
        const juce::ScopedLock lock (audioLock);
        voices.clear();
        running = false;
    }

    void stopLane (const juce::String& laneId)
    {
        const juce::ScopedLock lock (audioLock);
        for (auto& voice : voices)
            if (voice.laneId == laneId)
                voice.active = false;
    }

    bool scheduleLane (const Lane& lane, double delaySeconds, float gain, double maxDurationSeconds, bool reverse = false, double sourceOffsetSeconds = 0.0)
    {
        auto file = loadFile (lane.frozenAudioPath);
        if (file == nullptr)
            return false;

        const juce::ScopedLock lock (audioLock);
        const auto startOffset = static_cast<juce::int64> (juce::jmax (0.0, delaySeconds) * deviceSampleRate);
        Voice voice;
        voice.laneId = lane.id;
        voice.file = std::move (file);
        voice.startSample = transportSample + startOffset;
        voice.reverse = reverse;
        const auto sourceIncrement = voice.file->sampleRate / deviceSampleRate;
        voice.sourceIncrement = reverse ? -sourceIncrement : sourceIncrement;
        const auto sourceOffset = juce::jlimit (0.0,
                                                static_cast<double> (voice.file->buffer.getNumSamples() - 1),
                                                juce::jmax (0.0, sourceOffsetSeconds) * voice.file->sampleRate);
        const auto availableSourceSamples = reverse ? sourceOffset + 1.0
                                                    : static_cast<double> (voice.file->buffer.getNumSamples()) - sourceOffset;
        const auto fileOutputSamples = static_cast<juce::int64> ((availableSourceSamples / sourceIncrement) + 0.5);
        const auto remainingDurationSeconds = reverse
            ? (sourceOffsetSeconds > 0.0 ? sourceOffsetSeconds : maxDurationSeconds)
            : (maxDurationSeconds - juce::jmax (0.0, sourceOffsetSeconds));
        const auto durationOutputSamples = static_cast<juce::int64> (juce::jmax (0.01, remainingDurationSeconds) * deviceSampleRate + 0.5);
        voice.maxOutputSamples = juce::jmin (fileOutputSamples, durationOutputSamples);
        if (reverse)
            voice.sourcePosition = sourceOffsetSeconds > 0.0
                ? sourceOffset
                : juce::jlimit (0.0,
                                static_cast<double> (juce::jmax (0, voice.file->buffer.getNumSamples() - 1)),
                                (static_cast<double> (voice.maxOutputSamples - 1) * sourceIncrement));
        else
            voice.sourcePosition = sourceOffset;
        const auto maxFadeSamples = juce::jmax<juce::int64> (0, voice.maxOutputSamples / 2);
        voice.fadeInSamples = juce::jlimit<juce::int64> (0, maxFadeSamples, static_cast<juce::int64> (juce::jmax (0.0, lane.fadeInSeconds) * deviceSampleRate + 0.5));
        voice.fadeOutSamples = juce::jlimit<juce::int64> (0, maxFadeSamples, static_cast<juce::int64> (juce::jmax (0.0, lane.fadeOutSeconds) * deviceSampleRate + 0.5));
        voice.gain = gain;
        voice.pan = juce::jlimit (-1.0f, 1.0f, lane.pan);
        voices.push_back (std::move (voice));
        running = true;
        return true;
    }

    void setLaneMix (const juce::String& laneId, float gain, float pan)
    {
        const juce::ScopedLock lock (audioLock);
        for (auto& voice : voices)
        {
            if (voice.laneId == laneId)
            {
                voice.gain = gain;
                voice.pan = juce::jlimit (-1.0f, 1.0f, pan);
            }
        }
    }

    void setMasterGain (float gain)
    {
        const juce::ScopedLock lock (audioLock);
        masterGain = juce::jlimit (0.0f, 5.0f, gain);
    }

private:
    struct RenderedFile
    {
        juce::AudioBuffer<float> buffer;
        double sampleRate = 44100.0;
        juce::int64 size = 0;
        juce::Time modified;
    };

    struct Voice
    {
        juce::String laneId;
        std::shared_ptr<RenderedFile> file;
        juce::int64 startSample = 0;
        juce::int64 outputSamplesPlayed = 0;
        juce::int64 maxOutputSamples = 0;
        juce::int64 fadeInSamples = 0;
        juce::int64 fadeOutSamples = 0;
        double sourcePosition = 0.0;
        double sourceIncrement = 1.0;
        float gain = 1.0f;
        float pan = 0.0f;
        bool reverse = false;
        bool active = true;
    };

    static float interpolatedSample (const juce::AudioBuffer<float>& buffer, int channel, int index, int nextIndex, float alpha)
    {
        const auto* data = buffer.getReadPointer (channel);
        return data[index] + (data[nextIndex] - data[index]) * alpha;
    }

    std::shared_ptr<RenderedFile> loadFile (const juce::String& path)
    {
        if (path.isEmpty())
            return {};

        const auto file = juce::File (path);
        const auto key = path.toStdString();
        if (auto found = cache.find (key); found != cache.end()
            && found->second->size == file.getSize()
            && found->second->modified == file.getLastModificationTime())
        {
            return found->second;
        }

        auto reader = std::unique_ptr<juce::AudioFormatReader> (formatManager.createReaderFor (file));
        if (reader == nullptr || reader->lengthInSamples <= 0)
            return {};

        auto rendered = std::make_shared<RenderedFile>();
        rendered->sampleRate = reader->sampleRate > 0.0 ? reader->sampleRate : 44100.0;
        rendered->size = file.getSize();
        rendered->modified = file.getLastModificationTime();
        rendered->buffer.setSize (static_cast<int> (juce::jmax<juce::uint32> (1, reader->numChannels)),
                                  static_cast<int> (juce::jmin<juce::int64> (reader->lengthInSamples, std::numeric_limits<int>::max())));
        reader->read (&rendered->buffer, 0, rendered->buffer.getNumSamples(), 0, true, true);
        cache[key] = rendered;
        return rendered;
    }

    juce::CriticalSection audioLock;
    juce::AudioFormatManager formatManager;
    std::unordered_map<std::string, std::shared_ptr<RenderedFile>> cache;
    std::vector<Voice> voices;
    double deviceSampleRate = 44100.0;
    juce::int64 transportSample = 0;
    float masterGain = 1.0f;
    bool running = false;
};
} // namespace

struct LaneMeterValues
{
    float rms = 0.0f;
    float peak = 0.0f;
    bool live = false;
};

class GraphComponent final : public juce::Component,
                             private juce::Timer
{
public:
    std::function<void (int)> onStateChosen;
    std::function<void (int)> onNestedBadgeChosen;
    std::function<void (int, int)> onNestedStateChosen;
    std::function<void (int, int, int)> onSecondLayerNestedStateChosen;
    std::function<void (int, int)> onNestedStateCountChanged;
    std::function<void (int, int, int)> onSecondLayerNestedStateCountChanged;
    std::function<void()> onNodeLayoutChanged;

    void setInspectedMachine (MachineModel* inspected)
    {
        if (inspectedMachine == inspected)
            return;

        inspectedMachine = inspected;
        repaint();
    }

    explicit GraphComponent (MachineModel& machineToUse) : machine (&machineToUse)
    {
        setWantsKeyboardFocus (true);
        startTimerHz (60);
    }

    void setMachine (MachineModel& machineToUse)
    {
        if (machine != &machineToUse)
        {
            manualNodeOffsets.clear();
            clearNodePositionLock();
            fitView();
        }

        machine = &machineToUse;
        syncManualOffsetsFromMachine();
        repaint();
    }

    void setTimingPulse (const juce::String& machineIdToUse, int stateIndexToUse, float phaseToUse, int beatIndexToUse, int beatCountToUse)
    {
        pulseMachineId = machineIdToUse;
        pulseStateIndex = stateIndexToUse;
        pulsePhase = juce::jlimit (0.0f, 1.0f, phaseToUse);
        pulseBeatIndex = beatIndexToUse;
        pulseBeatCount = juce::jmax (1, beatCountToUse);
        pulseReceivedMs = juce::Time::getMillisecondCounterHiRes();
        repaint();
    }

    void clearTimingPulse()
    {
        pulseMachineId.clear();
        pulseStateIndex = -1;
        pulsePhase = 0.0f;
        pulseReceivedMs = 0.0;
        previewStateIndex = -1;
        previewProbability = 0.0f;
        repaint();
    }

    void setTransitionPreview (int stateIndex, float probability)
    {
        probability = juce::jlimit (0.0f, 1.0f, probability);
        if (previewStateIndex == stateIndex && std::abs (previewProbability - probability) < 0.001f)
            return;

        previewStateIndex = stateIndex;
        previewProbability = probability;
        repaint();
    }

    void fitView()
    {
        zoom = 1.0f;
        panOffset = {};
        repaint();
    }

    void resetLayout()
    {
        finishNestedStateCountEdit (false);
        ensureManualOffsetSize();
        std::fill (manualNodeOffsets.begin(), manualNodeOffsets.end(), juce::Point<float> {});
        syncManualOffsetsToMachine();
        clearNodePositionLock();
        fitView();
        if (onNodeLayoutChanged)
            onNodeLayoutChanged();
    }

    void beginNodePositionLock()
    {
        finishNestedStateCountEdit (false);
        nodePositionLockActive = false;
        layoutStatesNormal (true, true);
        lockedScreenPositions = statePositions;
        lockedScreenRadius = stateRadius;
        nodePositionLockActive = ! lockedScreenPositions.empty();
    }

    void endNodePositionLock()
    {
        if (! nodePositionLockActive)
            return;

        auto lockedPositions = lockedScreenPositions;
        clearNodePositionLock();

        layoutStatesNormal (false, false);
        auto basePositions = statePositions;
        ensureManualOffsetSize();

        const auto count = juce::jmin (static_cast<int> (basePositions.size()),
                                       static_cast<int> (lockedPositions.size()));
        for (int i = 0; i < count; ++i)
            manualNodeOffsets[static_cast<size_t> (i)] = screenToGraph (lockedPositions[static_cast<size_t> (i)])
                                                       - basePositions[static_cast<size_t> (i)];

        syncManualOffsetsToMachine();
        repaint();
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        const auto keyCode = key.getKeyCode();

        if (keyCode == 'f' || keyCode == 'F')
        {
            fitView();
            return true;
        }

        if (keyCode == 'r' || keyCode == 'R')
        {
            resetLayout();
            return true;
        }

        return false;
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (backgroundTop().interpolatedWith (backgroundBottom(), 0.55f));
        g.fillRoundedRectangle (bounds.reduced (2.0f), 6.0f);
        g.setColour (hairline().withAlpha (0.26f));
        g.drawRoundedRectangle (bounds.reduced (2.0f), 6.0f, 1.0f);

        layoutStates();
        drawRules (g);
        drawStates (g);
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        grabKeyboardFocus();
        dragStart = event.position;
        panStart = panOffset;
        draggingStateIndex = -1;
        draggedState = false;

        if (event.mods.isPopupMenu() || event.mods.isMiddleButtonDown() || event.mods.isAltDown())
            return;

        for (int i = 0; i < static_cast<int> (statePositions.size()); ++i)
        {
            if (auto* child = machine->childMachine (i))
            {
                const auto secondLayerBadge = hitTestSecondLayerBadge (*child, statePositions[static_cast<size_t> (i)], event.position);
                if (secondLayerBadge >= 0)
                {
                    if (auto* grandchild = child->childMachine (secondLayerBadge))
                        if (onSecondLayerNestedStateChosen)
                            onSecondLayerNestedStateChosen (i, secondLayerBadge, grandchild->selectedState);
                    return;
                }

                const auto secondLayerState = hitTestSecondLayerNestedState (*child, statePositions[static_cast<size_t> (i)], event.position);
                if (secondLayerState.first >= 0 && secondLayerState.second >= 0)
                {
                    if (onSecondLayerNestedStateChosen)
                        onSecondLayerNestedStateChosen (i, secondLayerState.first, secondLayerState.second);
                    return;
                }

                const auto childState = hitTestNestedState (*child, statePositions[static_cast<size_t> (i)], event.position);
                if (childState >= 0)
                {
                    if (onNestedStateChosen)
                        onNestedStateChosen (i, childState);
                    return;
                }
            }
        }

        for (int i = 0; i < static_cast<int> (statePositions.size()); ++i)
        {
            if (machine->childMachine (i) != nullptr
                && getNestedBadgeBounds (*machine->childMachine (i), statePositions[static_cast<size_t> (i)]).contains (event.position))
            {
                if (onNestedBadgeChosen)
                    onNestedBadgeChosen (i);
                return;
            }
        }

        for (int i = 0; i < static_cast<int> (statePositions.size()); ++i)
        {
            if (statePositions[static_cast<size_t> (i)].getDistanceFrom (event.position) < stateRadius)
            {
                ensureManualOffsetSize();
                draggingStateIndex = i;
                nodeOffsetStart = manualNodeOffsets[static_cast<size_t> (i)];
                machine->selectedState = i;
                machine->selectedLane = 0;
                if (onStateChosen)
                    onStateChosen (i);
                repaint();
                return;
            }
        }

    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        const auto delta = event.position - dragStart;
        if (delta.getDistanceFromOrigin() < 2.0f)
            return;

        if (draggingStateIndex >= 0)
        {
            ensureManualOffsetSize();
            const auto graphDelta = delta / juce::jmax (0.001f, zoom);
            manualNodeOffsets[static_cast<size_t> (draggingStateIndex)] = nodeOffsetStart + graphDelta;
            syncManualOffsetsToMachine();
            draggedState = true;
        }
        else
        {
            panOffset = panStart + delta;
        }

        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (draggedState)
        {
            syncManualOffsetsToMachine();
            if (onNodeLayoutChanged)
                onNodeLayoutChanged();
        }

        draggingStateIndex = -1;
        draggedState = false;
    }

    void mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
    {
        const auto cursor = event.position;
        const auto centre = getLocalBounds().toFloat().getCentre();
        const auto before = screenToGraph (cursor);
        const auto wheelDelta = std::abs (wheel.deltaY) > std::abs (wheel.deltaX) ? wheel.deltaY : wheel.deltaX;
        const auto zoomFactor = std::pow (1.18f, wheelDelta * 6.0f);
        const auto newZoom = juce::jlimit (0.55f, 2.8f, zoom * zoomFactor);

        if (std::abs (newZoom - zoom) < 0.001f)
            return;

        zoom = newZoom;
        panOffset = cursor - centre - ((before - centre) * zoom);
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& event) override
    {
        for (int i = 0; i < static_cast<int> (statePositions.size()); ++i)
        {
            if (auto* child = machine->childMachine (i))
            {
                const auto badgeHit = hitTestSecondLayerBadge (*child, statePositions[static_cast<size_t> (i)], event.position);
                if (badgeHit >= 0)
                {
                    startSecondLayerStateCountEdit (i, badgeHit, getSecondLayerBadgeBounds (*child->childMachine (badgeHit),
                                                                                             getNestedStatePoint (*child, statePositions[static_cast<size_t> (i)], badgeHit)));
                    return;
                }
            }

            if (machine->childMachine (i) != nullptr
                && getNestedBadgeBounds (*machine->childMachine (i), statePositions[static_cast<size_t> (i)]).contains (event.position))
            {
                startNestedStateCountEdit (i, getNestedBadgeBounds (*machine->childMachine (i), statePositions[static_cast<size_t> (i)]));
                return;
            }
        }
    }

private:
    void layoutStates()
    {
        if (nodePositionLockActive && lockedScreenPositions.size() == static_cast<size_t> (machine->getStateCount()))
        {
            statePositions = lockedScreenPositions;
            stateRadius = lockedScreenRadius;
            return;
        }

        layoutStatesNormal (true, true);
    }

    void layoutStatesNormal (bool includeManualOffsets, bool applyViewTransform)
    {
        const auto count = machine->getStateCount();
        statePositions.resize (static_cast<size_t> (count));
        ensureManualOffsetSize();

        stateRadius = juce::jmap (static_cast<float> (count), 1.0f, static_cast<float> (maxStateCount), 54.0f, 34.0f);
        stateRadius = juce::jlimit (34.0f, 54.0f, stateRadius);

        const auto clockLayout = isRootClockLayout();
        const auto outerMargin = clockLayout ? stateRadius * 1.75f : getOuterNodeExtent() + 28.0f;
        auto area = getLocalBounds().toFloat().reduced (outerMargin, clockLayout ? stateRadius * 1.35f : outerMargin * 0.78f);
        const auto maxLayoutWidth = area.getHeight() * 4.25f;
        if (! clockLayout && area.getWidth() > maxLayoutWidth)
            area = area.withSizeKeepingCentre (maxLayoutWidth, area.getHeight());

        const auto clockApplied = applyClockLayout (area);
        if (! clockApplied)
        {
            auto centre = area.getCentre();
            auto rx = area.getWidth() * 0.50f;
            auto ry = area.getHeight() * 0.47f;

            for (int i = 0; i < count; ++i)
            {
                auto angle = (juce::MathConstants<float>::twoPi * static_cast<float> (i) / static_cast<float> (count))
                           - juce::MathConstants<float>::halfPi;
                statePositions[static_cast<size_t> (i)] = { centre.x + std::cos (angle) * rx,
                                                            centre.y + std::sin (angle) * ry };
            }
        }

        if (! clockApplied)
            relaxStatePositions (area);
        if (includeManualOffsets)
            applyManualNodeOffsets();

        if (applyViewTransform)
            applyViewTransformToLayout();
    }

    bool isRootClockLayout() const
    {
        return machine->machineId == "root";
    }

    bool applyClockLayout (juce::Rectangle<float> area)
    {
        if (! isRootClockLayout())
            return false;

        const auto count = machine->getStateCount();
        if (count <= 0)
            return true;

        const auto centre = area.getCentre();
        if (count == 1)
        {
            statePositions[0] = centre;
            return true;
        }

        const auto available = juce::jmin (area.getWidth(), area.getHeight());
        const auto angleStep = juce::MathConstants<float>::twoPi / static_cast<float> (count);
        const auto spacingRadius = (stateRadius * 2.22f) / (2.0f * std::sin (angleStep * 0.5f));
        const auto comfortableRadius = juce::jmax (spacingRadius, stateRadius * (count <= 6 ? 2.85f : 2.55f));
        const auto ringRadius = juce::jmin (comfortableRadius, available * 0.42f);

        for (int i = 0; i < count; ++i)
        {
            const auto angle = angleStep * static_cast<float> (i) - juce::MathConstants<float>::halfPi;
            statePositions[static_cast<size_t> (i)] = { centre.x + std::cos (angle) * ringRadius,
                                                        centre.y + std::sin (angle) * ringRadius };
        }

        return true;
    }

    void clearNodePositionLock()
    {
        nodePositionLockActive = false;
        lockedScreenPositions.clear();
    }

    void ensureManualOffsetSize()
    {
        const auto count = static_cast<size_t> (machine->getStateCount());
        if (manualNodeOffsets.size() != count)
            manualNodeOffsets.resize (count, {});
        if (machine->nodeOffsets.size() != count)
            machine->nodeOffsets.resize (count, {});
    }

    void syncManualOffsetsFromMachine()
    {
        ensureManualOffsetSize();
        manualNodeOffsets = machine->nodeOffsets;
        ensureManualOffsetSize();
    }

    void syncManualOffsetsToMachine()
    {
        ensureManualOffsetSize();
        machine->nodeOffsets = manualNodeOffsets;
    }

    void applyManualNodeOffsets()
    {
        for (int i = 0; i < static_cast<int> (statePositions.size()); ++i)
        {
            auto& p = statePositions[static_cast<size_t> (i)];
            p += manualNodeOffsets[static_cast<size_t> (i)];
        }
    }

    void applyViewTransformToLayout()
    {
        for (auto& position : statePositions)
            position = graphToScreen (position);

        stateRadius *= zoom;
    }

    juce::Point<float> graphToScreen (juce::Point<float> point) const
    {
        const auto centre = getLocalBounds().toFloat().getCentre();
        return centre + panOffset + ((point - centre) * zoom);
    }

    juce::Point<float> screenToGraph (juce::Point<float> point) const
    {
        const auto centre = getLocalBounds().toFloat().getCentre();
        return centre + ((point - centre - panOffset) / zoom);
    }

    void relaxStatePositions (juce::Rectangle<float> area)
    {
        const auto count = static_cast<int> (statePositions.size());
        if (count < 2)
            return;

        constexpr int iterations = 150;
        for (int pass = 0; pass < iterations; ++pass)
        {
            for (int a = 0; a < count; ++a)
            {
                for (int b = a + 1; b < count; ++b)
                {
                    auto& pa = statePositions[static_cast<size_t> (a)];
                    auto& pb = statePositions[static_cast<size_t> (b)];
                    auto delta = pb - pa;
                    auto distance = std::sqrt (delta.x * delta.x + delta.y * delta.y);

                    if (distance < 0.001f)
                    {
                        delta = { 1.0f, 0.0f };
                        distance = 1.0f;
                    }

                    const auto minDistance = getNodeClearance (a) + getNodeClearance (b);
                    if (distance >= minDistance)
                        continue;

                    delta.x /= distance;
                    delta.y /= distance;

                    const auto push = (minDistance - distance) * 0.68f;
                    pa -= delta * push;
                    pb += delta * push;
                }
            }

            for (int i = 0; i < count; ++i)
            {
                auto& p = statePositions[static_cast<size_t> (i)];
                const auto extent = getNodeClearance (i);
                const auto minX = area.getX() + extent;
                const auto maxX = area.getRight() - extent;
                const auto minY = area.getY() + extent;
                const auto maxY = area.getBottom() - extent;
                p.x = maxX >= minX ? juce::jlimit (minX, maxX, p.x) : area.getCentreX();
                p.y = maxY >= minY ? juce::jlimit (minY, maxY, p.y) : area.getCentreY();
            }
        }
    }

    void drawRules (juce::Graphics& g)
    {
        for (const auto& rule : machine->rules)
        {
            if (rule.from >= static_cast<int> (statePositions.size()) || rule.to >= static_cast<int> (statePositions.size()))
                continue;

            auto fromCentre = statePositions[static_cast<size_t> (rule.from)];
            auto toCentre = statePositions[static_cast<size_t> (rule.to)];
            auto direction = toCentre - fromCentre;
            const auto length = juce::jmax (1.0f, std::sqrt (direction.x * direction.x + direction.y * direction.y));
            direction.x /= length;
            direction.y /= length;
            auto from = fromCentre + direction * (stateRadius * 1.05f);
            auto to = toCentre - direction * (stateRadius * 1.05f);
            auto mid = (from + to) * 0.5f;
            auto centre = getStatePositionCentre();
            auto control = mid + (mid - centre) * 0.18f;

            juce::Path curve;
            curve.startNewSubPath (from);
            curve.quadraticTo (control, to);

            const auto fromSelected = rule.from == machine->selectedState;
            const auto toPreviewed = rule.to == previewStateIndex;
            const auto lineAlpha = (fromSelected || toPreviewed) ? 0.42f : 0.16f;
            const auto lineWidth = (fromSelected || toPreviewed) ? 2.2f : 1.35f;
            const auto sourceColour = transitionColourFor (rule.from);
            const auto targetColour = graphColour (rule.to).interpolatedWith (mutedInk(), 0.28f);
            g.setColour ((fromSelected ? targetColour : sourceColour).withAlpha (lineAlpha + juce::jlimit (0.0f, 0.10f, rule.weight * 0.020f)));
            g.strokePath (curve, juce::PathStrokeType (lineWidth + juce::jlimit (0.0f, 0.9f, rule.weight * 0.14f),
                                                       juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));

            auto arrowPoint = from + (to - from) * 0.78f;
            const auto dotRadius = (fromSelected || toPreviewed) ? 2.7f : 2.0f;
            g.setColour ((toPreviewed ? graphColour (rule.to) : (fromSelected ? targetColour : sourceColour)).withAlpha ((fromSelected || toPreviewed) ? 0.72f : 0.36f));
            g.fillEllipse (arrowPoint.x - dotRadius, arrowPoint.y - dotRadius, dotRadius * 2.0f, dotRadius * 2.0f);
        }
    }

    juce::Point<float> getStatePositionCentre() const
    {
        if (statePositions.empty())
            return getLocalBounds().toFloat().getCentre();

        juce::Point<float> centre;
        for (const auto& position : statePositions)
            centre += position;

        return centre / static_cast<float> (statePositions.size());
    }

    void drawStates (juce::Graphics& g)
    {
        for (int i = 0; i < machine->getStateCount(); ++i)
        {
            auto p = statePositions[static_cast<size_t> (i)];
            auto selected = i == machine->selectedState;
            const auto previewed = i == previewStateIndex && i != machine->selectedState;
            auto laneCount = machine->getLaneCount (i);
            const auto stateColour = graphColour (i);

            g.setColour (stateColour.withAlpha (selected ? 0.16f : (previewed ? 0.11f : 0.045f)));
            g.fillEllipse (p.x - stateRadius * 1.34f, p.y - stateRadius * 1.34f, stateRadius * 2.68f, stateRadius * 2.68f);

            if (selected)
            {
                g.setColour (accentB().withAlpha (0.10f));
                g.fillEllipse (p.x - stateRadius * 1.58f, p.y - stateRadius * 1.58f, stateRadius * 3.16f, stateRadius * 3.16f);
            }

            g.setColour (selected ? panelFill().interpolatedWith (stateColour, 0.22f) : panelFill().interpolatedWith (stateColour, 0.045f));
            g.fillEllipse (p.x - stateRadius, p.y - stateRadius, stateRadius * 2.0f, stateRadius * 2.0f);

            g.setColour ((selected ? stateColour.brighter (0.12f) : hairline().interpolatedWith (stateColour, 0.22f)).withAlpha (selected ? 0.92f : 0.70f));
            g.drawEllipse (p.x - stateRadius, p.y - stateRadius, stateRadius * 2.0f, stateRadius * 2.0f, selected ? 2.3f : 1.1f);
            if (selected)
                drawActiveStateRing (g, p);
            if (previewed)
                drawPreviewStateRing (g, p);
            drawTimingPulse (g, i, p, selected);

            const auto* child = machine->childMachine (i);
            if (child != nullptr)
            {
                auto nestedRadius = stateRadius + 7.0f;
                g.setColour (graphColour (i, 2).withAlpha (selected ? 0.58f : 0.32f));
                g.drawEllipse (p.x - nestedRadius, p.y - nestedRadius, nestedRadius * 2.0f, nestedRadius * 2.0f, 1.4f);
                drawNestedIndicator (g, *child, p, selected, child == inspectedMachine);
            }

            g.setColour (ink());
            g.setFont (juce::FontOptions (stateRadius < 40.0f ? 13.0f : 16.0f, juce::Font::bold));
            g.drawFittedText (machine->state (i).name, juce::Rectangle<int> (static_cast<int> (p.x - stateRadius * 0.95f),
                                                                            static_cast<int> (p.y - stateRadius * 0.38f),
                                                                            static_cast<int> (stateRadius * 1.9f), 22),
                              juce::Justification::centred, 1);

            g.setColour (mutedInk());
            g.setFont (juce::FontOptions (stateRadius < 40.0f ? 10.0f : 12.0f));
            g.drawFittedText (juce::String (laneCount) + (laneCount == 1 ? " lane" : " lanes"),
                              juce::Rectangle<int> (static_cast<int> (p.x - stateRadius * 0.9f),
                                                    static_cast<int> (p.y + stateRadius * 0.10f),
                                                    static_cast<int> (stateRadius * 1.8f), 18),
                              juce::Justification::centred, 1);
        }

        drawStateBadgesOverlay (g);
    }

    void drawNestedIndicator (juce::Graphics& g, const MachineModel& child, juce::Point<float> parentCentre, bool parentSelected, bool childInspected)
    {
        const auto childCount = child.getStateCount();
        if (childCount <= 0)
            return;

        const auto orbitRadius = getNestedOrbitRadius();
        const auto nodeRadius = getNestedNodeRadius (childCount);
        std::vector<juce::Point<float>> childPoints;
        childPoints.reserve (static_cast<size_t> (childCount));

        for (int j = 0; j < childCount; ++j)
        {
            const auto angle = (juce::MathConstants<float>::twoPi * static_cast<float> (j) / static_cast<float> (childCount))
                             - juce::MathConstants<float>::halfPi;
            childPoints.push_back ({ parentCentre.x + std::cos (angle) * orbitRadius,
                                     parentCentre.y + std::sin (angle) * orbitRadius });
        }

        g.setColour ((childInspected ? inspectedFill() : juce::Colour (0xff11161d)).withAlpha (childInspected ? 0.88f : 0.78f));
        g.fillEllipse (parentCentre.x - orbitRadius - 4.0f, parentCentre.y - orbitRadius - 4.0f,
                       (orbitRadius + 4.0f) * 2.0f, (orbitRadius + 4.0f) * 2.0f);

        const auto baseColour = graphColour (child.entryState, 2);
        const auto ringColour = childInspected ? baseColour.brighter (0.15f) : baseColour.interpolatedWith (mutedInk(), 0.28f);
        g.setColour (ringColour.withAlpha (childInspected ? 0.9f : (parentSelected ? 0.58f : 0.38f)));
        g.drawEllipse (parentCentre.x - orbitRadius, parentCentre.y - orbitRadius,
                       orbitRadius * 2.0f, orbitRadius * 2.0f, childInspected ? 2.4f : (parentSelected ? 2.0f : 1.3f));

        for (const auto& rule : child.rules)
        {
            if (rule.from < 0 || rule.to < 0 || rule.from >= childCount || rule.to >= childCount)
                continue;

            auto from = childPoints[static_cast<size_t> (rule.from)];
            auto to = childPoints[static_cast<size_t> (rule.to)];
            auto mid = (from + to) * 0.5f;
            auto control = mid + ((mid - parentCentre) * 0.14f);
            juce::Path path;
            path.startNewSubPath (from);
            path.quadraticTo (control, to);
            g.setColour (transitionColourFor (rule.from + 2).withAlpha (childInspected ? 0.25f : 0.16f));
            g.strokePath (path, juce::PathStrokeType (1.0f));
        }

        for (int j = 0; j < childCount; ++j)
        {
            const auto point = childPoints[static_cast<size_t> (j)];
            const auto selected = j == child.selectedState;
            const auto stateColour = graphColour (j, 2);
            g.setColour ((selected && childInspected ? stateColour.brighter (0.35f) : stateColour).withAlpha (selected ? 0.98f : 0.84f));
            g.fillEllipse (point.x - nodeRadius, point.y - nodeRadius, nodeRadius * 2.0f, nodeRadius * 2.0f);
            g.setColour ((selected ? ink() : juce::Colour (0xff101318)).withAlpha (selected ? 0.82f : 0.92f));
            g.drawEllipse (point.x - nodeRadius, point.y - nodeRadius, nodeRadius * 2.0f, nodeRadius * 2.0f, selected ? 1.4f : 1.0f);

            if (auto* grandchild = child.childMachine (j))
                drawSecondLayerIndicator (g, *grandchild, point, selected && childInspected);
        }

        auto badge = getNestedBadgeBounds (child, parentCentre);
        g.setColour (juce::Colour (0xff101318).withAlpha (0.96f));
        g.fillRoundedRectangle (badge, 6.0f);
        g.setColour ((childInspected ? baseColour.brighter (0.12f) : (parentSelected ? accentA() : baseColour)).withAlpha (0.88f));
        g.drawRoundedRectangle (badge, 6.0f, 1.1f);
        g.setColour (ink());
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawFittedText (juce::String (childCount), badge.toNearestInt(), juce::Justification::centred, 1);
    }

    void drawSecondLayerIndicator (juce::Graphics& g, const MachineModel& grandchild, juce::Point<float> childStateCentre, bool selected)
    {
        const auto count = grandchild.getStateCount();
        if (count <= 0)
            return;

        const auto orbit = getSecondLayerOrbitRadius();
        const auto nodeRadius = getSecondLayerNodeRadius (count);

        const auto ringColour = graphColour (grandchild.entryState, 5);
        g.setColour (ringColour.withAlpha (selected ? 0.78f : 0.48f));
        g.drawEllipse (childStateCentre.x - orbit, childStateCentre.y - orbit, orbit * 2.0f, orbit * 2.0f, selected ? 1.35f : 0.9f);

        for (int k = 0; k < count; ++k)
        {
            const auto angle = (juce::MathConstants<float>::twoPi * static_cast<float> (k) / static_cast<float> (count))
                             - juce::MathConstants<float>::halfPi;
            const auto point = juce::Point<float> { childStateCentre.x + std::cos (angle) * orbit,
                                                    childStateCentre.y + std::sin (angle) * orbit };
            const auto stateSelected = k == grandchild.selectedState;
            const auto stateColour = graphColour (k, 5);
            g.setColour (stateColour.withAlpha (stateSelected ? 0.95f : 0.70f));
            g.fillEllipse (point.x - nodeRadius, point.y - nodeRadius, nodeRadius * 2.0f, nodeRadius * 2.0f);
        }

        auto badge = getSecondLayerBadgeBounds (grandchild, childStateCentre);
        g.setColour (juce::Colour (0xff101318).withAlpha (0.94f));
        g.fillRoundedRectangle (badge, 4.5f);
        g.setColour (ringColour.withAlpha (selected ? 0.88f : 0.72f));
        g.drawRoundedRectangle (badge, 4.5f, 0.9f);
        g.setColour (ink().withAlpha (0.95f));
        g.setFont (juce::FontOptions (8.8f, juce::Font::bold));
        g.drawFittedText (juce::String (count), badge.toNearestInt(), juce::Justification::centred, 1);
    }

    void drawActiveStateRing (juce::Graphics& g, juce::Point<float> centre)
    {
        const auto outerRadius = stateRadius + 5.0f;
        const auto markerAlpha = pulseReceivedMs > 0.0
            ? juce::jlimit (0.62f, 0.96f, 0.96f - static_cast<float> ((juce::Time::getMillisecondCounterHiRes() - pulseReceivedMs) / 1800.0))
            : 0.78f;

        g.setColour (accentB().withAlpha (markerAlpha));
        g.drawEllipse (centre.x - outerRadius, centre.y - outerRadius, outerRadius * 2.0f, outerRadius * 2.0f, 2.4f);
    }

    void drawActiveStateBadge (juce::Graphics& g, juce::Point<float> centre)
    {
        auto badge = juce::Rectangle<float> (0.0f, 0.0f, 42.0f, 18.0f)
                         .withCentre ({ centre.x, centre.y - stateRadius - 18.0f });
        g.setColour (juce::Colour (0xff101318).withAlpha (0.94f));
        g.fillRoundedRectangle (badge, 5.0f);
        g.setColour (accentB().withAlpha (0.92f));
        g.drawRoundedRectangle (badge, 5.0f, 1.1f);
        g.setColour (ink());
        g.setFont (juce::FontOptions (9.2f, juce::Font::bold));
        g.drawFittedText ("LIVE", badge.toNearestInt(), juce::Justification::centred, 1);
    }

    void drawPreviewStateRing (juce::Graphics& g, juce::Point<float> centre)
    {
        const auto outerRadius = stateRadius + 4.0f;
        g.setColour (accentB().withAlpha (0.58f));
        g.drawEllipse (centre.x - outerRadius, centre.y - outerRadius, outerRadius * 2.0f, outerRadius * 2.0f, 1.9f);
    }

    void drawPreviewStateBadge (juce::Graphics& g, juce::Point<float> centre)
    {
        const auto percent = juce::roundToInt (previewProbability * 100.0f);
        auto badge = juce::Rectangle<float> (0.0f, 0.0f, 54.0f, 18.0f)
                         .withCentre ({ centre.x, centre.y - stateRadius - 16.0f });
        g.setColour (juce::Colour (0xff101318).withAlpha (0.92f));
        g.fillRoundedRectangle (badge, 5.0f);
        g.setColour (accentB().withAlpha (0.78f));
        g.drawRoundedRectangle (badge, 5.0f, 1.0f);
        g.setColour (ink().withAlpha (0.92f));
        g.setFont (juce::FontOptions (8.6f, juce::Font::bold));
        g.drawFittedText ("NEXT " + juce::String (percent) + "%", badge.toNearestInt(), juce::Justification::centred, 1);
    }

    void drawStateBadgesOverlay (juce::Graphics& g)
    {
        if (machine == nullptr || statePositions.empty())
            return;

        const auto active = machine->selectedState;
        if (active >= 0 && active < static_cast<int> (statePositions.size()))
            drawActiveStateBadge (g, statePositions[static_cast<size_t> (active)]);

        if (previewStateIndex >= 0
            && previewStateIndex != active
            && previewStateIndex < static_cast<int> (statePositions.size()))
            drawPreviewStateBadge (g, statePositions[static_cast<size_t> (previewStateIndex)]);
    }

    void drawTimingPulse (juce::Graphics& g, int stateIndex, juce::Point<float> centre, bool selected)
    {
        if (! selected || pulseMachineId != machine->machineId || pulseStateIndex != stateIndex)
            return;

        const auto ageMs = juce::Time::getMillisecondCounterHiRes() - pulseReceivedMs;
        if (ageMs > 2200.0)
            return;

        const auto beatFlash = juce::jlimit (0.0f, 1.0f, 1.0f - static_cast<float> (ageMs / 420.0));
        const auto progressRadius = stateRadius + 11.0f;
        const auto startAngle = -juce::MathConstants<float>::halfPi;
        const auto endAngle = startAngle + juce::MathConstants<float>::twoPi * pulsePhase;

        juce::Path progress;
        progress.addCentredArc (centre.x, centre.y, progressRadius, progressRadius, 0.0f,
                                startAngle, endAngle, true);
        g.setColour (accentA().withAlpha (0.30f));
        g.strokePath (progress, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        if (beatFlash > 0.0f)
        {
            g.setColour (accentB().withAlpha (0.16f * beatFlash));
            const auto flareRadius = stateRadius * (1.25f + 0.34f * (1.0f - beatFlash));
            g.fillEllipse (centre.x - flareRadius, centre.y - flareRadius, flareRadius * 2.0f, flareRadius * 2.0f);
        }

        if (pulseBeatCount > 1)
        {
            const auto dotRadius = 2.1f;
            for (int beat = 0; beat < pulseBeatCount; ++beat)
            {
                const auto angle = startAngle + juce::MathConstants<float>::twoPi * static_cast<float> (beat) / static_cast<float> (pulseBeatCount);
                const auto dot = juce::Point<float> { centre.x + std::cos (angle) * progressRadius,
                                                      centre.y + std::sin (angle) * progressRadius };
                g.setColour ((beat == pulseBeatIndex ? accentA() : mutedInk()).withAlpha (beat == pulseBeatIndex ? 0.95f : 0.35f));
                g.fillEllipse (dot.x - dotRadius, dot.y - dotRadius, dotRadius * 2.0f, dotRadius * 2.0f);
            }
        }
    }

    int hitTestNestedState (const MachineModel& child, juce::Point<float> parentCentre, juce::Point<float> pointer) const
    {
        const auto childCount = child.getStateCount();
        if (childCount <= 0)
            return -1;

        const auto orbitRadius = getNestedOrbitRadius();
        const auto hitRadius = juce::jmax (7.0f, getNestedNodeRadius (childCount) + 5.0f);
        for (int j = 0; j < childCount; ++j)
        {
            const auto angle = (juce::MathConstants<float>::twoPi * static_cast<float> (j) / static_cast<float> (childCount))
                             - juce::MathConstants<float>::halfPi;
            juce::Point<float> point { parentCentre.x + std::cos (angle) * orbitRadius,
                                       parentCentre.y + std::sin (angle) * orbitRadius };

            if (point.getDistanceFrom (pointer) <= hitRadius)
                return j;
        }

        return -1;
    }

    int hitTestSecondLayerBadge (const MachineModel& child, juce::Point<float> parentCentre, juce::Point<float> pointer) const
    {
        for (int j = 0; j < child.getStateCount(); ++j)
        {
            auto* grandchild = child.childMachine (j);
            if (grandchild == nullptr)
                continue;

            if (getSecondLayerBadgeBounds (*grandchild, getNestedStatePoint (child, parentCentre, j)).contains (pointer))
                return j;
        }

        return -1;
    }

    std::pair<int, int> hitTestSecondLayerNestedState (const MachineModel& child, juce::Point<float> parentCentre, juce::Point<float> pointer) const
    {
        const auto childCount = child.getStateCount();
        const auto firstOrbit = getNestedOrbitRadius();
        const auto hitRadius = juce::jmax (5.5f, getSecondLayerNodeRadius (maxStateCount) + 4.0f);

        for (int j = 0; j < childCount; ++j)
        {
            auto* grandchild = child.childMachine (j);
            if (grandchild == nullptr)
                continue;

            const auto childAngle = (juce::MathConstants<float>::twoPi * static_cast<float> (j) / static_cast<float> (childCount))
                                  - juce::MathConstants<float>::halfPi;
            const auto childPoint = juce::Point<float> { parentCentre.x + std::cos (childAngle) * firstOrbit,
                                                         parentCentre.y + std::sin (childAngle) * firstOrbit };

            const auto secondOrbit = getSecondLayerOrbitRadius();
            for (int k = 0; k < grandchild->getStateCount(); ++k)
            {
                const auto angle = (juce::MathConstants<float>::twoPi * static_cast<float> (k) / static_cast<float> (grandchild->getStateCount()))
                                 - juce::MathConstants<float>::halfPi;
                const auto point = juce::Point<float> { childPoint.x + std::cos (angle) * secondOrbit,
                                                        childPoint.y + std::sin (angle) * secondOrbit };

                if (point.getDistanceFrom (pointer) <= hitRadius)
                    return { j, k };
            }
        }

        return { -1, -1 };
    }

    juce::Point<float> getNestedStatePoint (const MachineModel& child, juce::Point<float> parentCentre, int stateIndex) const
    {
        const auto childCount = juce::jmax (1, child.getStateCount());
        const auto angle = (juce::MathConstants<float>::twoPi * static_cast<float> (stateIndex) / static_cast<float> (childCount))
                         - juce::MathConstants<float>::halfPi;
        const auto orbitRadius = getNestedOrbitRadius();
        return { parentCentre.x + std::cos (angle) * orbitRadius,
                 parentCentre.y + std::sin (angle) * orbitRadius };
    }

    juce::Rectangle<float> getNestedBadgeBounds (const MachineModel& child, juce::Point<float> parentCentre) const
    {
        juce::ignoreUnused (child);
        const auto badgeWidth = stateRadius < 40.0f ? 24.0f : 28.0f;
        const auto badgeHeight = stateRadius < 40.0f ? 18.0f : 20.0f;
        const auto badgeRadius = getNestedOrbitRadius() + badgeWidth * 0.66f + 7.0f;
        const auto angle = juce::MathConstants<float>::pi * 0.25f;
        const auto centre = juce::Point<float> { parentCentre.x + std::cos (angle) * badgeRadius,
                                                 parentCentre.y + std::sin (angle) * badgeRadius };
        return juce::Rectangle<float> (0.0f, 0.0f, badgeWidth, badgeHeight).withCentre (centre);
    }

    juce::Rectangle<float> getSecondLayerBadgeBounds (const MachineModel& grandchild, juce::Point<float> childStateCentre) const
    {
        juce::ignoreUnused (grandchild);
        const auto scale = juce::jlimit (0.9f, 1.8f, stateRadius / 54.0f);
        const auto badgeWidth = 20.0f * scale;
        const auto badgeHeight = 15.0f * scale;
        const auto badgeRadius = getSecondLayerOrbitRadius() + badgeWidth * 0.68f + 4.0f;
        const auto angle = juce::MathConstants<float>::pi * 0.25f;
        const auto centre = juce::Point<float> { childStateCentre.x + std::cos (angle) * badgeRadius,
                                                 childStateCentre.y + std::sin (angle) * badgeRadius };
        return juce::Rectangle<float> (0.0f, 0.0f, badgeWidth, badgeHeight).withCentre (centre);
    }

    float getOuterNodeExtent() const
    {
        auto extent = stateRadius * 1.55f;

        for (int i = 0; i < machine->getStateCount(); ++i)
            extent = juce::jmax (extent, getNodeVisualExtent (i));

        return extent;
    }

    float getNodeClearance (int stateIndex) const
    {
        return getNodeVisualExtent (stateIndex) + 22.0f;
    }

    float getNodeVisualExtent (int stateIndex) const
    {
        auto extent = stateRadius * 1.55f;
        const auto* child = machine->childMachine (stateIndex);

        if (child == nullptr)
            return extent;

        extent = getNestedOrbitRadius() + getNestedNodeRadius (child->getStateCount()) + 10.0f;

        if (childHasGrandchildren (*child))
            extent += getSecondLayerOrbitRadius() + getSecondLayerBadgeOuterExtent() + 18.0f;
        else
            extent += getNestedBadgeOuterExtent() + 8.0f;

        return extent;
    }

    bool childHasGrandchildren (const MachineModel& child) const
    {
        for (int i = 0; i < child.getStateCount(); ++i)
            if (child.childMachine (i) != nullptr)
                return true;

        return false;
    }

    float getNestedOrbitRadius() const
    {
        return stateRadius + juce::jmap (stateRadius, 34.0f, 54.0f, 10.0f, 16.0f);
    }

    float getNestedNodeRadius (int childCount) const
    {
        return childCount > 7 || stateRadius < 40.0f ? 2.5f : 3.7f;
    }

    float getNestedBadgeOuterExtent() const
    {
        const auto badgeWidth = stateRadius < 40.0f ? 24.0f : 28.0f;
        return badgeWidth * 1.45f;
    }

    float getSecondLayerBadgeOuterExtent() const
    {
        const auto scale = juce::jlimit (0.9f, 1.8f, stateRadius / 54.0f);
        return 20.0f * scale * 1.55f;
    }

    float getSecondLayerOrbitRadius() const
    {
        return juce::jmax (18.0f, stateRadius * 0.63f);
    }

    float getSecondLayerNodeRadius (int childCount) const
    {
        const auto scale = juce::jlimit (1.15f, 3.2f, stateRadius / 42.0f);
        return (childCount > 7 ? 3.3f : 4.5f) * scale;
    }

    void startNestedStateCountEdit (int parentStateIndex, juce::Rectangle<float> badgeBounds)
    {
        auto* child = machine->childMachine (parentStateIndex);
        if (child == nullptr)
            return;

        finishNestedStateCountEdit (false);
        editingNestedParentState = parentStateIndex;
        auto safeThis = juce::Component::SafePointer<GraphComponent> (this);

        nestedCountEditor = std::make_unique<juce::TextEditor>();
        nestedCountEditor->setText (juce::String (child->getStateCount()), false);
        nestedCountEditor->setSelectAllWhenFocused (true);
        nestedCountEditor->setInputRestrictions (2, "0123456789");
        nestedCountEditor->setJustification (juce::Justification::centred);
        nestedCountEditor->setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff101318));
        nestedCountEditor->setColour (juce::TextEditor::textColourId, ink());
        nestedCountEditor->setColour (juce::TextEditor::highlightColourId, accentA().withAlpha (0.35f));
        nestedCountEditor->setColour (juce::TextEditor::outlineColourId, accentA());
        nestedCountEditor->onReturnKey = [safeThis] { if (safeThis != nullptr) safeThis->finishNestedStateCountEdit (true); };
        nestedCountEditor->onEscapeKey = [safeThis] { if (safeThis != nullptr) safeThis->finishNestedStateCountEdit (false); };
        nestedCountEditor->onFocusLost = [safeThis] { if (safeThis != nullptr) safeThis->finishNestedStateCountEdit (true); };

        addAndMakeVisible (*nestedCountEditor);
        nestedCountEditor->setBounds (badgeBounds.expanded (4.0f, 3.0f).toNearestInt());
        nestedCountEditor->grabKeyboardFocus();
        nestedCountEditor->selectAll();
    }

    void startSecondLayerStateCountEdit (int parentStateIndex, int childStateIndex, juce::Rectangle<float> badgeBounds)
    {
        auto* child = machine->childMachine (parentStateIndex);
        auto* grandchild = child != nullptr ? child->childMachine (childStateIndex) : nullptr;
        if (grandchild == nullptr)
            return;

        finishNestedStateCountEdit (false);
        editingNestedParentState = parentStateIndex;
        editingSecondLayerChildState = childStateIndex;
        auto safeThis = juce::Component::SafePointer<GraphComponent> (this);

        nestedCountEditor = std::make_unique<juce::TextEditor>();
        nestedCountEditor->setText (juce::String (grandchild->getStateCount()), false);
        nestedCountEditor->setSelectAllWhenFocused (true);
        nestedCountEditor->setInputRestrictions (2, "0123456789");
        nestedCountEditor->setJustification (juce::Justification::centred);
        nestedCountEditor->setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff101318));
        nestedCountEditor->setColour (juce::TextEditor::textColourId, ink());
        nestedCountEditor->setColour (juce::TextEditor::highlightColourId, accentC().withAlpha (0.35f));
        nestedCountEditor->setColour (juce::TextEditor::outlineColourId, accentC());
        nestedCountEditor->onReturnKey = [safeThis] { if (safeThis != nullptr) safeThis->finishNestedStateCountEdit (true); };
        nestedCountEditor->onEscapeKey = [safeThis] { if (safeThis != nullptr) safeThis->finishNestedStateCountEdit (false); };
        nestedCountEditor->onFocusLost = [safeThis] { if (safeThis != nullptr) safeThis->finishNestedStateCountEdit (true); };

        addAndMakeVisible (*nestedCountEditor);
        nestedCountEditor->setBounds (badgeBounds.expanded (4.0f, 3.0f).toNearestInt());
        nestedCountEditor->grabKeyboardFocus();
        nestedCountEditor->selectAll();
    }

    void finishNestedStateCountEdit (bool commit)
    {
        if (nestedCountEditor == nullptr)
            return;

        const auto parentStateIndex = editingNestedParentState;
        const auto text = nestedCountEditor->getText();

        auto editor = std::move (nestedCountEditor);
        const auto childStateIndex = editingSecondLayerChildState;
        editingNestedParentState = -1;
        editingSecondLayerChildState = -1;
        removeChildComponent (editor.get());
        editor.reset();

        if (! commit || parentStateIndex < 0)
            return;

        const auto newCount = juce::jlimit (1, maxStateCount, text.getIntValue());
        if (childStateIndex >= 0)
        {
            if (onSecondLayerNestedStateCountChanged)
                onSecondLayerNestedStateCountChanged (parentStateIndex, childStateIndex, newCount);
        }
        else if (onNestedStateCountChanged)
            onNestedStateCountChanged (parentStateIndex, newCount);
    }

    void timerCallback() override
    {
        if (pulseReceivedMs <= 0.0)
            return;

        const auto ageMs = juce::Time::getMillisecondCounterHiRes() - pulseReceivedMs;
        if (ageMs < 1900.0)
            repaint();
        else
            pulseReceivedMs = 0.0;
    }

    MachineModel* machine;
    MachineModel* inspectedMachine = nullptr;
    std::vector<juce::Point<float>> statePositions;
    std::vector<juce::Point<float>> lockedScreenPositions;
    std::unique_ptr<juce::TextEditor> nestedCountEditor;
    float stateRadius = 48.0f;
    float lockedScreenRadius = 48.0f;
    float zoom = 1.0f;
    juce::Point<float> panOffset;
    juce::String pulseMachineId;
    int pulseStateIndex = -1;
    float pulsePhase = 0.0f;
    int pulseBeatIndex = 0;
    int pulseBeatCount = 1;
    double pulseReceivedMs = 0.0;
    int previewStateIndex = -1;
    float previewProbability = 0.0f;
    juce::Point<float> dragStart;
    juce::Point<float> panStart;
    std::vector<juce::Point<float>> manualNodeOffsets;
    juce::Point<float> nodeOffsetStart;
    int draggingStateIndex = -1;
    bool draggedState = false;
    bool nodePositionLockActive = false;
    int editingNestedParentState = -1;
    int editingSecondLayerChildState = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GraphComponent)
};

class RuleListComponent final : public juce::Component
{
public:
    explicit RuleListComponent (MachineModel& modelToUse) : machine (&modelToUse)
    {
        addAndMakeVisible (fromBox);
        addAndMakeVisible (toBox);
        addAndMakeVisible (weightSlider);
        addAndMakeVisible (addButton);
        addAndMakeVisible (updateButton);
        addAndMakeVisible (removeButton);
        addAndMakeVisible (ringButton);

        weightSlider.setRange (0.1, 5.0, 0.1);
        weightSlider.setValue (1.0);
        weightSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 22);

        addButton.setButtonText ("Add");
        updateButton.setButtonText ("Save");
        removeButton.setButtonText ("Delete");
        ringButton.setButtonText ("Rules");

        addButton.onClick = [this]
        {
            addRuleFromControls();
        };

        updateButton.onClick = [this]
        {
            updateSelectedRule();
        };

        removeButton.onClick = [this]
        {
            removeSelectedRule();
        };

        ringButton.onClick = [this]
        {
            machine->regenerateRingRules();
            selectedRuleIndex = -1;
            refreshChoices();
            if (onRulesChanged)
                onRulesChanged();
        };

        refreshChoices();
    }

    void setMachine (MachineModel& modelToUse)
    {
        const auto stateCount = modelToUse.getStateCount();
        const auto ruleCount = static_cast<int> (modelToUse.rules.size());
        const auto selected = modelToUse.selectedState;
        const auto needsChoices = machine != &modelToUse
                               || cachedStateCount != stateCount
                               || cachedSelectedState != selected
                               || cachedRuleCount != ruleCount;

        machine = &modelToUse;
        if (! needsChoices)
            return;

        if (cachedStateCount != stateCount || cachedRuleCount != ruleCount || selectedRuleIndex >= ruleCount)
            selectedRuleIndex = -1;

        cachedStateCount = stateCount;
        cachedSelectedState = selected;
        cachedRuleCount = ruleCount;
        refreshChoices();
        repaint();
    }

    std::function<void()> onRulesChanged;

    void refreshChoices()
    {
        fromBox.clear();
        toBox.clear();
        for (int i = 0; i < machine->getStateCount(); ++i)
        {
            fromBox.addItem (machine->state (i).name, i + 1);
            toBox.addItem (machine->state (i).name, i + 1);
        }
        selectedRuleIndex = selectedRuleIndex >= static_cast<int> (machine->rules.size()) ? -1 : selectedRuleIndex;

        if (selectedRuleIndex >= 0)
            loadRuleIntoControls (selectedRuleIndex);
        else
        {
            fromBox.setSelectedItemIndex (machine->selectedState);
            toBox.setSelectedItemIndex ((machine->selectedState + 1) % machine->getStateCount());
            weightSlider.setValue (1.0, juce::dontSendNotification);
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (panelFill());
        g.setColour (ink());
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText ("Transition rules", getLocalBounds().removeFromTop (28), juce::Justification::centredLeft);

        auto list = getRuleListBounds();
        g.setFont (juce::FontOptions (12.5f));

        for (int i = 0; i < static_cast<int> (machine->rules.size()); ++i)
        {
            auto row = list.removeFromTop (26);
            const auto& r = machine->rules[static_cast<size_t> (i)];
            const auto selected = i == selectedRuleIndex;
            const auto fromColour = graphColour (r.from);
            const auto toColour = graphColour (r.to);
            g.setColour (selected ? rowFill().interpolatedWith (fromColour, 0.14f)
                                  : (i % 2 == 0 ? rowFill().withAlpha (0.76f) : panelFill().brighter (0.02f)));
            g.fillRoundedRectangle (row.toFloat().reduced (1.0f), 3.0f);
            if (selected)
            {
                g.setColour (fromColour.withAlpha (0.76f));
                g.fillRoundedRectangle (row.removeFromLeft (3).toFloat().reduced (0.0f, 4.0f), 1.5f);
            }

            g.setColour (selected ? ink() : mutedInk());

            auto rowArea = row.reduced (8, 0);
            g.setColour (selected ? fromColour.brighter (0.08f) : mutedInk().withAlpha (0.78f));
            g.drawText (machine->state (r.from).name, rowArea.removeFromLeft (96), juce::Justification::centredLeft);
            g.setColour (mutedInk().withAlpha (0.72f));
            g.drawText ("->", rowArea.removeFromLeft (24), juce::Justification::centred);
            g.setColour (selected ? toColour.brighter (0.08f) : mutedInk().withAlpha (0.78f));
            g.drawText (machine->state (r.to).name, rowArea.removeFromLeft (96), juce::Justification::centredLeft);
            g.setColour (selected ? ink() : mutedInk());
            g.drawText ("w " + juce::String (r.weight, 1), rowArea.removeFromRight (52), juce::Justification::centredRight);
        }
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        auto list = getRuleListBounds();
        for (int i = 0; i < static_cast<int> (machine->rules.size()); ++i)
        {
            auto row = list.removeFromTop (26);
            if (row.contains (event.getPosition()))
            {
                selectedRuleIndex = i;
                loadRuleIntoControls (i);
                repaint();
                return;
            }
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (0, 30).removeFromTop (36);
        fromBox.setBounds (area.removeFromLeft (82).reduced (0, 4));
        toBox.setBounds (area.removeFromLeft (82).reduced (4));
        weightSlider.setBounds (area.removeFromLeft (96).reduced (4));
        addButton.setBounds (area.removeFromLeft (54).reduced (4));
        updateButton.setBounds (area.removeFromLeft (58).reduced (4));
        removeButton.setBounds (area.removeFromLeft (62).reduced (4));
        ringButton.setBounds (area.removeFromLeft (64).reduced (4));
    }

private:
    juce::Rectangle<int> getRuleListBounds() const
    {
        return getLocalBounds().withTrimmedTop (76).reduced (0, 6);
    }

    void addRuleFromControls()
    {
        auto from = fromBox.getSelectedItemIndex();
        auto to = toBox.getSelectedItemIndex();
        if (from < 0 || to < 0)
            return;

        machine->rules.push_back ({ from, to, static_cast<float> (weightSlider.getValue()) });
        selectedRuleIndex = static_cast<int> (machine->rules.size()) - 1;
        if (onRulesChanged)
            onRulesChanged();
        repaint();
    }

    void updateSelectedRule()
    {
        if (selectedRuleIndex < 0 || selectedRuleIndex >= static_cast<int> (machine->rules.size()))
            return;

        auto from = fromBox.getSelectedItemIndex();
        auto to = toBox.getSelectedItemIndex();
        if (from < 0 || to < 0)
            return;

        machine->rules[static_cast<size_t> (selectedRuleIndex)] = { from, to, static_cast<float> (weightSlider.getValue()) };
        if (onRulesChanged)
            onRulesChanged();
        repaint();
    }

    void removeSelectedRule()
    {
        if (selectedRuleIndex < 0 || selectedRuleIndex >= static_cast<int> (machine->rules.size()))
            return;

        machine->rules.erase (machine->rules.begin() + selectedRuleIndex);
        selectedRuleIndex = juce::jmin (selectedRuleIndex, static_cast<int> (machine->rules.size()) - 1);
        refreshChoices();
        if (onRulesChanged)
            onRulesChanged();
        repaint();
    }

    void loadRuleIntoControls (int index)
    {
        if (index < 0 || index >= static_cast<int> (machine->rules.size()))
            return;

        const auto& rule = machine->rules[static_cast<size_t> (index)];
        fromBox.setSelectedItemIndex (rule.from, juce::dontSendNotification);
        toBox.setSelectedItemIndex (rule.to, juce::dontSendNotification);
        weightSlider.setValue (rule.weight, juce::dontSendNotification);
    }

    MachineModel* machine;
    juce::ComboBox fromBox;
    juce::ComboBox toBox;
    juce::Slider weightSlider;
    juce::TextButton addButton;
    juce::TextButton updateButton;
    juce::TextButton removeButton;
    juce::TextButton ringButton;
    int selectedRuleIndex = -1;
    int cachedStateCount = -1;
    int cachedSelectedState = -1;
    int cachedRuleCount = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RuleListComponent)
};

class PillBar final : public juce::Component
{
public:
    std::function<void (int)> onIndexSelected;

    void setItems (const juce::StringArray& names, int selected)
    {
        buttons.clear();
        selectedIndex = selected;

        for (int i = 0; i < names.size(); ++i)
        {
            auto button = std::make_unique<juce::TextButton> (names[i]);
            button->setClickingTogglesState (false);
            button->onClick = [this, i]
            {
                selectedIndex = i;
                if (onIndexSelected)
                    onIndexSelected (i);
                repaint();
            };
            addAndMakeVisible (*button);
            buttons.push_back (std::move (button));
        }

        resized();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colours::transparentBlack);
    }

    void resized() override
    {
        if (buttons.empty())
            return;

        auto area = getLocalBounds().reduced (5, 4);
        auto width = area.getWidth() / static_cast<int> (buttons.size());

        for (int i = 0; i < static_cast<int> (buttons.size()); ++i)
        {
            auto cell = area.removeFromLeft (i == static_cast<int> (buttons.size()) - 1 ? area.getWidth() : width);
            const auto stateColour = graphColour (i);
            buttons[static_cast<size_t> (i)]->setBounds (cell.reduced (3, 1));
            buttons[static_cast<size_t> (i)]->setColour (juce::TextButton::buttonColourId,
                                                         i == selectedIndex ? rowFill().interpolatedWith (stateColour, 0.20f) : juce::Colour (0xff1a1f24).interpolatedWith (stateColour, 0.025f));
            buttons[static_cast<size_t> (i)]->setColour (juce::TextButton::buttonOnColourId, rowFill().interpolatedWith (stateColour, 0.24f));
            buttons[static_cast<size_t> (i)]->setColour (juce::TextButton::textColourOffId,
                                                         i == selectedIndex ? stateColour.brighter (0.14f) : mutedInk().withAlpha (0.78f));
        }
    }

private:
    std::vector<std::unique_ptr<juce::TextButton>> buttons;
    int selectedIndex = 0;
};

class ArrangementStripComponent final : public juce::Component,
                                        private juce::Timer
{
public:
    std::function<void (int)> onStateSelected;
    std::function<void (int, int)> onNestedStateSelected;
    std::function<void (int, int)> onLaneSelected;
    std::function<void (int, int)> onStateLengthChanged;
    std::function<void()> onDeleteSelectedLaneRequested;

    ArrangementStripComponent()
    {
        setWantsKeyboardFocus (true);
        startTimerHz (60);
    }

    void setMachine (MachineModel& rootMachine, double playbackRate, bool showExtended, bool exporting, double exportElapsed, double exportTotal)
    {
        machine = &rootMachine;
        rate = juce::jmax (0.05, playbackRate);
        extended = showExtended;
        exportInProgress = exporting;
        exportElapsedSeconds = exportElapsed;
        exportTotalSeconds = exportTotal;
        clampScroll();
        repaint();
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        grabKeyboardFocus();

        if (extended)
        {
            if (handleZoomControlClick (event.position))
                return;
        }

        if (const auto handleState = lengthHandleStateAt (event.position); handleState >= 0)
        {
            resizingStateIndex = handleState;
            resizingStartX = event.position.x;
            resizingStartBars = machine != nullptr ? machine->state (handleState).arrangementBars : 1;
            resizingStartWidth = sectionBounds (lengthEditRow(), totalSeconds(), handleState, false).getWidth();
            setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
            return;
        }

        if (extended)
        {
            if (auto hit = hitTestExtended (event.position); hit.kind != Hit::none)
            {
                if (hit.kind == Hit::nestedState && onNestedStateSelected)
                    onNestedStateSelected (hit.stateIndex, hit.detailIndex);
                else if (hit.kind == Hit::lane && onLaneSelected)
                    onLaneSelected (hit.stateIndex, hit.detailIndex);
                else if (onStateSelected)
                    onStateSelected (hit.stateIndex);
                return;
            }
        }

        if (const auto index = stateIndexAt (event.position); index >= 0 && onStateSelected)
            onStateSelected (index);
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (isLaneDeleteKey (key))
        {
            if (onDeleteSelectedLaneRequested)
                onDeleteSelectedLaneRequested();
            return true;
        }

        return false;
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        if (resizingStateIndex < 0 || machine == nullptr || onStateLengthChanged == nullptr)
            return;

        const auto pixelsPerBar = resizingStartWidth / static_cast<float> (juce::jmax (1, resizingStartBars));
        const auto deltaBars = juce::roundToInt ((event.position.x - resizingStartX) / juce::jmax (8.0f, pixelsPerBar));
        const auto newBars = juce::jlimit (1, 64, resizingStartBars + deltaBars);
        if (newBars != machine->state (resizingStateIndex).arrangementBars)
        {
            onStateLengthChanged (resizingStateIndex, newBars);
            repaint();
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        resizingStateIndex = -1;
        resizingStartBars = 1;
        resizingStartWidth = 1.0f;
    }

    void mouseMove (const juce::MouseEvent& event) override
    {
        auto nextHover = hitAt (event.position);
        const auto lengthHandleState = lengthHandleStateAt (event.position);
        const auto zoomControl = extended ? zoomControlAt (event.position) : ZoomControl::none;
        const auto interactive = nextHover.kind != Hit::none || zoomControl != ZoomControl::none || lengthHandleState >= 0;

        if (lengthHandleState >= 0)
            setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
        else
            setMouseCursor (interactive ? juce::MouseCursor::PointingHandCursor
                                        : juce::MouseCursor::NormalCursor);
        const auto nextHint = lengthHandleState >= 0 ? "Drag section length"
                                                     : tooltipFor (nextHover);

        if (! sameHit (hoveredHit, nextHover)
            || hoveredZoomControl != zoomControl
            || hoveredLengthHandleState != lengthHandleState
            || hoverHint != nextHint)
        {
            hoveredHit = nextHover;
            hoveredZoomControl = zoomControl;
            hoveredLengthHandleState = lengthHandleState;
            hoverHint = nextHint;
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        hoveredHit = {};
        hoveredZoomControl = ZoomControl::none;
        hoveredLengthHandleState = -1;
        hoverHint = {};
        setMouseCursor (juce::MouseCursor::NormalCursor);
        repaint();
    }

    void setTimingPulse (const juce::String& machineIdToUse, int stateIndexToUse, float phaseToUse, int beatIndexToUse, int beatCountToUse)
    {
        juce::ignoreUnused (phaseToUse, beatIndexToUse, beatCountToUse);

        if (machine == nullptr || machineIdToUse != machine->machineId || stateIndexToUse < 0 || stateIndexToUse >= machine->getStateCount())
            return;

        if (playheadMachineId != machineIdToUse || playheadStateIndex != stateIndexToUse || playheadAnchorMs <= 0.0)
            setPlaybackState (machineIdToUse, stateIndexToUse);
    }

    void setPlaybackState (const juce::String& machineIdToUse, int stateIndexToUse)
    {
        if (machine == nullptr || machineIdToUse != machine->machineId || stateIndexToUse < 0 || stateIndexToUse >= machine->getStateCount())
            return;

        playheadMachineId = machineIdToUse;
        playheadStateIndex = stateIndexToUse;
        playheadPhaseOffset = 0.0f;
        playheadDurationSeconds = stateDurationSeconds (machine->state (stateIndexToUse));
        playheadAnchorMs = juce::Time::getMillisecondCounterHiRes() - visualSchedulerCompensationMs;
        repaint();
    }

    void clearTimingPulse()
    {
        playheadMachineId.clear();
        playheadStateIndex = -1;
        playheadPhaseOffset = 0.0f;
        playheadAnchorMs = 0.0;
        playheadDurationSeconds = 0.0;
        repaint();
    }

    void mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
    {
        if (! extended)
            return;

        if (event.mods.isCommandDown() || event.mods.isCtrlDown())
        {
            const auto factor = wheel.deltaY > 0.0f ? 1.12f : 0.89f;
            setArrangementZoom (arrangementZoom * factor, event.position.x);
        }
        else
        {
            arrangementScrollX -= (std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? wheel.deltaX : wheel.deltaY) * 140.0f;
        }

        clampScroll();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff101319).withAlpha (0.94f));
        g.fillRoundedRectangle (bounds, 7.0f);
        g.setColour (hairline().withAlpha (0.30f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 7.0f, 0.8f);

        if (machine == nullptr || machine->states.empty())
            return;

        auto titleArea = getLocalBounds().reduced (12, 7).removeFromTop (22);
        const auto total = totalSeconds();
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.setColour (ink().withAlpha (0.92f));
        g.drawFittedText ("Arrangement", titleArea.removeFromLeft (112), juce::Justification::centredLeft, 1);
        g.setColour (mutedInk().withAlpha (0.74f));
        g.setFont (juce::FontOptions (10.5f));
        g.drawFittedText (juce::String (totalArrangementBars()) + " bars  "
                            + juce::String (total, 1) + "s cycle  x" + juce::String (rate, 2),
                          titleArea, juce::Justification::centredLeft, 1);
        if (extended)
        {
            drawZoomControls (g);
            g.setColour (accentA().withAlpha (0.82f));
            g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
            titleArea.removeFromRight (170);
            auto hintArea = titleArea.removeFromRight (150);
            g.drawFittedText (hoverHint.isNotEmpty() ? hoverHint : "nested + lanes",
                              hintArea, juce::Justification::centredRight, 1);
        }

        auto timeline = timelineArea();
        g.setColour (juce::Colour (0xff0c0f14).withAlpha (extended ? 0.50f : 0.60f));
        g.fillRoundedRectangle (timeline, 4.0f);
        if (extended)
        {
            const auto content = contentArea (timeline);
            const auto rows = extendedRowsFor (content);
            auto stripe = [&g] (juce::Rectangle<float> row, float alpha)
            {
                g.setColour (juce::Colour (0xff141922).withAlpha (alpha));
                g.fillRoundedRectangle (row.reduced (0.0f, 2.0f), 5.0f);
            };

            stripe (rows.top, 0.32f);
            stripe (rows.flow, 0.18f);
            stripe (rows.nested, 0.16f);
            stripe (rows.lanes, 0.14f);
        }

        if (total <= 0.0)
            return;

        auto content = contentArea (timeline);
        juce::Rectangle<float> flowArea;
        g.saveState();
        g.reduceClipRegion (content.toNearestInt());
        if (extended)
            flowArea = drawExtendedTimeline (g, timeline, content, total);
        else
            drawSections (g, content, total);

        drawTransitionFlow (g, content, total, flowArea);
        drawRuler (g, content, total);
        drawExportProgress (g, content, total);
        drawPlayhead (g, content, total);
        drawHoverHighlight (g, content, total);
        drawLengthHandleHighlight (g, total);
        g.restoreState();
    }

private:
    void timerCallback() override
    {
        if (isShowing() && playheadAnchorMs > 0.0)
            repaint();
    }

    struct Hit
    {
        enum Kind
        {
            none,
            topState,
            nestedState,
            lane
        };

        Kind kind = none;
        int stateIndex = -1;
        int detailIndex = -1;
    };

    enum class ZoomControl
    {
        none,
        zoomOut,
        readout,
        zoomIn,
        fit
    };

    struct ZoomControlBounds
    {
        juce::Rectangle<float> out;
        juce::Rectangle<float> readout;
        juce::Rectangle<float> in;
        juce::Rectangle<float> fit;
    };

    struct ExtendedRows
    {
        juce::Rectangle<float> top;
        juce::Rectangle<float> flow;
        juce::Rectangle<float> nested;
        juce::Rectangle<float> lanes;
    };

    static bool sameHit (Hit a, Hit b)
    {
        return a.kind == b.kind && a.stateIndex == b.stateIndex && a.detailIndex == b.detailIndex;
    }

    double stateDurationSeconds (const State& state) const
    {
        return juce::jmax (0.1, state.secondsPerSection() / rate);
    }

    double totalSeconds() const
    {
        auto total = 0.0;
        for (const auto& state : machine->states)
            total += stateDurationSeconds (state);
        return total;
    }

    int totalArrangementBars() const
    {
        auto total = 0;
        for (const auto& state : machine->states)
            total += juce::jlimit (1, 64, state.arrangementBars);
        return total;
    }

    juce::Rectangle<float> timelineArea() const
    {
        return getLocalBounds().toFloat().reduced (12.0f, 8.0f).withTrimmedTop (26.0f).reduced (0.0f, 2.0f);
    }

    juce::Rectangle<float> contentArea (juce::Rectangle<float> timeline) const
    {
        return extended ? timeline.withTrimmedLeft (70.0f).reduced (0.0f, 1.0f)
                        : timeline.reduced (0.0f, 2.0f);
    }

    juce::Rectangle<float> lengthEditRow() const
    {
        const auto timeline = timelineArea();
        const auto content = contentArea (timeline);
        return extended ? extendedRowsFor (content).top : content;
    }

    ExtendedRows extendedRowsFor (juce::Rectangle<float> content) const
    {
        auto rows = content.withTrimmedTop (22.0f).reduced (0.0f, 8.0f);
        const auto usableHeight = rows.getHeight();
        ExtendedRows result;
        result.top = rows.removeFromTop (juce::jlimit (62.0f, 92.0f, usableHeight * 0.19f));
        rows.removeFromTop (10.0f);
        result.flow = rows.removeFromTop (juce::jlimit (112.0f, 180.0f, usableHeight * 0.36f));
        rows.removeFromTop (12.0f);
        result.nested = rows.removeFromTop (juce::jlimit (48.0f, 72.0f, usableHeight * 0.16f));
        rows.removeFromTop (10.0f);
        result.lanes = rows.withHeight (juce::jmin (86.0f, rows.getHeight()));
        return result;
    }

    float maxScrollFor (juce::Rectangle<float> area) const
    {
        return extended ? juce::jmax (0.0f, area.getWidth() * (arrangementZoom - 1.0f))
                        : 0.0f;
    }

    void clampScroll()
    {
        if (machine == nullptr)
        {
            arrangementScrollX = 0.0f;
            return;
        }

        arrangementScrollX = juce::jlimit (0.0f, maxScrollFor (contentArea (timelineArea())), arrangementScrollX);
    }

    juce::Rectangle<float> sectionBounds (juce::Rectangle<float> area, double total, int stateIndex, bool withGap = true) const
    {
        const auto zoom = extended ? arrangementZoom : 1.0f;
        const auto scroll = extended ? arrangementScrollX : 0.0f;
        const auto contentWidth = area.getWidth() * zoom;
        auto x = area.getX() - scroll;
        for (int i = 0; i < machine->getStateCount(); ++i)
        {
            const auto width = i == machine->getStateCount() - 1 ? area.getX() - scroll + contentWidth - x
                                                                 : contentWidth * static_cast<float> (stateDurationSeconds (machine->state (i)) / total);
            if (i == stateIndex)
                return juce::Rectangle<float> (x, area.getY(),
                                               juce::jmax (1.0f, width - (withGap ? 4.0f : 0.0f)),
                                               area.getHeight());
            x += width;
        }

        return {};
    }

    Hit hitAt (juce::Point<float> point) const
    {
        if (extended)
            return hitTestExtended (point);

        const auto index = stateIndexAt (point);
        return index >= 0 ? Hit { Hit::topState, index, -1 } : Hit {};
    }

    int lengthHandleStateAt (juce::Point<float> point) const
    {
        if (machine == nullptr || machine->states.empty())
            return -1;

        if (extended && zoomControlAt (point) != ZoomControl::none)
            return -1;

        const auto total = totalSeconds();
        if (total <= 0.0)
            return -1;

        const auto row = lengthEditRow();
        if (! row.expanded (0.0f, 5.0f).contains (point))
            return -1;

        for (int i = 0; i < machine->getStateCount(); ++i)
        {
            auto bounds = sectionBounds (row, total, i, false);
            auto handle = bounds.removeFromRight (juce::jmin (12.0f, juce::jmax (7.0f, bounds.getWidth() * 0.12f))).expanded (2.0f, 4.0f);
            if (handle.contains (point))
                return i;
        }

        return -1;
    }

    void drawSections (juce::Graphics& g, juce::Rectangle<float> area, double total, bool includeExtendedDetails = true)
    {
        g.saveState();
        g.reduceClipRegion (area.toNearestInt().expanded (6, 6));

        for (int i = 0; i < machine->getStateCount(); ++i)
        {
            const auto& state = machine->state (i);
            const auto seconds = stateDurationSeconds (state);
            auto segment = sectionBounds (area, total, i);
            const auto colour = graphColour (i);
            const auto selected = i == machine->selectedState;

            if (segment.getWidth() > 4.0f)
            {
                const auto fill = rowFill().interpolatedWith (colour, selected ? 0.24f : 0.10f);
                if (selected)
                {
                    g.setColour (colour.withAlpha (0.11f));
                    g.fillRoundedRectangle (segment.expanded (5.0f, 5.0f), 8.0f);
                }

                g.setColour (fill.withAlpha (selected ? 0.98f : 0.86f));
                g.fillRoundedRectangle (segment, 5.0f);

                auto colourBar = segment.withHeight (4.0f);
                g.setColour (colour.withAlpha (selected ? 0.96f : 0.68f));
                g.fillRoundedRectangle (colourBar, 2.0f);

                if (selected)
                {
                    g.setColour (colour.brighter (0.16f).withAlpha (0.98f));
                    g.drawRoundedRectangle (segment.reduced (0.5f), 5.0f, 1.8f);
                    g.setColour (ink().withAlpha (0.92f));
                    g.drawRoundedRectangle (segment.reduced (3.0f), 4.0f, 0.8f);
                }
                else
                {
                    g.setColour (hairline().withAlpha (0.36f));
                    g.drawRoundedRectangle (segment.reduced (0.5f), 5.0f, 0.7f);
                }

                auto textSegment = extended ? segment.withHeight (juce::jmin (54.0f, segment.getHeight() * 0.56f))
                                            : segment;
                drawSectionText (g, textSegment, state, i, seconds, colour, selected);
                if (extended && includeExtendedDetails)
                    drawExtendedDetails (g, segment.withTrimmedTop (textSegment.getHeight() + 2.0f), state, i, colour, selected);
            }

        }

        g.restoreState();
    }

    juce::Rectangle<float> drawExtendedTimeline (juce::Graphics& g,
                                                 juce::Rectangle<float> timeline,
                                                 juce::Rectangle<float> content,
                                                 double total)
    {
        const auto rows = extendedRowsFor (content);

        drawRowLabels (g, timeline.withWidth (64.0f).withY (rows.top.getY()).withHeight (rows.lanes.getBottom() - rows.top.getY()),
                       rows.top, rows.flow, rows.nested, rows.lanes);
        g.setColour (juce::Colour (0xff0c0f14).withAlpha (0.38f));
        g.fillRoundedRectangle (rows.flow.reduced (0.0f, 2.0f), 6.0f);
        drawSections (g, rows.top, total, false);
        drawNestedRow (g, rows.nested, total);
        drawLanesRow (g, rows.lanes, total);
        return rows.flow.reduced (0.0f, 3.0f);
    }

    void drawRowLabels (juce::Graphics& g,
                        juce::Rectangle<float> labelArea,
                        juce::Rectangle<float> topRow,
                        juce::Rectangle<float> flowRow,
                        juce::Rectangle<float> nestedRow,
                        juce::Rectangle<float> lanesRow)
    {
        g.setColour (hairline().withAlpha (0.18f));
        g.drawVerticalLine (juce::roundToInt (labelArea.getRight() - 6.0f), topRow.getY(), lanesRow.getBottom());

        auto drawLabel = [&g, labelArea] (juce::String text, juce::Rectangle<float> row)
        {
            g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
            g.setColour (mutedInk().withAlpha (0.70f));
            g.drawFittedText (text, juce::Rectangle<int> (juce::roundToInt (labelArea.getX() + 8.0f), juce::roundToInt (row.getY()),
                                                          juce::roundToInt (labelArea.getWidth() - 16.0f), juce::roundToInt (row.getHeight())),
                              juce::Justification::centredLeft, 1);
        };

        drawLabel ("Top", topRow);
        drawLabel ("Flow", flowRow);
        drawLabel ("Nested", nestedRow);
        drawLabel ("Lanes", lanesRow);
    }

    Hit hitTestExtended (juce::Point<float> point) const
    {
        if (machine == nullptr || machine->states.empty())
            return {};

        const auto total = totalSeconds();
        if (total <= 0.0)
            return {};

        auto timeline = timelineArea();
        auto content = contentArea (timeline);
        const auto rows = extendedRowsFor (content);

        for (int i = 0; i < machine->getStateCount(); ++i)
        {
            auto top = sectionBounds (rows.top, total, i, false);
            if (top.contains (point))
                return { Hit::topState, i, -1 };

            auto nested = sectionBounds (rows.nested, total, i, false).reduced (5.0f, 2.0f);
            if (nested.contains (point))
            {
                if (auto* child = machine->childMachine (i))
                    return { Hit::nestedState, i, childStateIndexAt (*child, nested, point) };
                return { Hit::topState, i, -1 };
            }

            auto lanes = sectionBounds (rows.lanes, total, i, false).reduced (5.0f, 2.0f);
            if (lanes.contains (point))
                return { Hit::lane, i, laneIndexAt (machine->state (i), lanes, point) };
        }

        return {};
    }

    int childStateIndexAt (const MachineModel& child, juce::Rectangle<float> area, juce::Point<float> point) const
    {
        if (child.getStateCount() <= 0)
            return 0;

        auto inner = area.reduced (5.0f, 5.0f);
        const auto gap = 3.0f;
        const auto cellWidth = juce::jmax (8.0f, (inner.getWidth() - gap * static_cast<float> (child.getStateCount() - 1))
                                                  / static_cast<float> (child.getStateCount()));
        const auto index = static_cast<int> ((point.x - inner.getX()) / juce::jmax (1.0f, cellWidth + gap));
        return juce::jlimit (0, child.getStateCount() - 1, index);
    }

    int laneIndexAt (const State& state, juce::Rectangle<float> area, juce::Point<float> point) const
    {
        if (state.lanes.empty())
            return 0;

        auto laneTrack = area.reduced (8.0f, juce::jmax (5.0f, area.getHeight() * 0.30f));
        if (area.getWidth() > 92.0f)
            laneTrack.removeFromRight (52.0f);

        const auto gap = 3.0f;
        const auto maxDots = juce::jlimit (1, static_cast<int> (state.lanes.size()),
                                           juce::jmax (1, juce::roundToInt (laneTrack.getWidth() / 17.0f)));
        const auto chipWidth = juce::jmax (9.0f, juce::jmin (22.0f, (laneTrack.getWidth() - gap * static_cast<float> (maxDots - 1))
                                                                  / static_cast<float> (maxDots)));
        const auto index = static_cast<int> ((point.x - laneTrack.getX()) / juce::jmax (1.0f, chipWidth + gap));
        return juce::jlimit (0, static_cast<int> (state.lanes.size()) - 1, index);
    }

    juce::String tooltipFor (Hit hit) const
    {
        if (machine == nullptr || hit.stateIndex < 0 || hit.stateIndex >= machine->getStateCount())
            return {};

        const auto& state = machine->state (hit.stateIndex);
        if (hit.kind == Hit::nestedState)
            return "Select nested state " + juce::String (hit.detailIndex + 1) + " in " + state.name;
        if (hit.kind == Hit::lane)
        {
            if (hit.detailIndex >= 0 && hit.detailIndex < static_cast<int> (state.lanes.size()))
                return "Select track: " + state.lanes[static_cast<size_t> (hit.detailIndex)].name;
            return "Select tracks in " + state.name;
        }
        if (hit.kind == Hit::topState)
            return "Select " + state.name;

        return {};
    }

    ZoomControlBounds zoomControlBounds() const
    {
        const auto top = getLocalBounds().toFloat().reduced (12.0f, 7.0f).withHeight (22.0f);
        auto x = top.getRight() - 154.0f;
        const auto y = top.getY() + 1.0f;
        const auto h = 19.0f;
        const auto gap = 5.0f;
        ZoomControlBounds bounds;
        bounds.out = { x, y, 23.0f, h };
        x += bounds.out.getWidth() + gap;
        bounds.readout = { x, y, 52.0f, h };
        x += bounds.readout.getWidth() + gap;
        bounds.in = { x, y, 23.0f, h };
        x += bounds.in.getWidth() + gap;
        bounds.fit = { x, y, 46.0f, h };
        return bounds;
    }

    ZoomControl zoomControlAt (juce::Point<float> point) const
    {
        const auto controls = zoomControlBounds();
        if (controls.out.contains (point))     return ZoomControl::zoomOut;
        if (controls.readout.contains (point)) return ZoomControl::readout;
        if (controls.in.contains (point))      return ZoomControl::zoomIn;
        if (controls.fit.contains (point))     return ZoomControl::fit;
        return ZoomControl::none;
    }

    bool handleZoomControlClick (juce::Point<float> point)
    {
        const auto control = zoomControlAt (point);
        if (control == ZoomControl::none)
            return false;

        if (control == ZoomControl::zoomOut)
            setArrangementZoom (arrangementZoom / 1.25f, point.x);
        else if (control == ZoomControl::zoomIn)
            setArrangementZoom (arrangementZoom * 1.25f, point.x);
        else if (control == ZoomControl::fit || control == ZoomControl::readout)
        {
            arrangementZoom = 1.0f;
            arrangementScrollX = 0.0f;
        }

        repaint();
        return true;
    }

    void setArrangementZoom (float newZoom, float anchorX)
    {
        const auto oldZoom = arrangementZoom;
        arrangementZoom = juce::jlimit (1.0f, 6.0f, newZoom);

        auto visible = contentArea (timelineArea());
        const auto mouseInContent = juce::jlimit (0.0f, visible.getWidth(), anchorX - visible.getX());
        const auto worldX = (arrangementScrollX + mouseInContent) / juce::jmax (0.001f, oldZoom);
        arrangementScrollX = worldX * arrangementZoom - mouseInContent;
        clampScroll();
    }

    void drawZoomButton (juce::Graphics& g, juce::Rectangle<float> bounds, juce::String text, bool hovered, bool filled = false)
    {
        g.setColour ((filled ? rowFill().brighter (0.05f) : juce::Colour (0xff12171f))
                         .interpolatedWith (accentA(), hovered ? 0.18f : 0.06f)
                         .withAlpha (0.92f));
        g.fillRoundedRectangle (bounds, 4.0f);
        g.setColour ((hovered ? accentA().brighter (0.12f) : hairline()).withAlpha (hovered ? 0.82f : 0.48f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, hovered ? 1.2f : 0.8f);
        g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        g.setColour (ink().withAlpha (filled ? 0.92f : 0.78f));
        g.drawFittedText (text, bounds.toNearestInt().reduced (2, 0), juce::Justification::centred, 1);
    }

    void drawZoomControls (juce::Graphics& g)
    {
        const auto controls = zoomControlBounds();
        drawZoomButton (g, controls.out, "-", hoveredZoomControl == ZoomControl::zoomOut);
        drawZoomButton (g, controls.readout, juce::String (juce::roundToInt (arrangementZoom * 100.0f)) + "%",
                        hoveredZoomControl == ZoomControl::readout, true);
        drawZoomButton (g, controls.in, "+", hoveredZoomControl == ZoomControl::zoomIn);
        drawZoomButton (g, controls.fit, "Fit", hoveredZoomControl == ZoomControl::fit);
    }

    void drawHoverHighlight (juce::Graphics& g, juce::Rectangle<float> content, double total)
    {
        if (hoveredHit.kind == Hit::none || machine == nullptr || total <= 0.0)
            return;

        auto highlight = juce::Rectangle<float>();
        auto colour = accentA();

        if (extended)
        {
            const auto rows = extendedRowsFor (content);
            colour = graphColour (hoveredHit.stateIndex);

            if (hoveredHit.kind == Hit::topState)
                highlight = sectionBounds (rows.top, total, hoveredHit.stateIndex, false).reduced (1.0f);
            else if (hoveredHit.kind == Hit::nestedState)
                highlight = sectionBounds (rows.nested, total, hoveredHit.stateIndex, false).reduced (5.0f, 2.0f);
            else if (hoveredHit.kind == Hit::lane)
                highlight = sectionBounds (rows.lanes, total, hoveredHit.stateIndex, false).reduced (5.0f, 2.0f);
        }
        else
        {
            colour = graphColour (hoveredHit.stateIndex);
            highlight = sectionBounds (content, total, hoveredHit.stateIndex, false).reduced (1.0f);
        }

        if (highlight.isEmpty())
            return;

        g.setColour (colour.withAlpha (0.10f));
        g.fillRoundedRectangle (highlight.expanded (3.0f, 3.0f), 7.0f);
        g.setColour (colour.brighter (0.24f).withAlpha (0.92f));
        g.drawRoundedRectangle (highlight, 6.0f, 1.3f);
    }

    float currentPlayheadPhase() const
    {
        if (machine == nullptr || playheadAnchorMs <= 0.0 || playheadStateIndex < 0 || playheadStateIndex >= machine->getStateCount())
            return 0.0f;

        const auto duration = juce::jmax (0.1, playheadDurationSeconds);
        const auto age = (juce::Time::getMillisecondCounterHiRes() - playheadAnchorMs) / 1000.0;
        return juce::jlimit (0.0f, 1.0f, static_cast<float> (static_cast<double> (playheadPhaseOffset) + (age / duration)));
    }

    void drawPlayhead (juce::Graphics& g, juce::Rectangle<float> content, double total)
    {
        if (machine == nullptr || playheadMachineId != machine->machineId || playheadStateIndex < 0 || playheadStateIndex >= machine->getStateCount())
            return;

        const auto section = sectionBounds (extended ? extendedRowsFor (content).top : content, total, playheadStateIndex, false);
        if (section.isEmpty())
            return;

        const auto x = section.getX() + section.getWidth() * currentPlayheadPhase();
        if (x < content.getX() - 2.0f || x > content.getRight() + 2.0f)
            return;

        const auto colour = graphColour (playheadStateIndex).brighter (0.25f);
        g.setColour (colour.withAlpha (0.13f));
        g.fillRoundedRectangle (juce::Rectangle<float> (x - 7.0f, content.getY() + 16.0f, 14.0f, content.getHeight() - 22.0f), 6.0f);
        g.setColour (colour.withAlpha (0.96f));
        g.drawVerticalLine (juce::roundToInt (x), content.getY() + 15.0f, content.getBottom() - 5.0f);
        g.fillEllipse (x - 3.2f, section.getY() - 2.0f, 6.4f, 6.4f);
    }

    void drawLengthHandleHighlight (juce::Graphics& g, double total)
    {
        if (machine == nullptr || total <= 0.0)
            return;

        const auto stateIndex = resizingStateIndex >= 0 ? resizingStateIndex : hoveredLengthHandleState;
        if (stateIndex < 0 || stateIndex >= machine->getStateCount())
            return;

        auto bounds = sectionBounds (lengthEditRow(), total, stateIndex, false);
        const auto colour = graphColour (stateIndex);
        const auto x = bounds.getRight();
        g.setColour (colour.withAlpha (resizingStateIndex >= 0 ? 0.95f : 0.72f));
        g.drawLine (juce::Line<float> ({ x, bounds.getY() + 7.0f }, { x, bounds.getBottom() - 7.0f }),
                    resizingStateIndex >= 0 ? 2.2f : 1.5f);

        auto badge = juce::Rectangle<float> (x - 22.0f, bounds.getBottom() - 19.0f, 42.0f, 17.0f);
        g.setColour (juce::Colour (0xff0b0e13).withAlpha (0.86f));
        g.fillRoundedRectangle (badge, 5.0f);
        g.setColour (colour.withAlpha (0.88f));
        g.drawRoundedRectangle (badge.reduced (0.5f), 5.0f, 1.0f);
        g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        g.setColour (ink().withAlpha (0.88f));
        g.drawFittedText (juce::String (machine->state (stateIndex).arrangementBars) + "b",
                          badge.toNearestInt().reduced (2, 0), juce::Justification::centred, 1);
    }

    void drawNestedRow (juce::Graphics& g, juce::Rectangle<float> area, double total)
    {
        for (int i = 0; i < machine->getStateCount(); ++i)
        {
            auto segment = sectionBounds (area, total, i).reduced (0.0f, 1.0f);
            const auto colour = graphColour (i);
            if (const auto* child = machine->childMachine (i))
            {
                drawNestedMachineSummary (g, segment.reduced (5.0f, 2.0f), *child, colour, i == machine->selectedState);
            }
            else if (segment.getWidth() > 28.0f)
            {
                g.setColour (hairline().withAlpha (0.14f));
                g.fillRoundedRectangle (segment.reduced (5.0f, segment.getHeight() * 0.36f), 3.0f);
            }
        }
    }

    void drawLanesRow (juce::Graphics& g, juce::Rectangle<float> area, double total)
    {
        for (int i = 0; i < machine->getStateCount(); ++i)
        {
            auto segment = sectionBounds (area, total, i).reduced (5.0f, 2.0f);
            drawLaneSummary (g, segment, machine->state (i), graphColour (i), i == machine->selectedState);
        }
    }

    void drawSectionText (juce::Graphics& g,
                          juce::Rectangle<float> segment,
                          const State& state,
                          int stateIndex,
                          double seconds,
                          juce::Colour colour,
                          bool selected)
    {
        juce::ignoreUnused (stateIndex);
        auto textArea = segment.toNearestInt().reduced (9, 7).withTrimmedTop (4);
        if (segment.getWidth() < 72.0f)
        {
            g.setColour (colour.withAlpha (selected ? 0.96f : 0.70f));
            g.fillRoundedRectangle (segment.reduced (segment.getWidth() * 0.42f, segment.getHeight() * 0.34f), 2.0f);
            return;
        }

        g.setFont (juce::FontOptions (12.2f, juce::Font::bold));
        g.setColour (selected ? ink() : mutedInk().withAlpha (0.88f));
        g.drawFittedText (state.name, textArea.removeFromTop (18), juce::Justification::centredLeft, 1, 0.92f);

        const auto timing = juce::String (state.tempoBpm, 0) + " BPM  "
                          + juce::String (state.beatsPerBar) + "/" + juce::String (state.beatUnit);
        const auto detail = juce::String (seconds, 1) + "s";
        const auto sectionBars = juce::jlimit (1, 64, state.arrangementBars);
        const auto lengthText = juce::String (sectionBars) + (sectionBars == 1 ? " bar" : " bars");
        g.setFont (juce::FontOptions (10.0f));
        g.setColour (mutedInk().withAlpha (selected ? 0.84f : 0.64f));

        if (segment.getWidth() > 134.0f)
            g.drawFittedText (timing, textArea.removeFromTop (14), juce::Justification::centredLeft, 1);
        else
            g.drawFittedText (detail, textArea.removeFromTop (14), juce::Justification::centredLeft, 1);

        if (segment.getWidth() > 112.0f)
        {
            g.setColour (mutedInk().withAlpha (selected ? 0.74f : 0.50f));
            g.setFont (juce::FontOptions (9.0f));
            g.drawFittedText (lengthText + "  " + detail, textArea.removeFromTop (13), juce::Justification::centredLeft, 1);
        }
    }

    void drawExtendedDetails (juce::Graphics& g,
                              juce::Rectangle<float> area,
                              const State& state,
                              int stateIndex,
                              juce::Colour colour,
                              bool selected)
    {
        if (area.getHeight() < 22.0f || area.getWidth() < 34.0f)
            return;

        auto nestedArea = area.withHeight (juce::jmin (22.0f, area.getHeight() * 0.52f)).reduced (7.0f, 2.0f);
        auto laneArea = area.withTrimmedTop (nestedArea.getHeight() + 5.0f).reduced (7.0f, 1.0f);

        if (const auto* child = machine->childMachine (stateIndex))
            drawNestedMachineSummary (g, nestedArea, *child, colour, selected);
        else if (area.getWidth() > 96.0f)
        {
            g.setColour (hairline().withAlpha (0.24f));
            g.drawHorizontalLine (juce::roundToInt (nestedArea.getCentreY()), nestedArea.getX(), nestedArea.getRight());
        }

        drawLaneSummary (g, laneArea, state, colour, selected);
    }

    void drawNestedMachineSummary (juce::Graphics& g,
                                   juce::Rectangle<float> area,
                                   const MachineModel& child,
                                   juce::Colour parentColour,
                                   bool parentSelected)
    {
        if (child.getStateCount() <= 0 || area.getWidth() < 18.0f)
            return;

        g.setColour (parentColour.withAlpha (parentSelected ? 0.18f : 0.10f));
        g.fillRoundedRectangle (area, 5.0f);
        g.setColour (parentColour.withAlpha (parentSelected ? 0.62f : 0.36f));
        g.drawRoundedRectangle (area.reduced (0.5f), 5.0f, 0.8f);

        auto inner = area.reduced (5.0f, 5.0f);
        auto x = inner.getX();
        const auto gap = 3.0f;
        const auto cellWidth = juce::jmax (8.0f, (inner.getWidth() - gap * static_cast<float> (child.getStateCount() - 1))
                                                  / static_cast<float> (child.getStateCount()));
        for (int i = 0; i < child.getStateCount(); ++i)
        {
            auto cell = juce::Rectangle<float> (x, inner.getY(), cellWidth, inner.getHeight());
            const auto childColour = graphColour (i, 2);
            const auto childSelected = i == child.selectedState;
            g.setColour (rowFill().interpolatedWith (childColour, childSelected ? 0.30f : 0.16f).withAlpha (0.92f));
            g.fillRoundedRectangle (cell, 3.0f);
            g.setColour (childSelected ? childColour.brighter (0.10f).withAlpha (0.95f) : childColour.withAlpha (0.50f));
            g.drawRoundedRectangle (cell.reduced (0.25f), 3.0f, childSelected ? 1.1f : 0.6f);

            if (const auto* grandchild = child.childMachine (i))
            {
                auto mark = cell.removeFromBottom (2.0f).reduced (1.0f, 0.0f);
                g.setColour (graphColour (i, 5).withAlpha (0.86f));
                g.fillRoundedRectangle (mark, 1.0f);
                juce::ignoreUnused (grandchild);
            }

            if (cellWidth > 22.0f)
            {
                g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
                g.setColour (ink().withAlpha (childSelected ? 0.82f : 0.52f));
                g.drawFittedText (juce::String (i + 1), cell.toNearestInt().reduced (1, 0), juce::Justification::centred, 1);
            }
            x += cellWidth + gap;
        }
    }

    void drawLaneSummary (juce::Graphics& g,
                          juce::Rectangle<float> area,
                          const State& state,
                          juce::Colour stateColour,
                          bool selected)
    {
        if (state.lanes.empty() || area.getHeight() < 9.0f || area.getWidth() < 18.0f)
            return;

        g.setColour (juce::Colour (0xff0d1015).withAlpha (selected ? 0.70f : 0.46f));
        g.fillRoundedRectangle (area, 5.0f);

        auto laneTrack = area.reduced (8.0f, juce::jmax (5.0f, area.getHeight() * 0.30f));
        const auto countText = juce::String (state.lanes.size()) + (state.lanes.size() == 1 ? " lane" : " lanes");
        if (area.getWidth() > 92.0f)
            laneTrack.removeFromRight (52.0f);

        if (! state.lanes.empty())
        {
            auto x = laneTrack.getX();
            const auto gap = 3.0f;
            const auto maxDots = juce::jlimit (1, static_cast<int> (state.lanes.size()),
                                               juce::jmax (1, juce::roundToInt (laneTrack.getWidth() / 17.0f)));
            const auto chipWidth = juce::jmax (9.0f, juce::jmin (22.0f, (laneTrack.getWidth() - gap * static_cast<float> (maxDots - 1))
                                                                      / static_cast<float> (maxDots)));
            for (int i = 0; i < maxDots; ++i)
            {
                const auto& lane = state.lanes[static_cast<size_t> (i)];
                auto laneColour = graphColour (i, state.index).interpolatedWith (stateColour, 0.24f);
                if (! lane.enabled || lane.muted)
                    laneColour = mutedInk().withAlpha (0.42f);

                g.setColour (laneColour.withAlpha (lane.solo ? 1.0f : 0.86f));
                auto chip = juce::Rectangle<float> (x, laneTrack.getY(), chipWidth, laneTrack.getHeight());
                g.fillRoundedRectangle (chip, 3.0f);
                if (lane.frozen)
                {
                    g.setColour (ink().withAlpha (0.58f));
                    g.drawRoundedRectangle (chip.reduced (0.6f), 3.0f, 0.7f);
                }
                x += chipWidth + gap;
            }
        }

        if (area.getWidth() > 92.0f)
        {
            g.setColour (mutedInk().withAlpha (selected ? 0.76f : 0.54f));
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawFittedText (countText, area.toNearestInt().removeFromRight (58).reduced (2, 0),
                              juce::Justification::centredRight, 1);
        }
    }

    void drawRuler (juce::Graphics& g, juce::Rectangle<float> area, double total)
    {
        const auto baselineY = area.getY() + 14.0f;
        g.setColour (hairline().withAlpha (0.30f));
        g.drawHorizontalLine (juce::roundToInt (baselineY), area.getX(), area.getRight());

        auto bar = 1;
        for (int i = 0; i < machine->getStateCount(); ++i)
        {
            const auto bounds = sectionBounds (area, total, i, false);
            const auto tickX = bounds.getX();
            const auto width = bounds.getWidth();
            g.setColour (hairline().withAlpha (i == 0 ? 0.50f : 0.30f));
            g.drawVerticalLine (juce::roundToInt (tickX), baselineY - 5.0f, baselineY + 5.0f);

            if (width > 38.0f)
            {
                g.setFont (juce::FontOptions (8.5f));
                g.setColour (mutedInk().withAlpha (0.50f));
                g.drawFittedText (juce::String (bar),
                                  juce::Rectangle<int> (juce::roundToInt (tickX + 4.0f), juce::roundToInt (baselineY - 13.0f), 28, 11),
                                  juce::Justification::centredLeft, 1);
            }
            ++bar;
        }

        g.setColour (hairline().withAlpha (0.36f));
        g.drawVerticalLine (juce::roundToInt (area.getRight()), baselineY - 5.0f, baselineY + 5.0f);
    }

    void drawTransitionFlow (juce::Graphics& g, juce::Rectangle<float> area, double total, juce::Rectangle<float> explicitFlowArea = {})
    {
        if (machine == nullptr || machine->rules.empty() || machine->getStateCount() < 2)
            return;

        auto flowArea = explicitFlowArea.isEmpty() ? area.withTrimmedTop (juce::jmax (34.0f, area.getHeight() * 0.46f)).withTrimmedBottom (8.0f)
                                                   : explicitFlowArea;
        if (flowArea.getHeight() < 18.0f)
            return;

        for (const auto& rule : machine->rules)
        {
            if (rule.weight <= 0.0f || rule.from == rule.to
                || rule.from < 0 || rule.from >= machine->getStateCount()
                || rule.to < 0 || rule.to >= machine->getStateCount())
                continue;

            const auto from = sectionBounds (area, total, rule.from, false);
            const auto to = sectionBounds (area, total, rule.to, false);
            if (from.isEmpty() || to.isEmpty())
                continue;

            const auto weight = juce::jlimit (0.0f, 1.0f, rule.weight / 12.0f);
            const auto colour = graphColour (rule.from).interpolatedWith (graphColour (rule.to), 0.45f);
            const auto start = juce::Point<float> (from.getCentreX(), flowArea.getY() + 3.0f);
            const auto end = juce::Point<float> (to.getCentreX(), flowArea.getY() + 3.0f);
            const auto span = juce::jlimit (0.0f, 1.0f, std::abs (end.x - start.x) / juce::jmax (1.0f, area.getWidth()));
            const auto maxDrop = juce::jmax (20.0f, flowArea.getHeight() - 2.0f);
            const auto drop = juce::jlimit (20.0f, maxDrop,
                                            flowArea.getHeight() * (0.46f + 0.46f * span));
            juce::Path path;
            path.startNewSubPath (start);
            path.quadraticTo ((start.x + end.x) * 0.5f, flowArea.getY() + drop, end.x, end.y);

            g.setColour (colour.withAlpha (0.34f + weight * 0.48f));
            g.strokePath (path, juce::PathStrokeType (1.4f + weight * 3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            if (weight > 0.18f && std::abs (end.x - start.x) > 24.0f)
            {
                g.setColour (colour.withAlpha (0.72f));
                g.fillEllipse (end.x - 2.2f, end.y - 2.2f, 4.4f, 4.4f);
            }
        }
    }

    void drawExportProgress (juce::Graphics& g, juce::Rectangle<float> area, double total)
    {
        if (! exportInProgress || exportTotalSeconds <= 0.0)
            return;

        const auto progress = juce::jlimit (0.0f, 1.0f, static_cast<float> (exportElapsedSeconds / exportTotalSeconds));
        auto progressArea = area.withHeight (4.0f).withY (area.getBottom() - 4.0f);
        g.setColour (juce::Colour (0xff111318).withAlpha (0.88f));
        g.fillRoundedRectangle (progressArea, 2.0f);
        g.setColour (accentA().withAlpha (0.92f));
        g.fillRoundedRectangle (progressArea.withWidth (progressArea.getWidth() * progress), 2.0f);
        juce::ignoreUnused (total);
    }

    float selfWeightFor (int stateIndex) const
    {
        if (machine == nullptr)
            return 0.0f;

        for (const auto& rule : machine->rules)
            if (rule.from == stateIndex && rule.to == stateIndex)
                return rule.weight;

        return 0.0f;
    }

    int stateIndexAt (juce::Point<float> point) const
    {
        if (machine == nullptr || machine->states.empty())
            return -1;

        auto area = contentArea (timelineArea());
        if (! area.contains (point))
            return -1;

        auto total = totalSeconds();

        for (int i = 0; i < machine->getStateCount(); ++i)
        {
            const auto bounds = sectionBounds (area, total, i, false);
            if (bounds.contains (point))
                return i;
        }

        return -1;
    }

    MachineModel* machine = nullptr;
    double rate = 1.0;
    bool extended = false;
    float arrangementZoom = 1.0f;
    float arrangementScrollX = 0.0f;
    bool exportInProgress = false;
    double exportElapsedSeconds = 0.0;
    double exportTotalSeconds = 0.0;
    Hit hoveredHit;
    ZoomControl hoveredZoomControl = ZoomControl::none;
    int hoveredLengthHandleState = -1;
    int resizingStateIndex = -1;
    int resizingStartBars = 1;
    float resizingStartX = 0.0f;
    float resizingStartWidth = 1.0f;
    juce::String playheadMachineId;
    int playheadStateIndex = -1;
    float playheadPhaseOffset = 0.0f;
    double playheadAnchorMs = 0.0;
    double playheadDurationSeconds = 0.0;
    static constexpr double visualSchedulerCompensationMs = 0.0;
    juce::String hoverHint;
};

class OrbitTrackCanvas final : public juce::Component,
                               private juce::Timer
{
public:
    std::function<void (int)> onTrackSelected;
    std::function<void (int, int)> onLaneSelected;
    std::function<void (int)> onTrackFocusRequested;
    std::function<void()> onTrackFocusCleared;
    std::function<void (int, float)> onScriptDropRequested;
    std::function<void (int, int, float)> onWarpChanged;
    std::function<void (int, int, float, bool)> onLanePhaseChanged;
    std::function<void (int, int, float, float, bool)> onLaneTrimChanged;
    std::function<void (int, int, double, double, bool)> onLaneFadeChanged;
    std::function<void (int, int)> onRenderedLaneSelected;
    std::function<void (int, int)> onDeleteRenderedLaneRequested;
    std::function<void()> onDeleteSelectedLaneRequested;
    std::function<void (int, int, float, int)> onConnectionRequested;

    enum class PlayheadMode
    {
        forward,
        reverse,
        paused
    };

    struct StatePlayhead
    {
        PlayheadMode mode = PlayheadMode::forward;
        double startedMs = 0.0;
        float startPhase = 0.0f;
    };

    OrbitTrackCanvas()
    {
        formatManager.registerBasicFormats();
        startTimerHz (45);
        setWantsKeyboardFocus (true);
    }

    void setMachine (MachineModel& rootMachine, double playbackRate, bool running, int focusedTrack)
    {
        machine = &rootMachine;
        rate = juce::jmax (0.05, playbackRate);
        transportRunning = running;
        const auto newFocusedTrack = focusedTrack >= 0 && focusedTrack < machine->getStateCount() ? focusedTrack : -1;
        if (newFocusedTrack != focusedTrackIndex)
        {
            focusedViewZoom = 1.0f;
            focusedViewPan = {};
            panningFocusedView = false;
        }

        focusedTrackIndex = newFocusedTrack;
        repaint();
    }

    void invalidateWaveforms()
    {
        waveforms.clear();
        repaint();
    }

    void setShapeEditMode (bool shouldEdit)
    {
        shapeEditMode = shouldEdit;
        repaint();
    }

    void resetTransportStart()
    {
        visualTransportStartMs = juce::Time::getMillisecondCounterHiRes();
        statePlayheads.clear();
        repaint();
    }

    void setReversePlayhead (int stateIndex, bool reversed)
    {
        if (reversed)
            setReversePlayheadFromPhase (stateIndex, currentPlayheadPhaseForState (stateIndex));
        else
            statePlayheads.erase (stateIndex);

        repaint();
    }

    void setReversePlayheadFromPhase (int stateIndex, float phase)
    {
        statePlayheads[stateIndex] = { PlayheadMode::reverse,
                                       juce::Time::getMillisecondCounterHiRes(),
                                       juce::jlimit (0.0f, 0.9999f, phase) };
        repaint();
    }

    void setForwardPlayheadFromPhase (int stateIndex, float phase)
    {
        statePlayheads[stateIndex] = { PlayheadMode::forward,
                                       juce::Time::getMillisecondCounterHiRes(),
                                       juce::jlimit (0.0f, 0.9999f, phase) };
        repaint();
    }

    void setRestartPlayhead (int stateIndex)
    {
        statePlayheads[stateIndex] = { PlayheadMode::forward, juce::Time::getMillisecondCounterHiRes(), 0.0f };
        repaint();
    }

    void setPausedPlayhead (int stateIndex)
    {
        statePlayheads[stateIndex] = { PlayheadMode::paused,
                                       juce::Time::getMillisecondCounterHiRes(),
                                       currentPlayheadPhaseForState (stateIndex) };
        repaint();
    }

    bool isPlayheadPaused (int stateIndex) const
    {
        if (const auto found = statePlayheads.find (stateIndex); found != statePlayheads.end())
            return found->second.mode == PlayheadMode::paused;

        return false;
    }

    bool isPlayheadReversed (int stateIndex) const
    {
        if (const auto found = statePlayheads.find (stateIndex); found != statePlayheads.end())
            return found->second.mode == PlayheadMode::reverse;

        return false;
    }

    float playheadPhaseForState (int stateIndex) const
    {
        return currentPlayheadPhaseForState (stateIndex);
    }

    void clearReversePlayheads()
    {
        statePlayheads.clear();
        repaint();
    }

    void setSelectedRenderedLane (int stateIndex, int laneIndex)
    {
        selectedRenderedTrack = stateIndex;
        selectedRenderedLane = laneIndex;
        repaint();
    }

    void clearSelectedRenderedLane()
    {
        clearRenderedSelection();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (backgroundTop().interpolatedWith (backgroundBottom(), 0.50f).withAlpha (0.96f));
        g.fillRoundedRectangle (bounds.reduced (2.0f), 5.0f);
        g.setColour (hairline().withAlpha (0.22f));
        g.drawRoundedRectangle (bounds.reduced (2.0f), 5.0f, 0.75f);

        if (machine == nullptr || machine->states.empty())
            return;

        layoutTracks();
        drawConnections (g);
        drawTracks (g);
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        grabKeyboardFocus();
        draggingTrack = -1;
        draggingWarpIndex = -1;
        draggingLaneTrack = -1;
        draggingLaneIndex = -1;
        draggingLaneHasMoved = false;
        draggingTrim = TrimHandle::none;
        panningFocusedView = false;
        panningOverviewView = false;

        if (auto handle = trimHandleHitTest (event.position); handle.track >= 0 && handle.lane >= 0)
        {
            selectedRenderedTrack = handle.track;
            selectedRenderedLane = handle.lane;
            draggingLaneTrack = handle.track;
            draggingLaneIndex = handle.lane;
            draggingTrim = handle.handle;

            if (auto* layout = layoutForState (handle.track))
            {
                auto& state = machine->state (handle.track);
                const auto& lane = state.lanes[static_cast<size_t> (handle.lane)];
                trimStartPhase = lane.orbitPhase;
                trimEndPhase = juce::jlimit (trimStartPhase + 0.002f, 0.9999f, trimStartPhase + lanePhaseSpan (state, lane));
                juce::ignoreUnused (layout);
            }

            if (onRenderedLaneSelected)
                onRenderedLaneSelected (handle.track, handle.lane);

            repaint();
            return;
        }

        if (focusedTrackIndex < 0 && pendingConnection.active)
        {
            const auto target = stateAtPoint (event.position);
            if (target >= 0 && onConnectionRequested)
                onConnectionRequested (pendingConnection.sourceState,
                                       pendingConnection.sourceLane,
                                       pendingConnection.sourcePhase,
                                       target);

            pendingConnection = {};
            repaint();
            return;
        }

        if (auto hit = waveformHitTest (event.position); hit.track >= 0 && hit.lane >= 0)
        {
            selectedRenderedTrack = hit.track;
            selectedRenderedLane = hit.lane;
            draggingLaneTrack = hit.track;
            draggingLaneIndex = hit.lane;

            if (onRenderedLaneSelected)
                onRenderedLaneSelected (hit.track, hit.lane);

            repaint();
            return;
        }

        if (shapeEditMode)
        {
            for (int i = 0; i < static_cast<int> (trackLayouts.size()); ++i)
            {
                const auto warpIndex = warpHandleAt (i, event.position);
                if (warpIndex >= 0)
                {
                    draggingTrack = trackLayouts[static_cast<size_t> (i)].stateIndex;
                    draggingWarpIndex = warpIndex;
                    clearRenderedSelection();
                    if (onTrackSelected)
                        onTrackSelected (draggingTrack);
                    return;
                }
            }
        }

        if (auto hit = orbitHitTest (event.position); hit.track >= 0)
        {
            if (focusedTrackIndex < 0 && hit.lane >= 0 && event.mods.isPopupMenu())
            {
                clearRenderedSelection();
                pendingConnection = { true, hit.track, hit.lane, hit.phase, event.position };
                machine->selectedState = hit.track;
                machine->selectedLane = hit.lane;
                if (onLaneSelected)
                    onLaneSelected (hit.track, hit.lane);
                repaint();
                return;
            }

            clearRenderedSelection();
            machine->selectedState = hit.track;
            if (hit.lane >= 0)
                machine->selectedLane = hit.lane;
            if (hit.lane >= 0 && onLaneSelected)
                onLaneSelected (hit.track, hit.lane);
            else if (onTrackSelected)
                onTrackSelected (hit.track);

            if (hit.lane >= 0 && isPlacedLane (machine->state (hit.track).lanes[static_cast<size_t> (hit.lane)]))
            {
                draggingLaneTrack = hit.track;
                draggingLaneIndex = hit.lane;
            }

            repaint();
            return;
        }

        if (focusedTrackIndex >= 0 && event.mods.isPopupMenu())
        {
            panningFocusedView = true;
            focusedPanStart = focusedViewPan;
            focusedPanDragStart = event.position;
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
            return;
        }

        if (focusedTrackIndex < 0 && event.mods.isPopupMenu())
        {
            panningOverviewView = true;
            overviewPanStart = overviewViewPan;
            overviewPanDragStart = event.position;
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        }
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        if (panningOverviewView)
        {
            overviewViewPan = overviewPanStart + (event.position - overviewPanDragStart);
            repaint();
            return;
        }

        if (panningFocusedView)
        {
            focusedViewPan = focusedPanStart + (event.position - focusedPanDragStart);
            repaint();
            return;
        }

        if (machine != nullptr && draggingTrim != TrimHandle::none && draggingLaneTrack >= 0 && draggingLaneIndex >= 0)
        {
            updateDraggedTrim (event.position, false);
            return;
        }

        if (machine != nullptr && draggingLaneTrack >= 0 && draggingLaneIndex >= 0)
        {
            draggingLaneHasMoved = true;
            updateDraggedLanePhase (event.position, false);
            return;
        }

        if (machine == nullptr || draggingTrack < 0 || draggingWarpIndex < 0)
            return;

        auto* layout = layoutForState (draggingTrack);
        if (layout == nullptr)
            return;

        const auto angle = angleForWarpIndex (draggingWarpIndex);
        const auto normal = juce::Point<float> (std::cos (angle), std::sin (angle));
        const auto radial = (event.position - layout->centre).getDotProduct (normal);
        const auto warp = juce::jlimit (-0.32f, 0.42f, (radial - layout->radius) / juce::jmax (24.0f, layout->radius));
        machine->state (draggingTrack).orbitWarp[static_cast<size_t> (draggingWarpIndex)] = warp;
        if (onWarpChanged)
            onWarpChanged (draggingTrack, draggingWarpIndex, warp);
        repaint();
    }

    void mouseMove (const juce::MouseEvent& event) override
    {
        if (pendingConnection.active)
        {
            pendingConnection.mouse = event.position;
            repaint();
        }

        setMouseCursor (trimHandleHitTest (event.position).track >= 0 || waveformHitTest (event.position).track >= 0
                            ? juce::MouseCursor::DraggingHandCursor
                            : juce::MouseCursor::NormalCursor);
    }

    void mouseUp (const juce::MouseEvent& event) override
    {
        if (machine != nullptr && draggingTrim != TrimHandle::none && draggingLaneTrack >= 0 && draggingLaneIndex >= 0)
            updateDraggedTrim (event.position, true);

        if (machine != nullptr && draggingLaneTrack >= 0 && draggingLaneIndex >= 0 && draggingLaneHasMoved)
            updateDraggedLanePhase (event.position, true);

        draggingTrack = -1;
        draggingWarpIndex = -1;
        draggingLaneTrack = -1;
        draggingLaneIndex = -1;
        draggingLaneHasMoved = false;
        draggingTrim = TrimHandle::none;
        panningFocusedView = false;
        panningOverviewView = false;
        setMouseCursor (juce::MouseCursor::NormalCursor);
    }

    void mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
    {
        if (machine == nullptr)
            return;

        const auto wheelDelta = std::abs (wheel.deltaY) > std::abs (wheel.deltaX) ? wheel.deltaY : wheel.deltaX;
        if (std::abs (wheelDelta) < 0.0001f)
            return;

        if (focusedTrackIndex < 0)
        {
            const auto anchor = getLocalBounds().toFloat().getCentre();
            const auto oldZoom = overviewViewZoom;
            const auto basePoint = anchor + ((event.position - anchor - overviewViewPan) / juce::jmax (0.001f, oldZoom));
            const auto zoomFactor = std::pow (1.16f, wheelDelta * 7.0f);
            overviewViewZoom = juce::jlimit (0.55f, 5.0f, overviewViewZoom * zoomFactor);

            if (std::abs (overviewViewZoom - oldZoom) < 0.0001f)
                return;

            overviewViewPan = event.position - anchor - ((basePoint - anchor) * overviewViewZoom);
            repaint();
            return;
        }

        layoutTracks();
        auto* layout = layoutForState (focusedTrackIndex);
        const auto oldCentre = layout != nullptr ? layout->centre : getLocalBounds().toFloat().getCentre();
        const auto oldZoom = focusedViewZoom;
        const auto zoomFactor = std::pow (1.16f, wheelDelta * 7.0f);
        focusedViewZoom = juce::jlimit (0.55f, 5.0f, focusedViewZoom * zoomFactor);

        if (std::abs (focusedViewZoom - oldZoom) < 0.0001f)
            return;

        const auto ratio = focusedViewZoom / juce::jmax (0.001f, oldZoom);
        const auto newCentre = event.position - (event.position - oldCentre) * ratio;
        focusedViewPan += newCentre - oldCentre;
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& event) override
    {
        pendingConnection = {};

        if (auto hit = orbitHitTest (event.position); hit.track >= 0)
        {
            if (focusedTrackIndex >= 0)
                onScriptDropRequested (hit.track, hit.phase);
            else if (onTrackFocusRequested)
                onTrackFocusRequested (hit.track);
            return;
        }

        if (focusedTrackIndex >= 0 && onTrackFocusCleared)
            onTrackFocusCleared();
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (isLaneDeleteKey (key))
        {
            if (selectedRenderedTrack >= 0 && selectedRenderedLane >= 0 && onDeleteRenderedLaneRequested)
            {
                onDeleteRenderedLaneRequested (selectedRenderedTrack, selectedRenderedLane);
                clearRenderedSelection();
                repaint();
                return true;
            }

            if (onDeleteSelectedLaneRequested)
                onDeleteSelectedLaneRequested();
            return true;
        }

        if (key.getKeyCode() == juce::KeyPress::escapeKey && focusedTrackIndex >= 0)
        {
            if (onTrackFocusCleared)
                onTrackFocusCleared();
            return true;
        }

        if (key.getKeyCode() == juce::KeyPress::escapeKey && pendingConnection.active)
        {
            pendingConnection = {};
            repaint();
            return true;
        }

        return false;
    }

private:
    struct TrackLayout
    {
        int stateIndex = 0;
        juce::Point<float> centre;
        float radius = 1.0f;
        float laneGap = 18.0f;
        float outerRadius = 1.0f;
    };

    struct Hit
    {
        int track = -1;
        int lane = -1;
        float phase = 0.0f;
    };

    enum class TrimHandle
    {
        none,
        start,
        end,
        fadeIn,
        fadeOut
    };

    struct TrimHit
    {
        int track = -1;
        int lane = -1;
        TrimHandle handle = TrimHandle::none;
    };

    struct PendingConnection
    {
        bool active = false;
        int sourceState = -1;
        int sourceLane = -1;
        float sourcePhase = 0.0f;
        juce::Point<float> mouse;
    };

    void timerCallback() override
    {
        if (isShowing() && transportRunning)
            repaint();
    }

    void layoutTracks()
    {
        trackLayouts.clear();
        auto area = getLocalBounds().toFloat().reduced (16.0f, 14.0f);
        std::vector<int> stateIndices;
        if (focusedTrackIndex >= 0)
            stateIndices.push_back (focusedTrackIndex);
        else
            for (int i = 0; i < machine->getStateCount(); ++i)
                stateIndices.push_back (i);

        const auto count = static_cast<int> (stateIndices.size());
        if (count <= 0 || area.getWidth() <= 0.0f || area.getHeight() <= 0.0f)
            return;

        if (focusedTrackIndex < 0)
        {
            layoutOverviewTracks (area, stateIndices);
            return;
        }

        const auto columns = 1;
        const auto rows = juce::jmax (1, static_cast<int> (std::ceil (static_cast<float> (count) / static_cast<float> (juce::jmax (1, columns)))));
        const auto cellW = area.getWidth() / static_cast<float> (juce::jmax (1, columns));
        const auto cellH = area.getHeight() / static_cast<float> (rows);

        for (int i = 0; i < count; ++i)
        {
            const auto stateIndex = stateIndices[static_cast<size_t> (i)];
            const auto col = i % columns;
            const auto row = i / columns;
            const auto rowStart = row * columns;
            const auto itemsInRow = juce::jmin (columns, count - rowStart);
            const auto rowInset = focusedTrackIndex >= 0 ? 0.0f : (static_cast<float> (columns - itemsInRow) * cellW * 0.5f);
            auto cell = juce::Rectangle<float> (area.getX() + rowInset + cellW * static_cast<float> (col),
                                                area.getY() + cellH * static_cast<float> (row),
                                                cellW, cellH).reduced (28.0f);
            const auto metrics = metricsForCell (cell, machine->state (stateIndex), true);
            constexpr float labelHeight = 34.0f;
            const auto verticalCentre = cell.withTrimmedBottom (labelHeight).getCentreY();
            auto centre = juce::Point<float> { cell.getCentreX(), verticalCentre };
            auto radius = metrics.radius;
            auto laneGap = metrics.laneGap;
            auto outerRadius = metrics.outerRadius;

            centre += focusedViewPan;
            radius *= focusedViewZoom;
            laneGap *= focusedViewZoom;
            outerRadius *= focusedViewZoom;

            trackLayouts.push_back ({ stateIndex, centre, radius, laneGap, outerRadius });
        }
    }

    struct TrackMetrics
    {
        float radius = 34.0f;
        float laneGap = 10.0f;
        float outerRadius = 42.0f;
    };

    void layoutOverviewTracks (juce::Rectangle<float> area, const std::vector<int>& stateIndices)
    {
        constexpr float labelHeight = 38.0f;
        constexpr float minGap = 20.0f;

        const auto count = static_cast<int> (stateIndices.size());
        const auto columns = chooseColumnCount (count);
        const auto rows = juce::jmax (1, static_cast<int> (std::ceil (static_cast<float> (count) / static_cast<float> (juce::jmax (1, columns)))));

        std::vector<TrackMetrics> metrics;
        metrics.reserve (stateIndices.size());
        for (auto stateIndex : stateIndices)
            metrics.push_back (overviewMetricsForState (machine->state (stateIndex)));

        auto maxOuterRadius = 1.0f;
        for (const auto& itemMetrics : metrics)
            maxOuterRadius = juce::jmax (maxOuterRadius, itemMetrics.outerRadius);

        const auto slotWidth = maxOuterRadius * 2.0f;
        const auto slotHeight = maxOuterRadius * 2.0f + labelHeight;
        const auto requiredWidth = static_cast<float> (columns) * slotWidth
                                 + static_cast<float> (juce::jmax (0, columns - 1)) * minGap;
        const auto requiredHeight = static_cast<float> (rows) * slotHeight
                                  + static_cast<float> (juce::jmax (0, rows - 1)) * minGap;
        const auto fitScale = juce::jlimit (0.12f, 1.65f,
                                            juce::jmin (area.getWidth() / juce::jmax (1.0f, requiredWidth),
                                                        area.getHeight() / juce::jmax (1.0f, requiredHeight)) * 0.96f);

        const auto scaledSlotWidth = slotWidth * fitScale;
        const auto scaledGap = minGap * fitScale;
        const auto scaledMaxOuterRadius = maxOuterRadius * fitScale;
        const auto scaledHeight = requiredHeight * fitScale;
        const auto firstRowCentreY = area.getCentreY() - scaledHeight * 0.5f + scaledMaxOuterRadius;
        const auto rowCentreStep = (slotHeight + minGap) * fitScale;

        for (int row = 0; row < rows; ++row)
        {
            const auto start = row * columns;
            const auto items = juce::jmin (columns, count - start);
            if (items <= 0)
                continue;

            const auto rowWidth = static_cast<float> (items) * scaledSlotWidth
                                + static_cast<float> (juce::jmax (0, items - 1)) * scaledGap;
            auto x = area.getCentreX() - rowWidth * 0.5f;
            const auto rowCentreY = firstRowCentreY + static_cast<float> (row) * rowCentreStep;

            for (int item = 0; item < items; ++item)
            {
                const auto index = start + item;
                auto itemMetrics = metrics[static_cast<size_t> (index)];
                itemMetrics.radius *= fitScale;
                itemMetrics.laneGap *= fitScale;
                itemMetrics.outerRadius *= fitScale;

                const auto centre = juce::Point<float> { x + scaledSlotWidth * 0.5f,
                                                         rowCentreY };
                trackLayouts.push_back ({ stateIndices[static_cast<size_t> (index)],
                                          centre,
                                          itemMetrics.radius,
                                          itemMetrics.laneGap,
                                          itemMetrics.outerRadius });
                x += scaledSlotWidth + scaledGap;
            }
        }

        applyOverviewViewTransform();
    }

    void applyOverviewViewTransform()
    {
        if (std::abs (overviewViewZoom - 1.0f) < 0.0001f && overviewViewPan.getDistanceFromOrigin() < 0.001f)
            return;

        const auto anchor = getLocalBounds().toFloat().getCentre();
        for (auto& layout : trackLayouts)
        {
            layout.centre = anchor + overviewViewPan + ((layout.centre - anchor) * overviewViewZoom);
            layout.radius *= overviewViewZoom;
            layout.laneGap *= overviewViewZoom;
            layout.outerRadius *= overviewViewZoom;
        }
    }

    TrackMetrics overviewMetricsForState (const State& state) const
    {
        const auto laneCount = juce::jmax (1, static_cast<int> (state.lanes.size()));
        const auto hasRenderedLanes = std::any_of (state.lanes.begin(), state.lanes.end(), [] (const Lane& lane)
        {
            return lane.freezeInProgress || (lane.frozen && lane.frozenAudioPath.isNotEmpty());
        });

        const auto baseRadius = hasRenderedLanes ? 72.0f : 82.0f;
        const auto laneGap = hasRenderedLanes ? 36.0f : 15.0f;
        const auto maxPositiveWarp = juce::jlimit (0.0f, 0.42f, *std::max_element (state.orbitWarp.begin(), state.orbitWarp.end()));
        const auto pipeAllowance = hasRenderedLanes ? 28.0f : 8.0f;
        const auto handleAllowance = hasRenderedLanes ? 16.0f : 6.0f;
        const auto outer = (baseRadius + static_cast<float> (laneCount - 1) * laneGap) * (1.0f + maxPositiveWarp)
                         + pipeAllowance + handleAllowance;

        return { baseRadius, laneGap, outer };
    }

    TrackMetrics metricsForCell (juce::Rectangle<float> cell, const State& state, bool focused = false) const
    {
        constexpr float labelHeight = 34.0f;
        const auto safety = focused ? 22.0f : 9.0f;
        const auto laneCount = juce::jmax (1, static_cast<int> (state.lanes.size()));
        auto laneGap = juce::jlimit (focused ? 12.0f : 7.0f, focused ? 24.0f : 14.0f,
                                     juce::jmin (cell.getWidth(), cell.getHeight()) / (focused ? 18.0f : 24.0f));
        const auto hasRenderedLanes = std::any_of (state.lanes.begin(), state.lanes.end(), [] (const Lane& lane)
        {
            return lane.freezeInProgress || (lane.frozen && lane.frozenAudioPath.isNotEmpty());
        });
        if (hasRenderedLanes)
            laneGap = juce::jmax (laneGap, focused ? 58.0f : 38.0f);

        const auto maxPositiveWarp = juce::jlimit (0.0f, 0.42f, *std::max_element (state.orbitWarp.begin(), state.orbitWarp.end()));
        const auto pipeAllowance = hasRenderedLanes ? (focused ? 30.0f : 18.0f) : 5.0f;

        auto solveRadius = [&] (float gap)
        {
            const auto laneOffset = static_cast<float> (laneCount - 1) * gap;
            const auto availableX = cell.getWidth() * 0.5f - safety;
            const auto availableY = (cell.getHeight() - labelHeight) * 0.5f - safety;
            return (juce::jmin (availableX, availableY) - pipeAllowance) / (1.0f + maxPositiveWarp) - laneOffset;
        };

        auto radius = solveRadius (laneGap);
        if (radius < 30.0f && laneCount > 1)
        {
            laneGap = juce::jlimit (hasRenderedLanes ? 22.0f : 5.0f, laneGap, cell.getHeight() / static_cast<float> (laneCount + 14));
            radius = solveRadius (laneGap);
        }

        radius = juce::jlimit (focused ? 88.0f : 24.0f, focused ? 260.0f : 92.0f, radius);
        const auto outer = (radius + static_cast<float> (laneCount - 1) * laneGap) * (1.0f + maxPositiveWarp) + pipeAllowance;
        return { radius, laneGap, outer };
    }

    int chooseColumnCount (int count) const
    {
        if (count <= 3)
            return juce::jmax (1, count);

        return juce::jmax (2, static_cast<int> (std::ceil (std::sqrt (static_cast<float> (count)))));
    }

    float angleForWarpIndex (int index) const
    {
        return juce::MathConstants<float>::twoPi * static_cast<float> (index) / 8.0f
             - juce::MathConstants<float>::halfPi;
    }

    float radiusForAngle (const State& state, float baseRadius, float angle) const
    {
        const auto normalised = std::fmod ((angle + juce::MathConstants<float>::halfPi + juce::MathConstants<float>::twoPi),
                                           juce::MathConstants<float>::twoPi)
                              / juce::MathConstants<float>::twoPi;
        const auto scaled = normalised * 8.0f;
        const auto i0 = juce::jlimit (0, 7, static_cast<int> (std::floor (scaled)));
        const auto i1 = (i0 + 1) % 8;
        const auto t = scaled - static_cast<float> (i0);
        const auto warp = state.orbitWarp[static_cast<size_t> (i0)] * (1.0f - t)
                        + state.orbitWarp[static_cast<size_t> (i1)] * t;
        return baseRadius * (1.0f + warp);
    }

    juce::Point<float> pointOnTrack (const State& state, const TrackLayout& layout, float phase, float laneOffset = 0.0f) const
    {
        const auto angle = phase * juce::MathConstants<float>::twoPi - juce::MathConstants<float>::halfPi;
        const auto radius = radiusForAngle (state, layout.radius + laneOffset, angle);
        return layout.centre + juce::Point<float> (std::cos (angle), std::sin (angle)) * radius;
    }

    juce::Path makeTrackPath (const State& state, const TrackLayout& layout, float laneOffset = 0.0f) const
    {
        juce::Path path;
        constexpr int segments = 144;
        for (int i = 0; i <= segments; ++i)
        {
            const auto phase = static_cast<float> (i) / static_cast<float> (segments);
            const auto point = pointOnTrack (state, layout, phase, laneOffset);
            if (i == 0)
                path.startNewSubPath (point);
            else
                path.lineTo (point);
        }
        path.closeSubPath();
        return path;
    }

    void drawConnections (juce::Graphics& g)
    {
        if (focusedTrackIndex >= 0 || machine == nullptr)
            return;

        for (const auto& connection : machine->orbitConnections)
        {
            auto* sourceLayout = layoutForState (connection.sourceState);
            auto* targetLayout = layoutForState (connection.targetState);
            if (sourceLayout == nullptr || targetLayout == nullptr)
                continue;
            if (connection.sourceLane < 0 || connection.sourceLane >= machine->getLaneCount (connection.sourceState))
                continue;

            const auto sourceColour = graphColour (connection.sourceLane, connection.sourceState).interpolatedWith (accentB(), 0.18f);
            const auto targetColour = graphColour (connection.targetState).interpolatedWith (accentA(), 0.12f);
            const auto root = pointOnTrack (machine->state (connection.sourceState),
                                            *sourceLayout,
                                            connection.sourcePhase,
                                            static_cast<float> (connection.sourceLane) * sourceLayout->laneGap);
            drawConnectionPath (g, root, targetLayout->centre, sourceColour, targetColour, connection.action, false);
        }

        if (pendingConnection.active)
        {
            auto* sourceLayout = layoutForState (pendingConnection.sourceState);
            if (sourceLayout != nullptr
                && pendingConnection.sourceLane >= 0
                && pendingConnection.sourceLane < machine->getLaneCount (pendingConnection.sourceState))
            {
                const auto root = pointOnTrack (machine->state (pendingConnection.sourceState),
                                                *sourceLayout,
                                                pendingConnection.sourcePhase,
                                                static_cast<float> (pendingConnection.sourceLane) * sourceLayout->laneGap);
                drawConnectionPath (g, root, pendingConnection.mouse,
                                    graphColour (pendingConnection.sourceLane, pendingConnection.sourceState),
                                    accentB(), OrbitConnectionAction::start, true);
            }
        }
    }

    void drawConnectionPath (juce::Graphics& g,
                             juce::Point<float> from,
                             juce::Point<float> to,
                             juce::Colour fromColour,
                             juce::Colour toColour,
                             OrbitConnectionAction action,
                             bool pending) const
    {
        auto delta = to - from;
        const auto length = juce::jmax (1.0f, delta.getDistanceFromOrigin());
        const auto normal = juce::Point<float> (-delta.y / length, delta.x / length);
        const auto lift = juce::jlimit (-80.0f, 80.0f, (to.y - from.y) * 0.18f);
        const auto control = (from + to) * 0.5f + normal * (pending ? 18.0f : 34.0f) + juce::Point<float> (0.0f, lift);

        juce::Path path;
        path.startNewSubPath (from);
        path.quadraticTo (control, to);

        g.setColour (juce::Colour (0xff05070a).withAlpha (pending ? 0.32f : 0.22f));
        g.strokePath (path, juce::PathStrokeType (pending ? 4.0f : 3.2f,
                                                  juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));

        g.setGradientFill (juce::ColourGradient (fromColour.withAlpha (pending ? 0.70f : 0.54f), from,
                                                 toColour.withAlpha (pending ? 0.78f : 0.62f), to,
                                                 false));
        g.strokePath (path, juce::PathStrokeType (pending ? 1.55f : 1.15f,
                                                  juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));

        const auto head = from + (to - from) * 0.86f;
        g.setColour (toColour.withAlpha (pending ? 0.88f : 0.72f));
        g.fillEllipse (head.x - 2.4f, head.y - 2.4f, 4.8f, 4.8f);

        if (! pending)
        {
            const auto label = orbitConnectionActionLabel (action).substring (0, 1);
            auto badge = juce::Rectangle<float> (0.0f, 0.0f, 16.0f, 16.0f).withCentre (control);
            g.setColour (juce::Colour (0xff101318).withAlpha (0.82f));
            g.fillEllipse (badge);
            g.setColour (toColour.withAlpha (0.82f));
            g.drawEllipse (badge, 0.75f);
            g.setColour (ink().withAlpha (0.80f));
            g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
            g.drawFittedText (label, badge.toNearestInt(), juce::Justification::centred, 1);
        }
    }

    void drawTracks (juce::Graphics& g)
    {
        for (const auto& layout : trackLayouts)
        {
            const auto stateIndex = layout.stateIndex;
            const auto& state = machine->state (stateIndex);
            const auto selected = stateIndex == machine->selectedState;
            const auto colour = graphColour (stateIndex);

            g.setColour (selected ? rowFill().interpolatedWith (colour, 0.045f).withAlpha (0.28f)
                                  : rowFill().withAlpha (0.09f));
            g.fillEllipse (layout.centre.x - layout.outerRadius, layout.centre.y - layout.outerRadius,
                           layout.outerRadius * 2.0f, layout.outerRadius * 2.0f);

            for (int laneIndex = static_cast<int> (state.lanes.size()) - 1; laneIndex >= 0; --laneIndex)
            {
                const auto laneOffset = static_cast<float> (laneIndex) * layout.laneGap;
                auto laneColour = graphColour (laneIndex, stateIndex).interpolatedWith (ink(), 0.04f);
                const auto& lane = state.lanes[static_cast<size_t> (laneIndex)];
                if (! lane.enabled || lane.muted)
                    laneColour = mutedInk().withAlpha (0.24f);

                auto path = makeTrackPath (state, layout, laneOffset);
                drawLaneWaveform (g, state, lane, layout, laneIndex, laneColour);
                g.setColour (laneColour.withAlpha (laneIndex == machine->selectedLane && selected ? 0.98f : 0.58f));
                g.strokePath (path, juce::PathStrokeType (laneIndex == machine->selectedLane && selected ? 1.8f : 1.2f,
                                                          juce::PathStrokeType::curved,
                                                          juce::PathStrokeType::rounded));
                drawBeatMarkers (g, state, layout, laneIndex, laneColour);
                drawLaneMarker (g, state, lane, layout, laneIndex, laneColour, selected && laneIndex == machine->selectedLane);
            }

            for (int laneIndex = static_cast<int> (state.lanes.size()) - 1; laneIndex >= 0; --laneIndex)
            {
                auto laneColour = graphColour (laneIndex, stateIndex).interpolatedWith (ink(), 0.04f);
                const auto& lane = state.lanes[static_cast<size_t> (laneIndex)];
                if (! lane.enabled || lane.muted)
                    laneColour = mutedInk().withAlpha (0.24f);

                drawTrimHandles (g, state, layout, laneIndex, laneColour,
                                 state.index == selectedRenderedTrack && laneIndex == selectedRenderedLane);
            }

            auto outer = makeTrackPath (state, layout, 0.0f);
            g.setColour (colour.interpolatedWith (ink(), 0.08f).withAlpha (selected ? 0.90f : 0.48f));
            g.strokePath (outer, juce::PathStrokeType (selected ? 1.55f : 1.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            if (shapeEditMode || selected)
                drawWarpHandles (g, state, layout, colour, selected);
            drawPlayhead (g, state, layout, colour);
            drawTrackLabel (g, state, layout, stateIndex, selected);
        }
    }

    void drawTrackLabel (juce::Graphics& g, const State& state, const TrackLayout& layout, int index, bool selected) const
    {
        auto text = juce::Rectangle<float> (layout.centre.x - layout.outerRadius,
                                            layout.centre.y + layout.outerRadius + 4.0f,
                                            layout.outerRadius * 2.0f, 32.0f).toNearestInt();
        g.setColour ((selected ? ink() : mutedInk()).withAlpha (selected ? 0.94f : 0.68f));
        g.setFont (juce::FontOptions (12.0f, selected ? juce::Font::bold : juce::Font::plain));
        g.drawFittedText (state.name, text.removeFromTop (18), juce::Justification::centred, 1);
        g.setColour (mutedInk().withAlpha (0.52f));
        g.setFont (juce::FontOptions (9.8f));
        g.drawFittedText (juce::String (state.tempoBpm, 0) + " BPM  " + juce::String (state.beatsPerBar) + "/" + juce::String (state.beatUnit)
                            + "  " + juce::String (state.lanes.size()) + (state.lanes.size() == 1 ? " lane" : " lanes"),
                          text, juce::Justification::centred, 1);
        juce::ignoreUnused (index);
    }

    void drawWarpHandles (juce::Graphics& g, const State& state, const TrackLayout& layout, juce::Colour colour, bool selected) const
    {
        for (int i = 0; i < 8; ++i)
        {
            const auto angle = angleForWarpIndex (i);
            const auto phase = std::fmod ((angle + juce::MathConstants<float>::halfPi + juce::MathConstants<float>::twoPi),
                                          juce::MathConstants<float>::twoPi) / juce::MathConstants<float>::twoPi;
            auto p = pointOnTrack (state, layout, phase);
            const auto editing = shapeEditMode && selected;
            const auto size = editing ? 8.8f : (selected ? 4.8f : 3.2f);
            if (editing)
            {
                const auto base = layout.centre + juce::Point<float> (std::cos (angle), std::sin (angle)) * layout.radius;
                g.setColour (colour.withAlpha (0.18f));
                g.drawLine (base.x, base.y, p.x, p.y, 1.0f);
                g.setColour (juce::Colour (0xff07090c).withAlpha (0.78f));
                g.fillEllipse (p.x - size * 0.5f - 1.5f, p.y - size * 0.5f - 1.5f, size + 3.0f, size + 3.0f);
            }

            g.setColour (editing ? colour.brighter (0.35f).withAlpha (0.96f)
                                 : (selected ? colour.withAlpha (0.82f) : colour.withAlpha (0.44f)));
            g.fillEllipse (p.x - size * 0.5f, p.y - size * 0.5f, size, size);
            if (editing)
            {
                g.setColour (ink().withAlpha (0.72f));
                g.drawEllipse (p.x - size * 0.5f, p.y - size * 0.5f, size, size, 1.0f);
            }
        }
    }

    void drawPlayhead (juce::Graphics& g, const State& state, const TrackLayout& layout, juce::Colour colour) const
    {
        const auto duration = juce::jmax (0.1, state.secondsPerSection() / rate);
        const auto now = juce::Time::getMillisecondCounterHiRes();
        auto phase = machine->selectedState == state.index ? 0.0f : -1.0f;
        if (transportRunning)
        {
            if (const auto override = statePlayheads.find (state.index); override != statePlayheads.end())
            {
                phase = phaseFromOverride (override->second, duration, now);
            }
            else
            {
                const auto elapsed = (now - visualTransportStartMs) * 0.001;
                phase = static_cast<float> (std::fmod (juce::jmax (0.0, elapsed), duration) / duration);
            }
        }
        if (phase < 0.0f)
            return;

        const auto p = pointOnTrack (state, layout, phase, static_cast<float> (state.lanes.size()) * layout.laneGap + 3.0f);
        g.setColour (colour.brighter (0.22f).withAlpha (0.96f));
        g.drawLine (layout.centre.x, layout.centre.y, p.x, p.y, 0.8f);
        g.fillEllipse (p.x - 3.6f, p.y - 3.6f, 7.2f, 7.2f);
        g.setColour (juce::Colour (0xff101318).withAlpha (0.62f));
        g.drawEllipse (p.x - 3.6f, p.y - 3.6f, 7.2f, 7.2f, 0.8f);
    }

    float currentPlayheadPhaseForState (int stateIndex) const
    {
        if (machine == nullptr || stateIndex < 0 || stateIndex >= machine->getStateCount())
            return 0.0f;

        const auto duration = juce::jmax (0.1, machine->state (stateIndex).secondsPerSection() / rate);
        const auto now = juce::Time::getMillisecondCounterHiRes();

        if (const auto override = statePlayheads.find (stateIndex); override != statePlayheads.end())
            return phaseFromOverride (override->second, duration, now);

        const auto elapsed = (now - visualTransportStartMs) * 0.001;
        return static_cast<float> (std::fmod (juce::jmax (0.0, elapsed), duration) / duration);
    }

    float phaseFromOverride (const StatePlayhead& playhead, double duration, double now) const
    {
        if (playhead.mode == PlayheadMode::paused)
            return juce::jlimit (0.0f, 0.9999f, playhead.startPhase);

        const auto elapsed = juce::jmax (0.0, (now - playhead.startedMs) * 0.001);
        const auto progress = static_cast<float> (std::fmod (elapsed, duration) / duration);
        auto phase = playhead.mode == PlayheadMode::reverse ? playhead.startPhase - progress
                                                            : playhead.startPhase + progress;
        while (phase < 0.0f)
            phase += 1.0f;
        while (phase >= 1.0f)
            phase -= 1.0f;
        return phase;
    }

    void drawLaneMarker (juce::Graphics& g,
                         const State& state,
                         const Lane& lane,
                         const TrackLayout& layout,
                         int laneIndex,
                         juce::Colour colour,
                         bool selected) const
    {
        const auto p = pointOnTrack (state, layout, lane.orbitPhase, static_cast<float> (laneIndex) * layout.laneGap);
        const auto size = selected ? 7.4f : 5.6f;
        g.setColour (colour.withAlpha (lane.freezeInProgress ? 0.46f : 0.98f));
        g.fillEllipse (p.x - size * 0.5f, p.y - size * 0.5f, size, size);
        g.setColour (selected ? ink().withAlpha (0.68f) : juce::Colour (0xff101318).withAlpha (0.46f));
        g.drawEllipse (p.x - size * 0.5f, p.y - size * 0.5f, size, size, selected ? 0.9f : 0.55f);
    }

    void drawBeatMarkers (juce::Graphics& g,
                          const State& state,
                          const TrackLayout& layout,
                          int laneIndex,
                          juce::Colour colour) const
    {
        const auto laneOffset = static_cast<float> (laneIndex) * layout.laneGap;
        const auto beatSeconds = 60.0 / juce::jlimit (20.0, 320.0, state.tempoBpm);
        const auto totalBeats = state.durationUsesSeconds
            ? state.secondsPerSection() / beatSeconds
            : state.clockBeatsPerSection();
        const auto markerCount = juce::jlimit (1, 256, static_cast<int> (std::ceil (totalBeats)));
        const auto barBeats = juce::jmax (1.0, static_cast<double> (state.beatsPerBar) * (4.0 / static_cast<double> (juce::jlimit (1, 32, state.beatUnit))));

        for (int beat = 0; beat <= markerCount; ++beat)
        {
            const auto phase = juce::jlimit (0.0f, 1.0f, static_cast<float> (static_cast<double> (beat) / juce::jmax (1.0, totalBeats)));
            const auto angle = phase * juce::MathConstants<float>::twoPi - juce::MathConstants<float>::halfPi;
            const auto normal = juce::Point<float> (std::cos (angle), std::sin (angle));
            const auto base = pointOnTrack (state, layout, phase, laneOffset);
            const auto isBar = std::abs (std::fmod (static_cast<double> (beat), barBeats)) < 0.001 || beat == markerCount;
            const auto length = isBar ? 6.2f : 3.0f;
            const auto inner = base - normal * (length * 0.45f);
            const auto outer = base + normal * (length * 0.55f);
            g.setColour (colour.withAlpha (isBar ? 0.58f : 0.28f));
            g.drawLine (inner.x, inner.y, outer.x, outer.y, isBar ? 0.95f : 0.55f);
        }
    }

    void drawLaneWaveform (juce::Graphics& g,
                           const State& state,
                           const Lane& lane,
                           const TrackLayout& layout,
                           int laneIndex,
                           juce::Colour colour)
    {
        if ((! lane.frozen || lane.frozenAudioPath.isEmpty()) && ! lane.freezeInProgress)
            return;

        const auto laneOffset = static_cast<float> (laneIndex) * layout.laneGap;
        auto* waveform = lane.frozen && lane.frozenAudioPath.isNotEmpty() ? waveformFor (lane) : nullptr;
        if ((waveform == nullptr || waveform->peaks.empty()) && ! lane.freezeInProgress)
            return;

        const auto samples = waveform != nullptr && ! waveform->peaks.empty()
            ? static_cast<int> (waveform->peaks.size())
            : 96;
        const auto halfWidth = pipeHalfWidth (layout);
        const auto amplitude = pipeAmplitude (layout);
        const auto phaseOffset = lane.orbitPhase;
        const auto phaseSpan = lanePhaseSpan (state, lane);
        const auto trackSeconds = juce::jmax (0.25, state.secondsPerSection());
        const auto durationSeconds = juce::jmax (0.01, static_cast<double> (phaseSpan) * trackSeconds);
        const auto fadeInRatio = static_cast<float> (juce::jlimit (0.0, 0.5, lane.fadeInSeconds / durationSeconds));
        const auto fadeOutRatio = static_cast<float> (juce::jlimit (0.0, 0.5, lane.fadeOutSeconds / durationSeconds));

        juce::Path outer;
        juce::Path audibleOuter;
        std::vector<juce::Point<float>> innerPoints;
        std::vector<juce::Point<float>> audiblePoints;
        innerPoints.reserve (static_cast<size_t> (samples + 1));
        audiblePoints.reserve (static_cast<size_t> (samples + 1));
        for (int i = 0; i <= samples; ++i)
        {
            const auto phase = static_cast<float> (i) / static_cast<float> (samples);
            const auto wrappedPhase = std::fmod (phaseOffset + phase * phaseSpan, 1.0f);
            const auto peak = lane.freezeInProgress && (waveform == nullptr || waveform->peaks.empty())
                ? (0.18f + 0.10f * std::sin (phase * juce::MathConstants<float>::twoPi * 8.0f))
                : juce::jlimit (0.0f, 1.0f, waveform->peaks[static_cast<size_t> (i % samples)]);

            const auto angle = wrappedPhase * juce::MathConstants<float>::twoPi - juce::MathConstants<float>::halfPi;
            const auto normal = juce::Point<float> (std::cos (angle), std::sin (angle));
            const auto base = pointOnTrack (state, layout, wrappedPhase, laneOffset);
            const auto outerPoint = base + normal * (halfWidth + peak * amplitude);
            const auto innerPoint = base - normal * (halfWidth + peak * amplitude * 0.30f);
            auto fadeScale = 1.0f;
            if (fadeInRatio > 0.0001f && phase < fadeInRatio)
                fadeScale = juce::jmin (fadeScale, phase / fadeInRatio);
            if (fadeOutRatio > 0.0001f && phase > 1.0f - fadeOutRatio)
                fadeScale = juce::jmin (fadeScale, (1.0f - phase) / fadeOutRatio);
            fadeScale = juce::jlimit (0.0f, 1.0f, fadeScale);
            const auto audiblePoint = base + normal * (halfWidth + peak * amplitude * fadeScale);

            if (i == 0)
            {
                outer.startNewSubPath (outerPoint);
                audibleOuter.startNewSubPath (audiblePoint);
            }
            else
            {
                outer.lineTo (outerPoint);
                audibleOuter.lineTo (audiblePoint);
            }

            innerPoints.push_back (innerPoint);
            audiblePoints.push_back (audiblePoint);
        }

        juce::Path pipe = outer;
        for (auto point = innerPoints.rbegin(); point != innerPoints.rend(); ++point)
            pipe.lineTo (*point);
        pipe.closeSubPath();

        g.setColour (colour.withAlpha (lane.freezeInProgress ? 0.10f : 0.18f));
        g.fillPath (pipe);
        if (fadeInRatio > 0.0001f || fadeOutRatio > 0.0001f)
        {
            juce::Path dulled = outer;
            for (auto point = audiblePoints.rbegin(); point != audiblePoints.rend(); ++point)
                dulled.lineTo (*point);
            dulled.closeSubPath();

            g.setColour (juce::Colour (0xff05070a).withAlpha (0.16f));
            g.fillPath (dulled);
            g.setColour (colour.withAlpha (0.24f));
            g.strokePath (audibleOuter, juce::PathStrokeType (0.75f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        const auto renderedSelected = state.index == selectedRenderedTrack && laneIndex == selectedRenderedLane;
        g.setColour (colour.withAlpha (renderedSelected ? 0.34f : 0.090f));
        g.strokePath (laneSegmentCentrePath (state, layout, phaseOffset, phaseSpan, laneOffset),
                      juce::PathStrokeType (pipeHalfWidth (layout) * 1.55f + pipeAmplitude (layout) * 0.55f,
                                            juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));
        if (renderedSelected)
        {
            g.setColour (colour.brighter (0.24f).withAlpha (0.90f));
            g.strokePath (laneSegmentCentrePath (state, layout, phaseOffset, phaseSpan, laneOffset),
                          juce::PathStrokeType (0.9f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour (colour.brighter (0.18f).withAlpha (lane.freezeInProgress ? 0.34f : 0.82f));
        g.strokePath (outer, juce::PathStrokeType (0.95f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (juce::Colour (0xff101318).withAlpha (0.16f));
        juce::Path inner;
        for (int i = 0; i < static_cast<int> (innerPoints.size()); ++i)
        {
            if (i == 0)
                inner.startNewSubPath (innerPoints[static_cast<size_t> (i)]);
            else
                inner.lineTo (innerPoints[static_cast<size_t> (i)]);
        }
        g.strokePath (inner, juce::PathStrokeType (0.58f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void drawTrimHandles (juce::Graphics& g,
                          const State& state,
                          const TrackLayout& layout,
                          int laneIndex,
                          juce::Colour,
                          bool selected) const
    {
        const auto& lane = state.lanes[static_cast<size_t> (laneIndex)];
        if (! isPlacedLane (lane))
            return;

        const auto start = waveformTrimHandlePoint (state, lane, layout, laneIndex, true);
        const auto end = waveformTrimHandlePoint (state, lane, layout, laneIndex, false);
        const auto fadeIn = waveformFadeHandlePoint (state, lane, layout, laneIndex, true);
        const auto fadeOut = waveformFadeHandlePoint (state, lane, layout, laneIndex, false);
        const auto zoomScale = focusedTrackIndex >= 0 ? juce::jlimit (0.46f, 1.0f, std::sqrt (focusedViewZoom)) : 1.0f;
        const auto trimLength = (selected ? 11.0f : 8.0f) * zoomScale;
        const auto fadeLength = (selected ? 8.0f : 6.0f) * zoomScale;

        auto strokeSoftLine = [&] (juce::Point<float> a,
                                   juce::Point<float> b,
                                   juce::Colour colour,
                                   float width,
                                   float backingAlpha = 0.0f)
        {
            juce::Path line;
            line.startNewSubPath (a);
            line.lineTo (b);

            if (backingAlpha > 0.0f)
            {
                g.setColour (juce::Colour (0xff05070a).withAlpha (backingAlpha));
                g.strokePath (line, juce::PathStrokeType (width + 1.15f * zoomScale,
                                                          juce::PathStrokeType::curved,
                                                          juce::PathStrokeType::rounded));
            }

            g.setColour (colour);
            g.strokePath (line, juce::PathStrokeType (width,
                                                      juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));
        };

        auto drawTick = [&] (juce::Point<float> p, bool isStart, bool fade)
        {
            const auto angle = phaseForPoint (p, layout.centre) * juce::MathConstants<float>::twoPi - juce::MathConstants<float>::halfPi;
            const auto normal = juce::Point<float> (std::cos (angle), std::sin (angle));
            const auto length = fade ? fadeLength : trimLength;
            const auto centre = p + normal * (fade ? 0.75f * zoomScale : 0.0f);
            const auto a = centre - normal * (length * 0.5f);
            const auto b = centre + normal * (length * 0.5f);
            const auto c = fade
                ? (isStart ? juce::Colour (0xffb9fbff) : juce::Colour (0xffffe58a)).withAlpha (selected ? 0.82f : 0.58f)
                : (isStart ? juce::Colour (0xff1ff6ff) : juce::Colour (0xffffcd26)).withAlpha (selected ? 1.0f : 0.78f);

            strokeSoftLine (a, b, c, (fade ? 1.0f : 1.18f) * zoomScale, selected ? 0.30f : 0.18f);
        };

        auto drawFadeRamp = [&] (juce::Point<float> boundary, juce::Point<float> p, bool isIn)
        {
            const auto low = isIn ? boundary : p;
            const auto high = isIn ? p : boundary;

            const auto c = (isIn ? juce::Colour (0xffb9fbff) : juce::Colour (0xffffe58a)).withAlpha (selected ? 0.72f : 0.46f);
            strokeSoftLine (low, high, c, 0.74f * zoomScale, selected ? 0.18f : 0.10f);
        };

        drawFadeRamp (start, fadeIn, true);
        drawFadeRamp (end, fadeOut, false);
        drawTick (start, true, false);
        drawTick (end, false, false);
        drawTick (fadeIn, true, true);
        drawTick (fadeOut, false, true);
    }

    float lanePhaseSpan (const State& state, const Lane& lane) const
    {
        const auto trackSeconds = juce::jmax (0.25, state.secondsPerSection());
        const auto remaining = juce::jmax (0.0001, 1.0 - juce::jlimit (0.0, 0.9999, static_cast<double> (lane.orbitPhase)));
        const auto beatSeconds = 60.0 / juce::jlimit (20.0, 320.0, state.tempoBpm);
        const auto totalBeats = state.durationUsesSeconds ? trackSeconds / beatSeconds : state.clockBeatsPerSection();
        const auto currentBeat = static_cast<double> (lane.orbitPhase) * totalBeats;
        const auto barBeats = juce::jmax (1.0, static_cast<double> (state.beatsPerBar) * (4.0 / static_cast<double> (juce::jlimit (1, 32, state.beatUnit))));

        auto seconds = trackSeconds * remaining;
        switch (lane.durationMode)
        {
            case LaneDurationMode::endOfBeat:
                seconds = (std::ceil (currentBeat + 0.0001) - currentBeat) * beatSeconds;
                break;

            case LaneDurationMode::endOfBar:
                seconds = (std::ceil ((currentBeat + 0.0001) / barBeats) * barBeats - currentBeat) * beatSeconds;
                break;

            case LaneDurationMode::fixedBars:
                seconds = juce::jmax (0.01, lane.durationValue) * state.secondsPerBar();
                break;

            case LaneDurationMode::fixedSeconds:
                seconds = juce::jmax (0.01, lane.durationValue);
                break;

            case LaneDurationMode::natural:
                seconds = trackSeconds * remaining;
                break;
        }

        return static_cast<float> (juce::jlimit (0.002, remaining, seconds / trackSeconds));
    }

    float pipeHalfWidth (const TrackLayout& layout) const
    {
        if (focusedTrackIndex >= 0)
        {
            const auto zoom = juce::jlimit (0.55f, 5.0f, focusedViewZoom);
            const auto inward = juce::jlimit (0.0f, 1.0f, (zoom - 1.0f) / 4.0f);
            const auto outward = juce::jlimit (0.0f, 1.0f, (1.0f - zoom) / 0.45f);
            const auto height = layout.laneGap * (0.085f + 0.060f * inward);
            return juce::jlimit (2.8f, 36.0f, height * (1.0f - 0.18f * outward));
        }

        return juce::jlimit (2.8f, 5.5f, layout.laneGap * 0.085f);
    }

    float pipeCoreWidth (const TrackLayout& layout) const
    {
        return juce::jlimit (1.2f, 2.6f, layout.laneGap * 0.05f);
    }

    float pipeAmplitude (const TrackLayout& layout) const
    {
        if (focusedTrackIndex >= 0)
        {
            const auto zoom = juce::jlimit (0.55f, 5.0f, focusedViewZoom);
            const auto inward = juce::jlimit (0.0f, 1.0f, (zoom - 1.0f) / 4.0f);
            const auto outward = juce::jlimit (0.0f, 1.0f, (1.0f - zoom) / 0.45f);
            const auto height = layout.laneGap * (0.18f + 0.09f * inward) * (1.0f + 0.30f * inward);
            return juce::jlimit (5.0f, 210.0f, height * (1.0f - 0.22f * outward));
        }

        return juce::jlimit (5.0f, 11.0f, layout.laneGap * 0.18f);
    }

    struct WaveformData
    {
        std::vector<float> peaks;
    };

    WaveformData* waveformFor (const Lane& lane)
    {
        constexpr int peakCount = 128;
        const auto path = lane.frozenAudioPath.toStdString();
        auto found = waveforms.find (path);
        if (found != waveforms.end())
            return found->second.get();

        auto data = std::make_unique<WaveformData>();
        auto file = juce::File (lane.frozenAudioPath);
        if (auto reader = std::unique_ptr<juce::AudioFormatReader> (formatManager.createReaderFor (file)))
        {
            data->peaks.resize (peakCount, 0.0f);
            juce::AudioBuffer<float> buffer (static_cast<int> (juce::jmax (1u, reader->numChannels)), 2048);
            const auto totalSamples = juce::jmax<juce::int64> (1, reader->lengthInSamples);

            for (int bucket = 0; bucket < peakCount; ++bucket)
            {
                const auto start = totalSamples * bucket / peakCount;
                const auto end = totalSamples * (bucket + 1) / peakCount;
                auto remaining = end - start;
                auto position = start;
                auto peak = 0.0f;

                while (remaining > 0)
                {
                    const auto block = static_cast<int> (juce::jmin<juce::int64> (buffer.getNumSamples(), remaining));
                    buffer.clear();
                    if (! reader->read (&buffer, 0, block, position, true, true))
                        break;

                    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                        peak = juce::jmax (peak, buffer.getMagnitude (channel, 0, block));

                    position += block;
                    remaining -= block;
                }

                data->peaks[static_cast<size_t> (bucket)] = peak;
            }

            const auto maxPeak = *std::max_element (data->peaks.begin(), data->peaks.end());
            if (maxPeak > 0.0001f)
            {
                for (auto& peak : data->peaks)
                    peak = std::sqrt (juce::jlimit (0.0f, 1.0f, peak / maxPeak));
            }
        }

        found = waveforms.emplace (path, std::move (data)).first;
        return found->second.get();
    }

    Hit orbitHitTest (juce::Point<float> point) const
    {
        Hit best;
        auto bestDistance = 100000.0f;
        for (const auto& layout : trackLayouts)
        {
            const auto stateIndex = layout.stateIndex;
            const auto& state = machine->state (stateIndex);
            auto delta = point - layout.centre;
            auto angle = std::atan2 (delta.y, delta.x);
            auto phase = (angle + juce::MathConstants<float>::halfPi) / juce::MathConstants<float>::twoPi;
            while (phase < 0.0f) phase += 1.0f;
            while (phase >= 1.0f) phase -= 1.0f;

            for (int laneIndex = 0; laneIndex < juce::jmax (1, static_cast<int> (state.lanes.size())); ++laneIndex)
            {
                const auto radius = radiusForAngle (state, layout.radius + static_cast<float> (laneIndex) * layout.laneGap, angle);
                const auto& lane = state.lanes[static_cast<size_t> (laneIndex)];
                const auto tolerance = isPlacedLane (lane) ? juce::jmin (layout.laneGap * 0.42f, pipeHalfWidth (layout) + pipeAmplitude (layout) * 0.86f) : 14.0f;
                const auto distance = std::abs (delta.getDistanceFromOrigin() - radius);
                if (distance < bestDistance && distance < tolerance)
                {
                    bestDistance = distance;
                    best = { stateIndex, laneIndex < static_cast<int> (state.lanes.size()) ? laneIndex : -1, phase };
                }
            }
        }

        return best;
    }

    int stateAtPoint (juce::Point<float> point) const
    {
        auto best = -1;
        auto bestDistance = std::numeric_limits<float>::max();
        for (const auto& layout : trackLayouts)
        {
            const auto distance = point.getDistanceFrom (layout.centre);
            if (distance <= layout.outerRadius && distance < bestDistance)
            {
                bestDistance = distance;
                best = layout.stateIndex;
            }
        }

        return best;
    }

    Hit waveformHitTest (juce::Point<float> point) const
    {
        Hit best;
        auto bestDistance = 100000.0f;

        for (const auto& layout : trackLayouts)
        {
            const auto stateIndex = layout.stateIndex;
            const auto& state = machine->state (stateIndex);

            for (int laneIndex = 0; laneIndex < static_cast<int> (state.lanes.size()); ++laneIndex)
            {
                const auto& lane = state.lanes[static_cast<size_t> (laneIndex)];
                if (! isPlacedLane (lane))
                    continue;

                const auto laneOffset = static_cast<float> (laneIndex) * layout.laneGap;
                const auto span = lanePhaseSpan (state, lane);
                const auto distance = distanceToLaneSegment (state, layout, lane.orbitPhase, span, laneOffset, point);
                const auto tolerance = juce::jmin (focusedTrackIndex >= 0 ? 34.0f : 24.0f,
                                                   layout.laneGap * 0.48f);

                if (distance >= bestDistance || distance > tolerance)
                    continue;

                bestDistance = distance;
                best = { stateIndex, laneIndex, phaseForPoint (point, layout.centre) };
            }
        }

        return best;
    }

    TrimHit trimHandleHitTest (juce::Point<float> point) const
    {
        TrimHit best;
        auto bestDistance = 100000.0f;

        for (const auto& layout : trackLayouts)
        {
            const auto stateIndex = layout.stateIndex;
            const auto& state = machine->state (stateIndex);

            for (int laneIndex = 0; laneIndex < static_cast<int> (state.lanes.size()); ++laneIndex)
            {
                const auto& lane = state.lanes[static_cast<size_t> (laneIndex)];
                if (! isPlacedLane (lane))
                    continue;

                const auto start = waveformTrimHandlePoint (state, lane, layout, laneIndex, true);
                const auto end = waveformTrimHandlePoint (state, lane, layout, laneIndex, false);
                const auto fadeIn = waveformFadeHandlePoint (state, lane, layout, laneIndex, true);
                const auto fadeOut = waveformFadeHandlePoint (state, lane, layout, laneIndex, false);
                const auto zoomScale = focusedTrackIndex >= 0 ? juce::jlimit (0.56f, 1.0f, std::sqrt (focusedViewZoom)) : 1.0f;
                const auto tolerance = (focusedTrackIndex >= 0 ? 28.0f : 22.0f) * zoomScale;
                const auto fadeTolerance = (focusedTrackIndex >= 0 ? 24.0f : 18.0f) * zoomScale;

                const auto fadeInDistance = point.getDistanceFrom (fadeIn);
                if (fadeInDistance < bestDistance && fadeInDistance <= fadeTolerance)
                {
                    bestDistance = fadeInDistance;
                    best = { stateIndex, laneIndex, TrimHandle::fadeIn };
                }

                const auto fadeOutDistance = point.getDistanceFrom (fadeOut);
                if (fadeOutDistance < bestDistance && fadeOutDistance <= fadeTolerance)
                {
                    bestDistance = fadeOutDistance;
                    best = { stateIndex, laneIndex, TrimHandle::fadeOut };
                }

                const auto startDistance = point.getDistanceFrom (start);
                if (startDistance < bestDistance && startDistance <= tolerance)
                {
                    bestDistance = startDistance;
                    best = { stateIndex, laneIndex, TrimHandle::start };
                }

                const auto endDistance = point.getDistanceFrom (end);
                if (endDistance < bestDistance && endDistance <= tolerance)
                {
                    bestDistance = endDistance;
                    best = { stateIndex, laneIndex, TrimHandle::end };
                }
            }
        }

        return best;
    }

    juce::Path laneSegmentHitPath (const State& state,
                                   const TrackLayout& layout,
                                   float startPhase,
                                   float phaseSpan,
                                   float laneOffset) const
    {
        auto centre = laneSegmentCentrePath (state, layout, startPhase, phaseSpan, laneOffset);
        juce::Path hitPath;
        juce::PathStrokeType (juce::jmin (layout.laneGap * 0.78f,
                                          pipeHalfWidth (layout) * 2.0f + pipeAmplitude (layout) * 1.6f + 8.0f),
                              juce::PathStrokeType::curved,
                              juce::PathStrokeType::rounded).createStrokedPath (hitPath, centre);
        return hitPath;
    }

    juce::Point<float> waveformTrimHandlePoint (const State& state,
                                                const Lane& lane,
                                                const TrackLayout& layout,
                                                int laneIndex,
                                                bool start) const
    {
        const auto laneOffset = static_cast<float> (laneIndex) * layout.laneGap;
        const auto phase = start ? lane.orbitPhase
                                 : juce::jlimit (lane.orbitPhase + 0.002f, 0.9999f, lane.orbitPhase + lanePhaseSpan (state, lane));
        auto* waveform = lane.frozen && lane.frozenAudioPath.isNotEmpty() ? const_cast<OrbitTrackCanvas*> (this)->waveformFor (lane) : nullptr;
        auto peak = 0.72f;
        if (waveform != nullptr && ! waveform->peaks.empty())
        {
            const auto index = start ? 0 : static_cast<int> (waveform->peaks.size()) - 1;
            peak = juce::jlimit (0.35f, 1.0f, waveform->peaks[static_cast<size_t> (juce::jlimit (0, static_cast<int> (waveform->peaks.size()) - 1, index))]);
        }

        const auto angle = phase * juce::MathConstants<float>::twoPi - juce::MathConstants<float>::halfPi;
        const auto normal = juce::Point<float> (std::cos (angle), std::sin (angle));
        const auto base = pointOnTrack (state, layout, phase, laneOffset);
        const auto outerPoint = base + normal * (pipeHalfWidth (layout) + peak * pipeAmplitude (layout));
        const auto innerPoint = base - normal * (pipeHalfWidth (layout) + peak * pipeAmplitude (layout) * 0.30f);
        return (outerPoint + innerPoint) * 0.5f;
    }

    juce::Point<float> waveformFadeHandlePoint (const State& state,
                                                const Lane& lane,
                                                const TrackLayout& layout,
                                                int laneIndex,
                                                bool fadeIn) const
    {
        const auto laneOffset = static_cast<float> (laneIndex) * layout.laneGap;
        const auto trackSeconds = juce::jmax (0.25, state.secondsPerSection());
        const auto span = lanePhaseSpan (state, lane);
        const auto startPhase = lane.orbitPhase;
        const auto endPhase = juce::jlimit (startPhase + 0.002f, 0.9999f, startPhase + span);
        const auto durationSeconds = juce::jmax (0.01, static_cast<double> (span) * trackSeconds);
        const auto maxFade = durationSeconds * 0.5;
        const auto fadeSeconds = juce::jlimit (0.0, maxFade, fadeIn ? lane.fadeInSeconds : lane.fadeOutSeconds);
        const auto phase = fadeIn
            ? juce::jlimit (startPhase, endPhase, startPhase + static_cast<float> (fadeSeconds / trackSeconds))
            : juce::jlimit (startPhase, endPhase, endPhase - static_cast<float> (fadeSeconds / trackSeconds));

        const auto angle = phase * juce::MathConstants<float>::twoPi - juce::MathConstants<float>::halfPi;
        const auto normal = juce::Point<float> (std::cos (angle), std::sin (angle));
        return pointOnTrack (state, layout, phase, laneOffset)
             + normal * (pipeHalfWidth (layout) + pipeAmplitude (layout) + 7.0f);
    }

    juce::Point<float> trimHandlePoint (const State& state,
                                        const TrackLayout& layout,
                                        float phase,
                                        float laneOffset) const
    {
        const auto angle = phase * juce::MathConstants<float>::twoPi - juce::MathConstants<float>::halfPi;
        const auto normal = juce::Point<float> (std::cos (angle), std::sin (angle));
        return pointOnTrack (state, layout, phase, laneOffset)
             + normal * (pipeHalfWidth (layout) + pipeAmplitude (layout) + 8.0f);
    }

    juce::Path laneSegmentCentrePath (const State& state,
                                      const TrackLayout& layout,
                                      float startPhase,
                                      float phaseSpan,
                                      float laneOffset) const
    {
        juce::Path path;
        constexpr int samples = 160;

        for (int i = 0; i <= samples; ++i)
        {
            const auto phase = startPhase + phaseSpan * static_cast<float> (i) / static_cast<float> (samples);
            const auto point = pointOnTrack (state, layout, phase, laneOffset);
            if (i == 0)
                path.startNewSubPath (point);
            else
                path.lineTo (point);
        }

        return path;
    }

    static float phaseForPoint (juce::Point<float> point, juce::Point<float> centre)
    {
        auto delta = point - centre;
        auto phase = (std::atan2 (delta.y, delta.x) + juce::MathConstants<float>::halfPi) / juce::MathConstants<float>::twoPi;
        while (phase < 0.0f) phase += 1.0f;
        while (phase >= 1.0f) phase -= 1.0f;
        return juce::jlimit (0.0f, 0.9999f, phase);
    }

    float distanceToLaneSegment (const State& state,
                                 const TrackLayout& layout,
                                 float startPhase,
                                 float phaseSpan,
                                 float laneOffset,
                                 juce::Point<float> point) const
    {
        constexpr int samples = 160;
        auto best = 100000.0f;
        auto previous = pointOnTrack (state, layout, startPhase, laneOffset);

        for (int i = 1; i <= samples; ++i)
        {
            const auto phase = startPhase + phaseSpan * static_cast<float> (i) / static_cast<float> (samples);
            const auto current = pointOnTrack (state, layout, phase, laneOffset);
            best = juce::jmin (best, distanceToSegment (point, previous, current));
            previous = current;
        }

        return best;
    }

    static float distanceToSegment (juce::Point<float> point, juce::Point<float> a, juce::Point<float> b)
    {
        const auto ab = b - a;
        const auto lengthSquared = ab.getDotProduct (ab);
        if (lengthSquared <= 0.0001f)
            return point.getDistanceFrom (a);

        const auto t = juce::jlimit (0.0f, 1.0f, (point - a).getDotProduct (ab) / lengthSquared);
        return point.getDistanceFrom (a + ab * t);
    }

    int warpHandleAt (int layoutIndex, juce::Point<float> point) const
    {
        if (machine == nullptr || layoutIndex < 0 || layoutIndex >= static_cast<int> (trackLayouts.size()))
            return -1;

        const auto& layout = trackLayouts[static_cast<size_t> (layoutIndex)];
        const auto& state = machine->state (layout.stateIndex);
        for (int i = 0; i < 8; ++i)
        {
            const auto angle = angleForWarpIndex (i);
            const auto phase = std::fmod ((angle + juce::MathConstants<float>::halfPi + juce::MathConstants<float>::twoPi),
                                          juce::MathConstants<float>::twoPi) / juce::MathConstants<float>::twoPi;
            if (point.getDistanceFrom (pointOnTrack (state, layout, phase)) <= (shapeEditMode ? 15.0f : 9.0f))
                return i;
        }

        return -1;
    }

    TrackLayout* layoutForState (int stateIndex)
    {
        for (auto& layout : trackLayouts)
            if (layout.stateIndex == stateIndex)
                return &layout;

        return nullptr;
    }

    static bool isPlacedLane (const Lane& lane)
    {
        return lane.sourceScriptPath.isNotEmpty() || lane.frozenAudioPath.isNotEmpty();
    }

    void clearRenderedSelection()
    {
        selectedRenderedTrack = -1;
        selectedRenderedLane = -1;
    }

    void updateDraggedLanePhase (juce::Point<float> point, bool finished)
    {
        auto* layout = layoutForState (draggingLaneTrack);
        if (layout == nullptr || draggingLaneTrack < 0 || draggingLaneTrack >= machine->getStateCount())
            return;

        auto& state = machine->state (draggingLaneTrack);
        if (draggingLaneIndex < 0 || draggingLaneIndex >= static_cast<int> (state.lanes.size()))
            return;

        auto delta = point - layout->centre;
        auto phase = (std::atan2 (delta.y, delta.x) + juce::MathConstants<float>::halfPi) / juce::MathConstants<float>::twoPi;
        while (phase < 0.0f) phase += 1.0f;
        while (phase >= 1.0f) phase -= 1.0f;
        phase = juce::jlimit (0.0f, 0.9999f, phase);

        auto& lane = state.lanes[static_cast<size_t> (draggingLaneIndex)];
        lane.orbitPhase = phase;
        selectedRenderedTrack = draggingLaneTrack;
        selectedRenderedLane = draggingLaneIndex;

        if (onLanePhaseChanged)
            onLanePhaseChanged (draggingLaneTrack, draggingLaneIndex, phase, finished);

        repaint();
    }

    void updateDraggedTrim (juce::Point<float> point, bool finished)
    {
        auto* layout = layoutForState (draggingLaneTrack);
        if (layout == nullptr || draggingLaneTrack < 0 || draggingLaneTrack >= machine->getStateCount())
            return;

        auto& state = machine->state (draggingLaneTrack);
        if (draggingLaneIndex < 0 || draggingLaneIndex >= static_cast<int> (state.lanes.size()))
            return;

        auto phase = phaseForPoint (point, layout->centre);
        constexpr float minSpan = 0.0025f;
        if (draggingTrim == TrimHandle::start)
            trimStartPhase = juce::jlimit (0.0f, trimEndPhase - minSpan, phase);
        else if (draggingTrim == TrimHandle::end)
            trimEndPhase = juce::jlimit (trimStartPhase + minSpan, 0.9999f, phase);

        auto& lane = state.lanes[static_cast<size_t> (draggingLaneIndex)];
        selectedRenderedTrack = draggingLaneTrack;
        selectedRenderedLane = draggingLaneIndex;

        if (draggingTrim == TrimHandle::fadeIn || draggingTrim == TrimHandle::fadeOut)
        {
            const auto trackSeconds = juce::jmax (0.25, state.secondsPerSection());
            const auto safePhase = juce::jlimit (trimStartPhase, trimEndPhase, phase);
            const auto durationSeconds = juce::jmax (0.01, static_cast<double> (trimEndPhase - trimStartPhase) * trackSeconds);
            const auto maxFade = durationSeconds * 0.5;
            if (draggingTrim == TrimHandle::fadeIn)
                lane.fadeInSeconds = juce::jlimit (0.0, maxFade, static_cast<double> (safePhase - trimStartPhase) * trackSeconds);
            else
                lane.fadeOutSeconds = juce::jlimit (0.0, maxFade, static_cast<double> (trimEndPhase - safePhase) * trackSeconds);

            if (onLaneFadeChanged)
                onLaneFadeChanged (draggingLaneTrack, draggingLaneIndex, lane.fadeInSeconds, lane.fadeOutSeconds, finished);
        }
        else
        {
            lane.orbitPhase = trimStartPhase;
            if (onLaneTrimChanged)
                onLaneTrimChanged (draggingLaneTrack, draggingLaneIndex, trimStartPhase, trimEndPhase, finished);
        }

        repaint();
    }

    MachineModel* machine = nullptr;
    double rate = 1.0;
    bool transportRunning = false;
    int focusedTrackIndex = -1;
    std::vector<TrackLayout> trackLayouts;
    int draggingTrack = -1;
    int draggingWarpIndex = -1;
    int draggingLaneTrack = -1;
    int draggingLaneIndex = -1;
    bool draggingLaneHasMoved = false;
    TrimHandle draggingTrim = TrimHandle::none;
    bool panningOverviewView = false;
    bool panningFocusedView = false;
    bool shapeEditMode = false;
    float overviewViewZoom = 1.0f;
    juce::Point<float> overviewViewPan;
    juce::Point<float> overviewPanStart;
    juce::Point<float> overviewPanDragStart;
    float focusedViewZoom = 1.0f;
    juce::Point<float> focusedViewPan;
    juce::Point<float> focusedPanStart;
    juce::Point<float> focusedPanDragStart;
    float trimStartPhase = 0.0f;
    float trimEndPhase = 0.0f;
    int selectedRenderedTrack = -1;
    int selectedRenderedLane = -1;
    double visualTransportStartMs = 0.0;
    juce::AudioFormatManager formatManager;
    std::unordered_map<std::string, std::unique_ptr<WaveformData>> waveforms;
    std::unordered_map<int, StatePlayhead> statePlayheads;
    PendingConnection pendingConnection;
};

class FsmNavigatorComponent final : public juce::Component
{
public:
    std::function<void (MachineModel*, int)> onStateChosen;

    void setMachines (MachineModel& rootMachine, MachineModel* active, MachineModel* inspected)
    {
        root = &rootMachine;
        activeMachine = active;
        inspectedMachine = inspected;
        rebuildRows();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (panelFill().withAlpha (0.88f));
        g.fillRoundedRectangle (bounds, 4.0f);

        g.setColour (ink());
        g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        g.drawFittedText ("Navigator", getLocalBounds().reduced (10, 6).removeFromTop (18),
                          juce::Justification::centredLeft, 1);

        for (const auto& row : rows)
        {
            const auto selected = row.machine == inspectedMachine && row.stateIndex == row.machine->selectedState;
            const auto active = row.machine == activeMachine;
            auto r = row.bounds.toFloat();

            if (selected)
            {
                const auto rowColour = graphColour (row.stateIndex, row.depth * 2);
                g.setColour (inspectedFill().interpolatedWith (rowColour, 0.12f).withAlpha (0.96f));
                g.fillRoundedRectangle (r.reduced (2.0f, 1.0f), 3.0f);
                g.setColour (rowColour.withAlpha (0.62f));
                g.drawRoundedRectangle (r.reduced (2.0f, 1.0f), 3.0f, 0.75f);
            }

            const auto dotX = static_cast<float> (row.bounds.getX() + 10 + row.depth * 14);
            const auto dotY = static_cast<float> (row.bounds.getCentreY());
            g.setColour (active ? graphColour (row.stateIndex).brighter (0.18f) : graphColour (row.stateIndex, row.depth * 2));
            g.fillEllipse (dotX - 3.5f, dotY - 3.5f, 7.0f, 7.0f);

            g.setColour (selected ? ink() : mutedInk());
            g.setFont (juce::FontOptions (11.5f, selected ? juce::Font::bold : juce::Font::plain));
            auto textArea = row.bounds.withTrimmedLeft (22 + row.depth * 14).withTrimmedRight (48);
            g.drawFittedText (row.name, textArea, juce::Justification::centredLeft, 1);

            g.setColour (mutedInk().withAlpha (0.72f));
            g.setFont (juce::FontOptions (10.0f));
            g.drawFittedText (row.detail, row.bounds.withTrimmedLeft (row.bounds.getWidth() - 50).reduced (4, 0),
                              juce::Justification::centredRight, 1);
        }
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        for (const auto& row : rows)
        {
            if (row.bounds.contains (event.getPosition()))
            {
                if (onStateChosen)
                    onStateChosen (row.machine, row.stateIndex);
                return;
            }
        }
    }

    void resized() override
    {
        rebuildRows();
    }

private:
    struct Row
    {
        MachineModel* machine = nullptr;
        int stateIndex = 0;
        int depth = 0;
        juce::String name;
        juce::String detail;
        juce::Rectangle<int> bounds;
    };

    void rebuildRows()
    {
        rows.clear();
        if (root == nullptr)
            return;

        auto area = getLocalBounds().reduced (8, 30);
        addRowsForMachine (*root, 0, area);
    }

    void addRowsForMachine (MachineModel& model, int depth, juce::Rectangle<int>& area)
    {
        for (int i = 0; i < model.getStateCount(); ++i)
        {
            if (area.getHeight() < rowHeight)
                return;

            const auto& state = model.state (i);
            const auto laneCount = static_cast<int> (state.lanes.size());
            auto detail = juce::String (laneCount) + (laneCount == 1 ? " trk" : " trks");
            if (model.hasChildMachine (i))
                detail = juce::String (model.childMachine (i)->getStateCount()) + " FSM";

            rows.push_back ({ &model, i, depth, state.name, detail, area.removeFromTop (rowHeight) });

            if (auto* child = model.childMachine (i))
                addRowsForMachine (*child, depth + 1, area);
        }
    }

    static constexpr int rowHeight = 24;
    MachineModel* root = nullptr;
    MachineModel* activeMachine = nullptr;
    MachineModel* inspectedMachine = nullptr;
    std::vector<Row> rows;
};

class ClickableLabel final : public juce::Label
{
public:
    std::function<void()> onClick;

    void mouseUp (const juce::MouseEvent&) override
    {
        if (onClick)
            onClick();
    }
};

class TrackListComponent final : public juce::Component
{
public:
    std::function<void (int)> onTrackSelected;
    std::function<void (int)> onEnabledToggled;
    std::function<void (int)> onMuteToggled;
    std::function<void (int)> onSoloToggled;
    std::function<void (int)> onFreezeToggled;
    std::function<void (int, float)> onVolumeChanged;
    std::function<void()> onDeleteSelectedLaneRequested;

    TrackListComponent()
    {
        setWantsKeyboardFocus (true);
    }

    void setState (State& stateToShow, int selectedLane)
    {
        state = &stateToShow;
        selectedIndex = selectedLane;
        clampScroll();
        scrollSelectedIntoView();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds();
        g.setColour (panelFill().withAlpha (0.88f));
        g.fillRoundedRectangle (bounds.toFloat(), 4.0f);

        if (state == nullptr)
            return;

        {
            juce::Graphics::ScopedSaveState saveState (g);
            g.reduceClipRegion (getTrackViewportBounds());

            const auto viewport = getTrackViewportBounds();
            auto list = getTrackListBounds();
            for (int i = 0; i < static_cast<int> (state->lanes.size()); ++i)
            {
                auto row = list.removeFromTop (rowHeight).reduced (0, rowVerticalInset);
                if (row.getBottom() < viewport.getY() || row.getY() > viewport.getBottom())
                    continue;

                const auto& lane = state->lanes[static_cast<size_t> (i)];
                const auto selected = i == selectedIndex;

                const auto laneColour = getTrackColour (i);
                g.setColour (selected ? rowFill().interpolatedWith (laneColour, 0.28f).withAlpha (1.0f)
                                      : rowFill().interpolatedWith (laneColour, 0.09f).withAlpha (lane.enabled ? 0.94f : 0.44f));
                g.fillRoundedRectangle (row.toFloat(), 3.0f);
                if (selected)
                {
                    g.setColour (laneColour.withAlpha (0.70f));
                    g.fillRoundedRectangle (row.withWidth (3).toFloat(), 2.0f);
                }

                g.setColour (selected ? laneColour.withAlpha (1.0f) : laneColour.withAlpha (0.42f));
                g.drawRoundedRectangle (row.toFloat(), 3.0f, selected ? 1.05f : 0.65f);

                auto rowText = row.reduced (10, 0);
                auto dotArea = rowText.removeFromLeft (12).withSizeKeepingCentre (8, 8).toFloat();
                g.setColour (laneColour.withAlpha (lane.enabled ? (lane.playing ? 0.88f : 0.62f) : 0.24f));
                g.fillEllipse (dotArea);
                g.setColour (lane.playing ? ink().withAlpha (0.85f) : juce::Colour (0xff101318).withAlpha (0.8f));
                g.drawEllipse (dotArea.expanded (1.0f), lane.playing ? 1.4f : 0.8f);

                auto buttons = rowText.removeFromRight (92);
                drawToggle (g, buttons.removeFromLeft (23), "E", lane.enabled, laneColour);
                drawToggle (g, buttons.removeFromLeft (23), "M", lane.muted, graphColour (i, 4));
                drawToggle (g, buttons.removeFromLeft (23), "S", lane.solo, graphColour (i, 1));
                const auto freezeText = lane.freezeInProgress ? "..." : (lane.frozen && lane.freezeStale ? "!" : "F");
                drawToggle (g, buttons.removeFromLeft (23), freezeText, lane.frozen || lane.freezeInProgress, lane.freezeStale ? graphColour (i, 4) : graphColour (i, 2));

                auto volumeArea = rowText.removeFromRight (62).reduced (7, 0);
                drawVolumeControl (g, volumeArea, lane.volume, laneColour, lane.enabled);

                g.setColour (selected ? ink() : mutedInk().withAlpha (lane.enabled ? 1.0f : 0.52f));
                g.setFont (juce::FontOptions (12.5f, selected ? juce::Font::bold : juce::Font::plain));
                g.drawFittedText (lane.name, rowText, juce::Justification::centredLeft, 1);
            }
        }

        drawScrollBar (g);
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        grabKeyboardFocus();

        if (state == nullptr)
            return;

        auto list = getTrackListBounds();
        for (int i = 0; i < static_cast<int> (state->lanes.size()); ++i)
        {
            auto row = list.removeFromTop (rowHeight).reduced (0, rowVerticalInset);
            if (row.contains (event.getPosition()))
            {
                auto controls = row.reduced (10, 0).removeFromRight (92);
                auto enabledArea = controls.removeFromLeft (23);
                auto muteArea = controls.removeFromLeft (23);
                auto soloArea = controls.removeFromLeft (23);
                auto freezeArea = controls.removeFromLeft (23);
                auto volumeArea = getVolumeBoundsForRow (row);
                selectedIndex = i;
                if (volumeArea.contains (event.getPosition()))
                {
                    draggingVolumeIndex = i;
                    updateVolumeFromMouse (i, event.position.x);
                }
                else if (enabledArea.contains (event.getPosition()))
                {
                    if (onEnabledToggled)
                        onEnabledToggled (i);
                }
                else if (muteArea.contains (event.getPosition()))
                {
                    if (onMuteToggled)
                        onMuteToggled (i);
                }
                else if (soloArea.contains (event.getPosition()))
                {
                    if (onSoloToggled)
                        onSoloToggled (i);
                }
                else if (freezeArea.contains (event.getPosition()))
                {
                    if (onFreezeToggled)
                        onFreezeToggled (i);
                }
                else if (onTrackSelected)
                    onTrackSelected (i);
                repaint();
                return;
            }
        }
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (isLaneDeleteKey (key))
        {
            if (onDeleteSelectedLaneRequested)
                onDeleteSelectedLaneRequested();
            return true;
        }

        return false;
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        if (state == nullptr || draggingVolumeIndex < 0)
            return;

        updateVolumeFromMouse (draggingVolumeIndex, event.position.x);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        draggingVolumeIndex = -1;
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        scrollOffset -= wheel.deltaY * static_cast<float> (rowHeight * 2);
        clampScroll();
        repaint();
    }

    void resized() override
    {
        clampScroll();
    }

private:
    juce::Colour getTrackColour (int index) const
    {
        return paletteColour (index);
    }

    void drawToggle (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& text, bool active, juce::Colour colour) const
    {
        auto pill = area.reduced (2, 10).toFloat();
        g.setColour (active ? colour.withAlpha (0.92f) : juce::Colour (0xff111318));
        g.fillRoundedRectangle (pill, 3.0f);
        g.setColour (active ? juce::Colour (0xff111318).withAlpha (0.78f) : juce::Colour (0xff4b5560));
        g.drawRoundedRectangle (pill, 3.0f, active ? 0.8f : 1.0f);
        g.setColour (active ? juce::Colour (0xff101318) : mutedInk().withAlpha (0.72f));
        g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        g.drawText (text, area, juce::Justification::centred);
    }

    void drawVolumeControl (juce::Graphics& g, juce::Rectangle<int> area, float volume, juce::Colour colour, bool enabled) const
    {
        const auto clipped = juce::jlimit (0.0f, 1.0f, volume);
        auto valueArea = area.removeFromRight (24);
        auto slider = area.reduced (0, 12);

        g.setColour (juce::Colour (0xff111318).withAlpha (enabled ? 1.0f : 0.55f));
        g.fillRoundedRectangle (slider.toFloat(), 2.0f);

        auto fill = slider.toFloat();
        fill.setWidth (juce::jmax (2.0f, fill.getWidth() * clipped));
        g.setColour (colour.withAlpha (enabled ? 0.78f : 0.24f));
        g.fillRoundedRectangle (fill, 2.0f);

        g.setColour (mutedInk().withAlpha (enabled ? 0.72f : 0.38f));
        g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        g.drawText (juce::String (clipped, 2), valueArea, juce::Justification::centredRight);
    }

    juce::Rectangle<int> getVolumeBoundsForRow (juce::Rectangle<int> row) const
    {
        auto rowText = row.reduced (10, 0);
        rowText.removeFromLeft (12);
        rowText.removeFromRight (92);
        return rowText.removeFromRight (62).reduced (7, 7);
    }

    void updateVolumeFromMouse (int index, float x)
    {
        if (state == nullptr || index < 0 || index >= static_cast<int> (state->lanes.size()))
            return;

        auto list = getTrackListBounds();
        auto row = list.removeFromTop (rowHeight * index + rowHeight).removeFromBottom (rowHeight).reduced (0, rowVerticalInset);
        auto volumeArea = getVolumeBoundsForRow (row);
        const auto newVolume = juce::jlimit (0.0f, 1.0f, (x - static_cast<float> (volumeArea.getX())) / static_cast<float> (juce::jmax (1, volumeArea.getWidth() - 24)));

        if (onVolumeChanged)
            onVolumeChanged (index, newVolume);

        repaint();
    }

    juce::Rectangle<int> getTrackListBounds() const
    {
        return getTrackViewportBounds().translated (0, -juce::roundToInt (scrollOffset));
    }

    juce::Rectangle<int> getTrackViewportBounds() const
    {
        auto viewport = getLocalBounds().reduced (viewportPadding, viewportPadding);
        if (needsScrollBar())
            viewport.removeFromRight (scrollbarGutter);

        return viewport;
    }

    int getContentHeight() const
    {
        return state == nullptr ? 0 : static_cast<int> (state->lanes.size()) * rowHeight;
    }

    int getViewportHeight() const
    {
        return getLocalBounds().reduced (viewportPadding, viewportPadding).getHeight();
    }

    void clampScroll()
    {
        const auto maxScroll = juce::jmax (0.0f, static_cast<float> (getContentHeight() - getViewportHeight()));
        scrollOffset = juce::jlimit (0.0f, maxScroll, scrollOffset);
    }

    void scrollSelectedIntoView()
    {
        if (state == nullptr || state->lanes.empty())
            return;

        const auto index = juce::jlimit (0, static_cast<int> (state->lanes.size()) - 1, selectedIndex);
        const auto rowTop = static_cast<float> (index * rowHeight);
        const auto rowBottom = rowTop + static_cast<float> (rowHeight);
        const auto viewportHeight = static_cast<float> (getViewportHeight());

        if (rowTop < scrollOffset)
            scrollOffset = rowTop;
        else if (rowBottom > scrollOffset + viewportHeight)
            scrollOffset = rowBottom - viewportHeight;

        clampScroll();
    }

    bool needsScrollBar() const
    {
        return getContentHeight() > getViewportHeight();
    }

    void drawScrollBar (juce::Graphics& g) const
    {
        const auto contentHeight = getContentHeight();
        const auto viewportHeight = getViewportHeight();
        if (contentHeight <= viewportHeight || viewportHeight <= 0)
            return;

        auto track = getLocalBounds().reduced (3, viewportPadding).removeFromRight (4).toFloat();
        const auto thumbHeight = juce::jmax (22.0f, track.getHeight() * static_cast<float> (viewportHeight) / static_cast<float> (contentHeight));
        const auto maxScroll = static_cast<float> (contentHeight - viewportHeight);
        const auto thumbY = track.getY() + (track.getHeight() - thumbHeight) * (scrollOffset / juce::jmax (1.0f, maxScroll));
        g.setColour (graphColour (selectedIndex).withAlpha (0.12f));
        g.fillRoundedRectangle (track, 2.0f);
        g.setColour (graphColour (selectedIndex).withAlpha (0.62f));
        g.fillRoundedRectangle (track.withY (thumbY).withHeight (thumbHeight), 2.0f);
    }

    State* state = nullptr;
    int selectedIndex = 0;
    int draggingVolumeIndex = -1;
    float scrollOffset = 0.0f;
    static constexpr int rowHeight = 32;
    static constexpr int rowVerticalInset = 2;
    static constexpr int viewportPadding = 8;
    static constexpr int scrollbarGutter = 10;
};

class MixerComponent final : public juce::Component,
                             private juce::Timer
{
public:
    std::function<LaneMeterValues (const juce::String&)> meterProvider;
    std::function<void (int)> onTrackSelected;
    std::function<void (int)> onEnabledToggled;
    std::function<void (int)> onMuteToggled;
    std::function<void (int)> onSoloToggled;
    std::function<void (int)> onFreezeToggled;
    std::function<void (int, float)> onVolumeChanged;
    std::function<void (int, float)> onGainChanged;
    std::function<void (int, float)> onPanChanged;
    std::function<void()> onDeleteSelectedLaneRequested;

    MixerComponent()
    {
        setWantsKeyboardFocus (true);
        startTimerHz (36);
    }

    void setState (State& stateToShow, int selectedLane, bool running)
    {
        state = &stateToShow;
        selectedIndex = selectedLane;
        transportRunning = running;
        clampScroll();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds();
        g.setColour (panelFill());
        g.fillRoundedRectangle (bounds.toFloat(), 5.0f);

        if (state == nullptr)
            return;

        auto area = bounds.reduced (10, 9);
        auto header = area.removeFromTop (30);
        g.setColour (ink());
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText ("Lane mix", header.removeFromLeft (90), juce::Justification::centredLeft);
        g.setColour (transportRunning ? graphColour (selectedIndex).withAlpha (0.9f) : mutedInk().withAlpha (0.55f));
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText (transportRunning ? "LIVE" : "IDLE", header, juce::Justification::centredRight);

        {
            juce::Graphics::ScopedSaveState saveState (g);
            g.reduceClipRegion (area);

            auto rowArea = area.translated (0, -juce::roundToInt (scrollOffset));
            rowArea.removeFromTop (3);
            for (int i = 0; i < static_cast<int> (state->lanes.size()); ++i)
            {
                auto row = rowArea.removeFromTop (72).reduced (0, 4);
                if (row.getBottom() < area.getY() || row.getY() > area.getBottom())
                    continue;

                drawLaneStrip (g, row, i);
            }
        }

        drawScrollBar (g, area);
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        grabKeyboardFocus();

        if (state == nullptr)
            return;

        for (int i = 0; i < static_cast<int> (state->lanes.size()); ++i)
        {
            const auto row = getRowBounds (i);
            if (! row.contains (event.getPosition()))
                continue;

            selectedIndex = i;
            auto buttons = getButtonBounds (row);
            const auto volumeArea = getVolumeBounds (row);
            const auto gainArea = getGainBounds (row);
            const auto panArea = getPanBounds (row);

            if (volumeArea.contains (event.getPosition()))
            {
                draggingVolumeIndex = i;
                updateVolumeFromMouse (i, event.position.x);
            }
            else if (gainArea.contains (event.getPosition()))
            {
                draggingGainIndex = i;
                updateGainFromMouse (i, event.position.x);
            }
            else if (panArea.contains (event.getPosition()))
            {
                draggingPanIndex = i;
                updatePanFromMouse (i, event.position.x);
            }
            else if (buttons.enabled.contains (event.getPosition()))
            {
                if (onEnabledToggled)
                    onEnabledToggled (i);
            }
            else if (buttons.mute.contains (event.getPosition()))
            {
                if (onMuteToggled)
                    onMuteToggled (i);
            }
            else if (buttons.solo.contains (event.getPosition()))
            {
                if (onSoloToggled)
                    onSoloToggled (i);
            }
            else if (buttons.freeze.contains (event.getPosition()))
            {
                if (onFreezeToggled)
                    onFreezeToggled (i);
            }
            else if (onTrackSelected)
            {
                onTrackSelected (i);
            }

            repaint();
            return;
        }
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (isLaneDeleteKey (key))
        {
            if (onDeleteSelectedLaneRequested)
                onDeleteSelectedLaneRequested();
            return true;
        }

        return false;
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        if (state == nullptr)
            return;

        if (draggingVolumeIndex >= 0)
            updateVolumeFromMouse (draggingVolumeIndex, event.position.x);
        else if (draggingGainIndex >= 0)
            updateGainFromMouse (draggingGainIndex, event.position.x);
        else if (draggingPanIndex >= 0)
            updatePanFromMouse (draggingPanIndex, event.position.x);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        draggingVolumeIndex = -1;
        draggingGainIndex = -1;
        draggingPanIndex = -1;
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        scrollOffset -= wheel.deltaY * 112.0f;
        clampScroll();
        repaint();
    }

    void resized() override
    {
        clampScroll();
    }

private:
    struct ButtonBounds
    {
        juce::Rectangle<int> enabled;
        juce::Rectangle<int> mute;
        juce::Rectangle<int> solo;
        juce::Rectangle<int> freeze;
    };

    void timerCallback() override
    {
        repaint();
    }

    void drawLaneStrip (juce::Graphics& g, juce::Rectangle<int> row, int index) const
    {
        const auto& lane = state->lanes[static_cast<size_t> (index)];
        const auto selected = index == selectedIndex;
        const auto laneColour = paletteColour (index);
        const auto meter = meterProvider ? meterProvider (lane.id) : LaneMeterValues {};
        const auto rmsLevel = meterToDisplay (meter.rms);
        const auto peakLevel = meterToDisplay (meter.peak);
        const auto active = meter.live && meter.peak > 0.0015f;

        g.setColour (selected ? rowFill().interpolatedWith (laneColour, 0.14f)
                              : rowFill().interpolatedWith (laneColour, 0.025f).withAlpha (lane.enabled ? 0.88f : 0.42f));
        g.fillRoundedRectangle (row.toFloat(), 4.0f);
        g.setColour ((selected ? laneColour : hairline()).withAlpha (selected ? 0.72f : 0.62f));
        g.drawRoundedRectangle (row.toFloat(), 4.0f, selected ? 1.0f : 0.7f);

        auto top = row.reduced (10, 4).removeFromTop (22);
        auto buttons = getButtonBounds (row);

        auto dotArea = top.removeFromLeft (13).withSizeKeepingCentre (8, 8).toFloat();
        g.setColour (laneColour.withAlpha (lane.enabled ? (active ? 0.95f : 0.66f) : 0.26f));
        g.fillEllipse (dotArea);
        g.setColour (active ? ink().withAlpha (0.85f) : juce::Colour (0xff101318).withAlpha (0.9f));
        g.drawEllipse (dotArea.expanded (1.0f), active ? 1.3f : 0.8f);

        auto valueArea = top.removeFromRight (36);
        top.removeFromRight (92);
        g.setColour (selected ? ink() : mutedInk().withAlpha (lane.enabled ? 0.95f : 0.46f));
        g.setFont (juce::FontOptions (12.0f, selected ? juce::Font::bold : juce::Font::plain));
        g.drawFittedText (lane.name, top, juce::Justification::centredLeft, 1);

        g.setColour (mutedInk().withAlpha (lane.enabled ? 0.74f : 0.36f));
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (juce::String (lane.volume, 2), valueArea, juce::Justification::centredRight);

        drawToggle (g, buttons.enabled, "E", lane.enabled, laneColour);
        drawToggle (g, buttons.mute, "M", lane.muted, graphColour (index, 4));
        drawToggle (g, buttons.solo, "S", lane.solo, graphColour (index, 1));
        const auto freezeText = lane.freezeInProgress ? "..." : (lane.frozen && lane.freezeStale ? "!" : "F");
        drawToggle (g, buttons.freeze, freezeText, lane.frozen || lane.freezeInProgress, lane.freezeStale ? graphColour (index, 4) : graphColour (index, 2));

        auto volumeArea = getVolumeBounds (row);
        const auto clipped = juce::jlimit (0.0f, 1.0f, lane.volume);
        const auto gain = juce::jlimit (0.0f, 2.0f, lane.gain);
        const auto pan = juce::jlimit (-1.0f, 1.0f, lane.pan);
        const auto level = meter.live ? rmsLevel : 0.0f;
        const auto peak = meter.live ? peakLevel : 0.0f;

        g.setColour (juce::Colour (0xff101318).withAlpha (lane.enabled ? 1.0f : 0.56f));
        g.fillRoundedRectangle (volumeArea.toFloat(), 3.0f);

        auto meterFill = volumeArea.toFloat();
        meterFill.setWidth (juce::jmax (2.0f, meterFill.getWidth() * level));
        g.setColour (laneColour.withAlpha (0.24f));
        g.fillRoundedRectangle (meterFill, 3.0f);

        if (peak > 0.0f)
        {
            const auto peakX = volumeArea.getX() + juce::roundToInt (static_cast<float> (volumeArea.getWidth()) * peak);
            g.setColour (ink().withAlpha (0.62f));
            g.drawVerticalLine (juce::jlimit (volumeArea.getX(), volumeArea.getRight() - 1, peakX),
                                static_cast<float> (volumeArea.getY() + 2),
                                static_cast<float> (volumeArea.getBottom() - 2));
        }

        auto volumeFill = volumeArea.toFloat().reduced (0.0f, 5.0f);
        volumeFill.setWidth (juce::jmax (3.0f, volumeFill.getWidth() * clipped));
        g.setColour (laneColour.withAlpha (lane.enabled ? 0.84f : 0.22f));
        g.fillRoundedRectangle (volumeFill, 2.0f);

        g.setColour (mutedInk().withAlpha (0.18f));
        g.drawRoundedRectangle (volumeArea.toFloat(), 3.0f, 0.8f);

        auto gainArea = getGainBounds (row);
        auto panArea = getPanBounds (row);
        drawSmallMixStrip (g, gainArea, "gain", gain / 2.0f, "x" + juce::String (gain, 2), laneColour, lane.enabled);
        drawPanStrip (g, panArea, pan, laneColour, lane.enabled);
    }

    void drawSmallMixStrip (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& label, float normalised, const juce::String& value, juce::Colour colour, bool enabled) const
    {
        area = area.reduced (0, 1);
        auto labelArea = area.removeFromLeft (34);
        auto valueArea = area.removeFromRight (34);
        auto strip = area.reduced (0, 6);

        g.setColour (mutedInk().withAlpha (enabled ? 0.64f : 0.30f));
        g.setFont (juce::FontOptions (8.8f, juce::Font::bold));
        g.drawText (label, labelArea, juce::Justification::centredLeft);
        g.drawText (value, valueArea, juce::Justification::centredRight);

        g.setColour (juce::Colour (0xff101318).withAlpha (enabled ? 1.0f : 0.50f));
        g.fillRoundedRectangle (strip.toFloat(), 2.0f);
        auto fill = strip.toFloat();
        fill.setWidth (juce::jmax (2.0f, fill.getWidth() * juce::jlimit (0.0f, 1.0f, normalised)));
        g.setColour (colour.withAlpha (enabled ? 0.72f : 0.22f));
        g.fillRoundedRectangle (fill, 2.0f);
    }

    void drawPanStrip (juce::Graphics& g, juce::Rectangle<int> area, float pan, juce::Colour colour, bool enabled) const
    {
        area = area.reduced (0, 1);
        auto labelArea = area.removeFromLeft (26);
        auto valueArea = area.removeFromRight (26);
        auto strip = area.reduced (0, 6);
        const auto centreX = strip.getCentreX();
        const auto panX = strip.getX() + juce::roundToInt ((pan + 1.0f) * 0.5f * static_cast<float> (strip.getWidth()));
        const auto text = std::abs (pan) < 0.04f ? "C" : (pan < 0.0f ? "L" + juce::String (std::abs (pan), 1) : "R" + juce::String (pan, 1));

        g.setColour (mutedInk().withAlpha (enabled ? 0.64f : 0.30f));
        g.setFont (juce::FontOptions (8.8f, juce::Font::bold));
        g.drawText ("pan", labelArea, juce::Justification::centredLeft);
        g.drawText (text, valueArea, juce::Justification::centredRight);

        g.setColour (juce::Colour (0xff101318).withAlpha (enabled ? 1.0f : 0.50f));
        g.fillRoundedRectangle (strip.toFloat(), 2.0f);
        g.setColour (mutedInk().withAlpha (0.26f));
        g.drawVerticalLine (centreX, static_cast<float> (strip.getY()), static_cast<float> (strip.getBottom()));
        g.setColour (colour.withAlpha (enabled ? 0.80f : 0.24f));
        g.fillEllipse (static_cast<float> (juce::jlimit (strip.getX(), strip.getRight() - 1, panX)) - 3.0f,
                       static_cast<float> (strip.getCentreY()) - 3.0f, 6.0f, 6.0f);
    }

    void drawToggle (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& text, bool active, juce::Colour colour) const
    {
        auto pill = area.reduced (2, 5).toFloat();
        g.setColour (active ? colour.withAlpha (0.92f) : juce::Colour (0xff111318));
        g.fillRoundedRectangle (pill, 3.0f);
        g.setColour (active ? juce::Colour (0xff101318).withAlpha (0.72f) : juce::Colour (0xff4b5560));
        g.drawRoundedRectangle (pill, 3.0f, active ? 0.8f : 1.0f);
        g.setColour (active ? juce::Colour (0xff101318) : mutedInk().withAlpha (0.72f));
        g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        g.drawText (text, area, juce::Justification::centred);
    }

    juce::Rectangle<int> getRowBounds (int index) const
    {
        auto area = getRowsViewportBounds().translated (0, -juce::roundToInt (scrollOffset));
        area.removeFromTop (3);
        return area.removeFromTop (72 * index + 72).removeFromBottom (72).reduced (0, 4);
    }

    juce::Rectangle<int> getRowsViewportBounds() const
    {
        auto area = getLocalBounds().reduced (10, 9);
        area.removeFromTop (30);
        return area;
    }

    juce::Rectangle<int> getVolumeBounds (juce::Rectangle<int> row) const
    {
        auto lower = row.reduced (10, 5).removeFromBottom (34);
        return lower.removeFromTop (17);
    }

    juce::Rectangle<int> getGainBounds (juce::Rectangle<int> row) const
    {
        auto lower = row.reduced (10, 5).removeFromBottom (17);
        return lower.removeFromLeft (lower.getWidth() / 2).reduced (0, 1);
    }

    juce::Rectangle<int> getPanBounds (juce::Rectangle<int> row) const
    {
        auto lower = row.reduced (10, 5).removeFromBottom (17);
        lower.removeFromLeft (lower.getWidth() / 2);
        return lower.reduced (5, 1);
    }

    ButtonBounds getButtonBounds (juce::Rectangle<int> row) const
    {
        auto buttons = row.reduced (10, 4).removeFromTop (22).removeFromRight (92);
        ButtonBounds result;
        result.enabled = buttons.removeFromLeft (23);
        result.mute = buttons.removeFromLeft (23);
        result.solo = buttons.removeFromLeft (23);
        result.freeze = buttons.removeFromLeft (23);
        return result;
    }

    void updateVolumeFromMouse (int index, float x)
    {
        if (state == nullptr || index < 0 || index >= static_cast<int> (state->lanes.size()))
            return;

        const auto volumeArea = getVolumeBounds (getRowBounds (index));
        const auto newVolume = juce::jlimit (0.0f, 1.0f, (x - static_cast<float> (volumeArea.getX())) / static_cast<float> (juce::jmax (1, volumeArea.getWidth())));

        if (onVolumeChanged)
            onVolumeChanged (index, newVolume);

        repaint();
    }

    void updateGainFromMouse (int index, float x)
    {
        if (state == nullptr || index < 0 || index >= static_cast<int> (state->lanes.size()))
            return;

        const auto gainArea = getGainBounds (getRowBounds (index));
        const auto normalised = (x - static_cast<float> (gainArea.getX())) / static_cast<float> (juce::jmax (1, gainArea.getWidth()));
        if (onGainChanged)
            onGainChanged (index, juce::jlimit (0.0f, 2.0f, normalised * 2.0f));

        repaint();
    }

    void updatePanFromMouse (int index, float x)
    {
        if (state == nullptr || index < 0 || index >= static_cast<int> (state->lanes.size()))
            return;

        const auto panArea = getPanBounds (getRowBounds (index));
        const auto normalised = (x - static_cast<float> (panArea.getX())) / static_cast<float> (juce::jmax (1, panArea.getWidth()));
        if (onPanChanged)
            onPanChanged (index, juce::jlimit (-1.0f, 1.0f, normalised * 2.0f - 1.0f));

        repaint();
    }

    float meterToDisplay (float value) const
    {
        const auto clipped = juce::jlimit (0.000001f, 1.0f, value);
        const auto db = 20.0f * std::log10 (clipped);
        return juce::jlimit (0.0f, 1.0f, (db + 54.0f) / 54.0f);
    }

    int getContentHeight() const
    {
        return state == nullptr ? 0 : 3 + static_cast<int> (state->lanes.size()) * 72;
    }

    void clampScroll()
    {
        const auto maxScroll = juce::jmax (0.0f, static_cast<float> (getContentHeight() - getRowsViewportBounds().getHeight()));
        scrollOffset = juce::jlimit (0.0f, maxScroll, scrollOffset);
    }

    void drawScrollBar (juce::Graphics& g, juce::Rectangle<int> viewport) const
    {
        const auto contentHeight = getContentHeight();
        const auto viewportHeight = viewport.getHeight();
        if (contentHeight <= viewportHeight || viewportHeight <= 0)
            return;

        auto track = viewport.reduced (0, 6).removeFromRight (4).toFloat();
        const auto thumbHeight = juce::jmax (22.0f, track.getHeight() * static_cast<float> (viewportHeight) / static_cast<float> (contentHeight));
        const auto maxScroll = static_cast<float> (contentHeight - viewportHeight);
        const auto thumbY = track.getY() + (track.getHeight() - thumbHeight) * (scrollOffset / juce::jmax (1.0f, maxScroll));
        g.setColour (graphColour (selectedIndex).withAlpha (0.12f));
        g.fillRoundedRectangle (track, 2.0f);
        g.setColour (graphColour (selectedIndex).withAlpha (0.62f));
        g.fillRoundedRectangle (track.withY (thumbY).withHeight (thumbHeight), 2.0f);
    }

    State* state = nullptr;
    int selectedIndex = 0;
    int draggingVolumeIndex = -1;
    int draggingGainIndex = -1;
    int draggingPanIndex = -1;
    bool transportRunning = false;
    float scrollOffset = 0.0f;
};

class PaneDivider final : public juce::Component
{
public:
    enum class Orientation
    {
        vertical,
        horizontal
    };

    std::function<void()> onDragStarted;
    std::function<void (int)> onDragged;
    std::function<void()> onDragEnded;

    explicit PaneDivider (Orientation orientationToUse = Orientation::vertical) : orientation (orientationToUse)
    {
        setMouseCursor (orientation == Orientation::vertical ? juce::MouseCursor::LeftRightResizeCursor
                                                             : juce::MouseCursor::UpDownResizeCursor);
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff0f1116).withAlpha (0.72f));
        g.fillRoundedRectangle (orientation == Orientation::vertical ? bounds.reduced (2.0f, 0.0f)
                                                                     : bounds.reduced (0.0f, 2.0f), 3.0f);
        g.setColour (accentB().withAlpha (isMouseOverOrDragging() ? 0.62f : 0.18f));
        g.fillRoundedRectangle (orientation == Orientation::vertical
                                    ? bounds.withSizeKeepingCentre (1.5f, bounds.getHeight() - 20.0f)
                                    : bounds.withSizeKeepingCentre (bounds.getWidth() - 28.0f, 1.5f), 1.0f);
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        if (onDragStarted)
            onDragStarted();
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        if (onDragged)
            onDragged (orientation == Orientation::vertical ? event.getDistanceFromDragStartX()
                                                            : event.getDistanceFromDragStartY());
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (onDragEnded)
            onDragEnded();

        repaint();
    }

private:
    Orientation orientation = Orientation::vertical;
};

class SuperColliderTokeniser final : public juce::CodeTokeniser
{
public:
    enum TokenType
    {
        error = 0,
        comment,
        keyword,
        ugen,
        identifier,
        number,
        string,
        symbol,
        bracket,
        punctuation,
        op
    };

    int readNextToken (juce::CodeDocument::Iterator& source) override
    {
        source.skipWhitespace();

        const auto first = source.peekNextChar();
        if (first == 0)
            return identifier;

        if (first == '/' && source.peekPreviousChar() != '\\')
        {
            source.skip();
            if (source.peekNextChar() == '/')
            {
                source.skipToEndOfLine();
                return comment;
            }

            return op;
        }

        if (first == '"')
        {
            source.skip();
            while (! source.isEOF())
            {
                const auto c = source.nextChar();
                if (c == '\\')
                    source.skip();
                else if (c == '"')
                    break;
            }
            return string;
        }

        if (first == '\\')
        {
            source.skip();
            while (isIdentifierBody (source.peekNextChar()))
                source.skip();
            return symbol;
        }

        if (juce::CharacterFunctions::isDigit (first))
        {
            bool seenDot = false;
            while (juce::CharacterFunctions::isDigit (source.peekNextChar()) || (! seenDot && source.peekNextChar() == '.'))
            {
                seenDot = seenDot || source.peekNextChar() == '.';
                source.skip();
            }
            return number;
        }

        if (isIdentifierStart (first))
        {
            juce::String token;
            while (isIdentifierBody (source.peekNextChar()))
                token << juce::String::charToString (source.nextChar());

            if (isKeyword (token))
                return keyword;
            if (isUGen (token))
                return ugen;
            return identifier;
        }

        if (isBracket (first))
        {
            source.skip();
            return bracket;
        }

        if (juce::String (";,.").containsChar (first))
        {
            source.skip();
            return punctuation;
        }

        source.skip();
        return op;
    }

    juce::CodeEditorComponent::ColourScheme getDefaultColourScheme() override
    {
        juce::CodeEditorComponent::ColourScheme scheme;
        scheme.set ("Error",       juce::Colour (0xffff5c77));
        scheme.set ("Comment",     juce::Colour (0xff68737d));
        scheme.set ("Keyword",     juce::Colour (0xffffc857));
        scheme.set ("UGen",        juce::Colour (0xff52d1dc));
        scheme.set ("Identifier",  ink());
        scheme.set ("Number",      juce::Colour (0xff7bd88f));
        scheme.set ("String",      juce::Colour (0xfff76f8e));
        scheme.set ("Symbol",      juce::Colour (0xffb48cff));
        scheme.set ("Bracket",     juce::Colour (0xfff2efe7));
        scheme.set ("Punctuation", juce::Colour (0xffaeb5bd));
        scheme.set ("Operator",    juce::Colour (0xffff9f68));
        return scheme;
    }

private:
    static bool isIdentifierStart (juce::juce_wchar c)
    {
        return juce::CharacterFunctions::isLetter (c) || c == '_' || c == '~';
    }

    static bool isIdentifierBody (juce::juce_wchar c)
    {
        return isIdentifierStart (c) || juce::CharacterFunctions::isDigit (c);
    }

    static bool isBracket (juce::juce_wchar c)
    {
        return c == '(' || c == ')' || c == '{' || c == '}' || c == '[' || c == ']';
    }

    static bool isKeyword (const juce::String& token)
    {
        static const char* keywords[] =
        {
            "arg", "var", "classvar", "const", "this", "super", "nil", "true", "false",
            "if", "while", "for", "case", "switch", "do", "collect", "select", "reject",
            "inf", "pi"
        };

        for (auto* keyword : keywords)
            if (token == keyword)
                return true;

        return false;
    }

    static bool isUGen (const juce::String& token)
    {
        static const char* ugens[] =
        {
            "SinOsc", "LFTri", "LFSaw", "VarSaw", "Pulse", "Saw", "WhiteNoise", "PinkNoise",
            "Impulse", "Demand", "Dseq", "Dwhite", "Drand", "Env", "EnvGen", "Decay2",
            "Lag", "TRand", "LFNoise0", "LFNoise1", "RLPF", "LPF", "HPF", "BPF",
            "Limiter", "LeakDC", "Compander", "Pan2", "Splay", "CombC", "Mix",
            "In", "ReplaceOut", "Out", "SendReply", "Amplitude", "Silent"
        };

        for (auto* ugenName : ugens)
            if (token == ugenName)
                return true;

        return false;
    }
};

class SuperColliderCodeEditor final : public juce::CodeEditorComponent
{
public:
    SuperColliderCodeEditor (juce::CodeDocument& document, juce::CodeTokeniser* tokeniser)
        : juce::CodeEditorComponent (document, tokeniser)
    {
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        drawCurrentLine (g);
        drawBracketMatch (g);
    }

    void caretPositionMoved() override
    {
        repaint();
    }

private:
    void drawCurrentLine (juce::Graphics& g)
    {
        const auto caret = getCaretPos();
        const auto bounds = getCharacterBounds ({ getDocument(), caret.getLineNumber(), 0 });
        if (bounds.isEmpty())
            return;

        g.setColour (accentB().withAlpha (0.055f));
        g.fillRect (juce::Rectangle<int> (0, bounds.getY(), getWidth(), getLineHeight()));
    }

    void drawBracketMatch (juce::Graphics& g)
    {
        const auto text = getDocument().getAllContent();
        if (text.isEmpty())
            return;

        const auto caretIndex = getCaretPosition();
        const auto bracketIndex = findBracketNearCaret (text, caretIndex);
        if (bracketIndex < 0)
            return;

        const auto matchIndex = findMatchingBracket (text, bracketIndex);
        drawBracketBox (g, bracketIndex, matchIndex >= 0 ? accentA() : accentC());

        if (matchIndex >= 0)
            drawBracketBox (g, matchIndex, accentA());
    }

    int findBracketNearCaret (const juce::String& text, int caretIndex) const
    {
        if (caretIndex > 0 && isBracket (text[caretIndex - 1]))
            return caretIndex - 1;

        if (caretIndex < text.length() && isBracket (text[caretIndex]))
            return caretIndex;

        return -1;
    }

    int findMatchingBracket (const juce::String& text, int bracketIndex) const
    {
        const auto open = text[bracketIndex];
        const auto close = matchingBracket (open);
        if (close == 0)
            return -1;

        const auto direction = isOpeningBracket (open) ? 1 : -1;
        const auto targetOpen = direction > 0 ? open : close;
        const auto targetClose = direction > 0 ? close : open;
        int depth = 0;

        for (int i = bracketIndex; i >= 0 && i < text.length(); i += direction)
        {
            const auto c = text[i];
            if (c == targetOpen)
                ++depth;
            else if (c == targetClose)
            {
                --depth;
                if (depth == 0)
                    return i;
            }
        }

        return -1;
    }

    void drawBracketBox (juce::Graphics& g, int index, juce::Colour colour)
    {
        auto bounds = getCharacterBounds ({ getDocument(), index }).toFloat().expanded (1.5f, 1.0f);
        if (bounds.isEmpty())
            return;

        g.setColour (colour.withAlpha (0.18f));
        g.fillRoundedRectangle (bounds, 2.0f);
        g.setColour (colour.withAlpha (0.90f));
        g.drawRoundedRectangle (bounds, 2.0f, 1.1f);
    }

    static bool isBracket (juce::juce_wchar c)
    {
        return c == '(' || c == ')' || c == '{' || c == '}' || c == '[' || c == ']';
    }

    static bool isOpeningBracket (juce::juce_wchar c)
    {
        return c == '(' || c == '{' || c == '[';
    }

    static juce::juce_wchar matchingBracket (juce::juce_wchar c)
    {
        switch (c)
        {
            case '(': return ')';
            case '{': return '}';
            case '[': return ']';
            case ')': return '(';
            case '}': return '{';
            case ']': return '[';
            default: break;
        }

        return 0;
    }
};

struct AudioExportSettings
{
    juce::String range = "cycle";
    int cycles = 1;
    double customSeconds = 30.0;
    double tailSeconds = 2.0;
    juce::String sampleFormat = "int16";

    bool operator== (const AudioExportSettings& other) const
    {
        return range == other.range
            && cycles == other.cycles
            && std::abs (customSeconds - other.customSeconds) < 0.001
            && std::abs (tailSeconds - other.tailSeconds) < 0.001
            && sampleFormat == other.sampleFormat;
    }

    bool operator!= (const AudioExportSettings& other) const
    {
        return ! (*this == other);
    }
};

class AudioSettingsComponent final : public juce::Component
{
public:
    AudioSettingsComponent (juce::AudioDeviceManager& manager,
                            bool colourblindSafeEnabled,
                            std::function<void (bool)> colourblindSafeChanged,
                            SuperColliderAudioSettings scAudioSettingsToUse,
                            std::function<void (SuperColliderAudioSettings)> scAudioSettingsChanged)
        : onColourblindSafeChanged (std::move (colourblindSafeChanged)),
          onScAudioSettingsChanged (std::move (scAudioSettingsChanged)),
          scAudioSettings (std::move (scAudioSettingsToUse)),
          selector (manager, 0, 0, 0, 2, false, false, true, false)
    {
        addAndMakeVisible (title);
        addAndMakeVisible (colourSectionTitle);
        addAndMakeVisible (colourblindSafeToggle);
        addAndMakeVisible (scSectionTitle);
        addAndMakeVisible (scDeviceLabel);
        addAndMakeVisible (scDeviceEditor);
        addAndMakeVisible (scSampleRateLabel);
        addAndMakeVisible (scSampleRateEditor);
        addAndMakeVisible (scBufferLabel);
        addAndMakeVisible (scBufferEditor);
        addAndMakeVisible (scChannelsLabel);
        addAndMakeVisible (scChannelsEditor);
        addAndMakeVisible (scNote);
        addAndMakeVisible (audioSectionTitle);
        addAndMakeVisible (note);
        addAndMakeVisible (selector);

        title.setText ("Settings", juce::dontSendNotification);
        title.setFont (juce::FontOptions (21.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, ink());

        colourSectionTitle.setText ("Appearance", juce::dontSendNotification);
        colourSectionTitle.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        colourSectionTitle.setColour (juce::Label::textColourId, mutedInk());
        colourblindSafeToggle.setButtonText ("Colourblind-safe colours");
        colourblindSafeToggle.setToggleState (colourblindSafeEnabled, juce::dontSendNotification);
        colourblindSafeToggle.setColour (juce::ToggleButton::textColourId, ink());
        colourblindSafeToggle.setColour (juce::ToggleButton::tickColourId, accentA());
        colourblindSafeToggle.setColour (juce::ToggleButton::tickDisabledColourId, mutedInk().withAlpha (0.40f));
        colourblindSafeToggle.onClick = [this]
        {
            if (onColourblindSafeChanged)
                onColourblindSafeChanged (colourblindSafeToggle.getToggleState());
        };

        scSectionTitle.setText ("SuperCollider audio", juce::dontSendNotification);
        audioSectionTitle.setText ("JUCE audio devices", juce::dontSendNotification);
        for (auto* label : { &scSectionTitle, &audioSectionTitle })
        {
            label->setFont (juce::FontOptions (12.0f, juce::Font::bold));
            label->setColour (juce::Label::textColourId, mutedInk());
        }

        scDeviceLabel.setText ("Output device", juce::dontSendNotification);
        scSampleRateLabel.setText ("Sample rate", juce::dontSendNotification);
        scBufferLabel.setText ("Buffer", juce::dontSendNotification);
        scChannelsLabel.setText ("Outs", juce::dontSendNotification);
        for (auto* label : { &scDeviceLabel, &scSampleRateLabel, &scBufferLabel, &scChannelsLabel })
        {
            label->setFont (juce::FontOptions (12.0f, juce::Font::bold));
            label->setColour (juce::Label::textColourId, mutedInk());
            label->setJustificationType (juce::Justification::centredLeft);
        }

        configureSettingsEditor (scDeviceEditor);
        configureSettingsEditor (scSampleRateEditor);
        configureSettingsEditor (scBufferEditor);
        configureSettingsEditor (scChannelsEditor);
        scSampleRateEditor.setInputRestrictions (8, "0123456789.");
        scBufferEditor.setInputRestrictions (4, "0123456789");
        scChannelsEditor.setInputRestrictions (2, "0123456789");

        scDeviceEditor.setText (scAudioSettings.outputDevice, false);
        scSampleRateEditor.setText (scAudioSettings.sampleRate <= 0.0 ? juce::String() : juce::String (scAudioSettings.sampleRate, 1), false);
        scBufferEditor.setText (juce::String (scAudioSettings.hardwareBufferSize), false);
        scChannelsEditor.setText (juce::String (scAudioSettings.outputChannels), false);

        scDeviceEditor.onReturnKey = [this] { commitScAudioSettings(); };
        scDeviceEditor.onFocusLost = [this] { commitScAudioSettings(); };
        scSampleRateEditor.onReturnKey = [this] { commitScAudioSettings(); };
        scSampleRateEditor.onFocusLost = [this] { commitScAudioSettings(); };
        scBufferEditor.onReturnKey = [this] { commitScAudioSettings(); };
        scBufferEditor.onFocusLost = [this] { commitScAudioSettings(); };
        scChannelsEditor.onReturnKey = [this] { commitScAudioSettings(); };
        scChannelsEditor.onFocusLost = [this] { commitScAudioSettings(); };

        scNote.setText ("Leave output device blank to use SuperCollider's default. Changes restart the SC bridge on the next audio action.",
                        juce::dontSendNotification);
        scNote.setFont (juce::FontOptions (11.5f));
        scNote.setColour (juce::Label::textColourId, mutedInk());
        scNote.setJustificationType (juce::Justification::centredLeft);

        exportSectionTitle.setText ("Audio export", juce::dontSendNotification);
        exportSectionTitle.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        exportSectionTitle.setColour (juce::Label::textColourId, mutedInk());
        exportRangeLabel.setText ("Range", juce::dontSendNotification);
        exportCyclesLabel.setText ("Cycles", juce::dontSendNotification);
        exportCustomLabel.setText ("Seconds", juce::dontSendNotification);
        exportTailLabel.setText ("Tail", juce::dontSendNotification);
        exportFormatLabel.setText ("Format", juce::dontSendNotification);
        for (auto* label : { &exportRangeLabel, &exportCyclesLabel, &exportCustomLabel, &exportTailLabel, &exportFormatLabel })
        {
            label->setFont (juce::FontOptions (12.0f, juce::Font::bold));
            label->setColour (juce::Label::textColourId, mutedInk());
            label->setJustificationType (juce::Justification::centredLeft);
        }

        exportRangeBox.addItem ("Project cycle", 1);
        exportRangeBox.addItem ("Selected state", 2);
        exportRangeBox.addItem ("Custom length", 3);
        exportRangeBox.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff111318));
        exportRangeBox.setColour (juce::ComboBox::textColourId, ink());
        exportRangeBox.setColour (juce::ComboBox::outlineColourId, hairline());
        exportRangeBox.setSelectedItemIndex (rangeIndexFor (exportSettings.range), juce::dontSendNotification);
        exportRangeBox.onChange = [this] { commitExportSettings(); };

        configureSettingsEditor (exportCyclesEditor);
        configureSettingsEditor (exportCustomEditor);
        configureSettingsEditor (exportTailEditor);
        exportCyclesEditor.setInputRestrictions (2, "0123456789");
        exportCustomEditor.setInputRestrictions (6, "0123456789.");
        exportTailEditor.setInputRestrictions (5, "0123456789.");
        exportCyclesEditor.setText (juce::String (exportSettings.cycles), false);
        exportCustomEditor.setText (juce::String (exportSettings.customSeconds, 1), false);
        exportTailEditor.setText (juce::String (exportSettings.tailSeconds, 1), false);
        exportCyclesEditor.onReturnKey = [this] { commitExportSettings(); };
        exportCyclesEditor.onFocusLost = [this] { commitExportSettings(); };
        exportCustomEditor.onReturnKey = [this] { commitExportSettings(); };
        exportCustomEditor.onFocusLost = [this] { commitExportSettings(); };
        exportTailEditor.onReturnKey = [this] { commitExportSettings(); };
        exportTailEditor.onFocusLost = [this] { commitExportSettings(); };

        exportFormatBox.addItem ("16-bit WAV", 1);
        exportFormatBox.addItem ("24-bit WAV", 2);
        exportFormatBox.addItem ("32-bit float WAV", 3);
        exportFormatBox.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff111318));
        exportFormatBox.setColour (juce::ComboBox::textColourId, ink());
        exportFormatBox.setColour (juce::ComboBox::outlineColourId, hairline());
        exportFormatBox.setSelectedItemIndex (formatIndexFor (exportSettings.sampleFormat), juce::dontSendNotification);
        exportFormatBox.onChange = [this] { commitExportSettings(); };

        audioSectionTitle.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        audioSectionTitle.setColour (juce::Label::textColourId, mutedInk());

        note.setText ("JUCE device settings are kept for app-side audio features. Live script output is controlled by the SuperCollider settings above.",
                      juce::dontSendNotification);
        note.setFont (juce::FontOptions (12.5f));
        note.setColour (juce::Label::textColourId, mutedInk());
        note.setJustificationType (juce::Justification::centredLeft);

        selector.setItemHeight (24);
        setSize (590, 680);
    }

    void paint (juce::Graphics& g) override
    {
        juce::ColourGradient bg (backgroundTop(), 0.0f, 0.0f, backgroundBottom(), 0.0f, static_cast<float> (getHeight()), false);
        g.setGradientFill (bg);
        g.fillAll();
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (18, 16);
        title.setBounds (area.removeFromTop (30));
        area.removeFromTop (10);
        colourSectionTitle.setBounds (area.removeFromTop (22));
        colourblindSafeToggle.setBounds (area.removeFromTop (28).reduced (0, 2));
        area.removeFromTop (12);
        scSectionTitle.setBounds (area.removeFromTop (22));
        auto deviceRow = area.removeFromTop (32);
        scDeviceLabel.setBounds (deviceRow.removeFromLeft (102).reduced (0, 4));
        scDeviceEditor.setBounds (deviceRow.reduced (0, 3));
        auto numericRow = area.removeFromTop (32);
        scSampleRateLabel.setBounds (numericRow.removeFromLeft (86).reduced (0, 4));
        scSampleRateEditor.setBounds (numericRow.removeFromLeft (78).reduced (0, 3));
        numericRow.removeFromLeft (12);
        scBufferLabel.setBounds (numericRow.removeFromLeft (48).reduced (0, 4));
        scBufferEditor.setBounds (numericRow.removeFromLeft (58).reduced (0, 3));
        numericRow.removeFromLeft (12);
        scChannelsLabel.setBounds (numericRow.removeFromLeft (34).reduced (0, 4));
        scChannelsEditor.setBounds (numericRow.removeFromLeft (42).reduced (0, 3));
        scNote.setBounds (area.removeFromTop (40));
        area.removeFromTop (14);
        audioSectionTitle.setBounds (area.removeFromTop (22));
        note.setBounds (area.removeFromTop (54));
        area.removeFromTop (8);
        selector.setBounds (area);
    }

private:
    static int rangeIndexFor (const juce::String& range)
    {
        if (range == "state") return 1;
        if (range == "custom") return 2;
        return 0;
    }

    static juce::String rangeForIndex (int index)
    {
        if (index == 1) return "state";
        if (index == 2) return "custom";
        return "cycle";
    }

    static int formatIndexFor (const juce::String& format)
    {
        if (format == "int24") return 1;
        if (format == "float") return 2;
        return 0;
    }

    static juce::String formatForIndex (int index)
    {
        if (index == 1) return "int24";
        if (index == 2) return "float";
        return "int16";
    }

    static void configureSettingsEditor (juce::TextEditor& editor)
    {
        editor.setMultiLine (false);
        editor.setFont (juce::FontOptions (12.5f));
        editor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff111318));
        editor.setColour (juce::TextEditor::textColourId, ink());
        editor.setColour (juce::TextEditor::outlineColourId, hairline());
        editor.setColour (juce::TextEditor::focusedOutlineColourId, accentA());
    }

    void commitScAudioSettings()
    {
        SuperColliderAudioSettings updated;
        updated.outputDevice = scDeviceEditor.getText().trim();
        updated.sampleRate = scSampleRateEditor.getText().trim().isEmpty() ? 0.0 : scSampleRateEditor.getText().getDoubleValue();
        updated.hardwareBufferSize = scBufferEditor.getText().getIntValue();
        updated.outputChannels = scChannelsEditor.getText().getIntValue();
        updated.sampleRate = updated.sampleRate <= 0.0 ? 0.0 : juce::jlimit (8000.0, 384000.0, updated.sampleRate);
        updated.hardwareBufferSize = juce::jlimit (16, 4096, updated.hardwareBufferSize <= 0 ? 64 : updated.hardwareBufferSize);
        updated.outputChannels = juce::jlimit (1, 64, updated.outputChannels <= 0 ? 2 : updated.outputChannels);

        scSampleRateEditor.setText (updated.sampleRate <= 0.0 ? juce::String() : juce::String (updated.sampleRate, 1), false);
        scBufferEditor.setText (juce::String (updated.hardwareBufferSize), false);
        scChannelsEditor.setText (juce::String (updated.outputChannels), false);

        if (updated == scAudioSettings)
            return;

        scAudioSettings = updated;
        if (onScAudioSettingsChanged)
            onScAudioSettingsChanged (scAudioSettings);
    }

    void commitExportSettings()
    {
        AudioExportSettings updated;
        updated.range = rangeForIndex (exportRangeBox.getSelectedItemIndex());
        updated.cycles = juce::jlimit (1, 16, exportCyclesEditor.getText().getIntValue() <= 0 ? 1 : exportCyclesEditor.getText().getIntValue());
        updated.customSeconds = juce::jlimit (1.0, 1800.0, exportCustomEditor.getText().getDoubleValue() <= 0.0 ? 30.0 : exportCustomEditor.getText().getDoubleValue());
        updated.tailSeconds = juce::jlimit (0.0, 60.0, exportTailEditor.getText().getDoubleValue());
        updated.sampleFormat = formatForIndex (exportFormatBox.getSelectedItemIndex());

        exportRangeBox.setSelectedItemIndex (rangeIndexFor (updated.range), juce::dontSendNotification);
        exportCyclesEditor.setText (juce::String (updated.cycles), false);
        exportCustomEditor.setText (juce::String (updated.customSeconds, 1), false);
        exportTailEditor.setText (juce::String (updated.tailSeconds, 1), false);
        exportFormatBox.setSelectedItemIndex (formatIndexFor (updated.sampleFormat), juce::dontSendNotification);

        if (updated == exportSettings)
            return;

        exportSettings = updated;
        if (onExportSettingsChanged)
            onExportSettingsChanged (exportSettings);
    }

    std::function<void (bool)> onColourblindSafeChanged;
    std::function<void (SuperColliderAudioSettings)> onScAudioSettingsChanged;
    SuperColliderAudioSettings scAudioSettings;
    std::function<void (AudioExportSettings)> onExportSettingsChanged;
    AudioExportSettings exportSettings;
    juce::Label title;
    juce::Label colourSectionTitle;
    juce::ToggleButton colourblindSafeToggle;
    juce::Label scSectionTitle;
    juce::Label scDeviceLabel;
    juce::TextEditor scDeviceEditor;
    juce::Label scSampleRateLabel;
    juce::TextEditor scSampleRateEditor;
    juce::Label scBufferLabel;
    juce::TextEditor scBufferEditor;
    juce::Label scChannelsLabel;
    juce::TextEditor scChannelsEditor;
    juce::Label scNote;
    juce::Label exportSectionTitle;
    juce::Label exportRangeLabel;
    juce::ComboBox exportRangeBox;
    juce::Label exportCyclesLabel;
    juce::TextEditor exportCyclesEditor;
    juce::Label exportCustomLabel;
    juce::TextEditor exportCustomEditor;
    juce::Label exportTailLabel;
    juce::TextEditor exportTailEditor;
    juce::Label exportFormatLabel;
    juce::ComboBox exportFormatBox;
    juce::Label audioSectionTitle;
    juce::Label note;
    juce::AudioDeviceSelectorComponent selector;
};

class SettingsWindow final : public juce::DocumentWindow
{
public:
    SettingsWindow (juce::AudioDeviceManager& manager,
                    bool colourblindSafeEnabled,
                    std::function<void (bool)> colourblindSafeChanged,
                    SuperColliderAudioSettings scAudioSettings,
                    std::function<void (SuperColliderAudioSettings)> scAudioSettingsChanged)
        : DocumentWindow ("Settings", backgroundTop(), DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new AudioSettingsComponent (manager,
                                                     colourblindSafeEnabled,
                                                     std::move (colourblindSafeChanged),
                                                     std::move (scAudioSettings),
                                                     std::move (scAudioSettingsChanged)), true);
        setResizable (false, false);
        centreWithSize (590, 680);
    }

    void closeButtonPressed() override
    {
        setVisible (false);
    }
};

class AudioExportComponent final : public juce::Component
{
public:
    AudioExportComponent (AudioExportSettings settingsToUse,
                          juce::File defaultOutput,
                          std::function<double (const AudioExportSettings&)> durationProvider,
                          std::function<void (AudioExportSettings, juce::File)> exportRequested,
                          std::function<void()> cancelRequested)
        : settings (std::move (settingsToUse)),
          outputFile (std::move (defaultOutput)),
          getDurationSeconds (std::move (durationProvider)),
          onExportRequested (std::move (exportRequested)),
          onCancelRequested (std::move (cancelRequested))
    {
        addAndMakeVisible (title);
        addAndMakeVisible (rangeLabel);
        addAndMakeVisible (rangeBox);
        addAndMakeVisible (cyclesLabel);
        addAndMakeVisible (cyclesEditor);
        addAndMakeVisible (customLabel);
        addAndMakeVisible (customEditor);
        addAndMakeVisible (tailLabel);
        addAndMakeVisible (tailEditor);
        addAndMakeVisible (formatLabel);
        addAndMakeVisible (formatBox);
        addAndMakeVisible (destinationLabel);
        addAndMakeVisible (destinationEditor);
        addAndMakeVisible (browseButton);
        addAndMakeVisible (durationLabel);
        addAndMakeVisible (exportButton);
        addAndMakeVisible (cancelButton);

        title.setText ("Audio Export", juce::dontSendNotification);
        title.setFont (juce::FontOptions (21.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, ink());

        for (auto* label : { &rangeLabel, &cyclesLabel, &customLabel, &tailLabel, &formatLabel, &destinationLabel })
        {
            label->setFont (juce::FontOptions (12.0f, juce::Font::bold));
            label->setColour (juce::Label::textColourId, mutedInk());
            label->setJustificationType (juce::Justification::centredLeft);
        }

        rangeLabel.setText ("Range", juce::dontSendNotification);
        cyclesLabel.setText ("Cycles", juce::dontSendNotification);
        customLabel.setText ("Seconds", juce::dontSendNotification);
        tailLabel.setText ("Tail", juce::dontSendNotification);
        formatLabel.setText ("Format", juce::dontSendNotification);
        destinationLabel.setText ("Destination", juce::dontSendNotification);

        configureEditor (cyclesEditor);
        configureEditor (customEditor);
        configureEditor (tailEditor);
        configureEditor (destinationEditor);
        cyclesEditor.setInputRestrictions (2, "0123456789");
        customEditor.setInputRestrictions (6, "0123456789.");
        tailEditor.setInputRestrictions (5, "0123456789.");
        destinationEditor.setReadOnly (true);

        rangeBox.addItem ("Project cycle", 1);
        rangeBox.addItem ("Selected state", 2);
        rangeBox.addItem ("Custom length", 3);
        formatBox.addItem ("16-bit WAV", 1);
        formatBox.addItem ("24-bit WAV", 2);
        formatBox.addItem ("32-bit float WAV", 3);
        for (auto* box : { &rangeBox, &formatBox })
        {
            box->setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff111318));
            box->setColour (juce::ComboBox::textColourId, ink());
            box->setColour (juce::ComboBox::outlineColourId, hairline());
            box->onChange = [this] { commitFields(); };
        }

        browseButton.setButtonText ("Choose...");
        exportButton.setButtonText ("Export");
        cancelButton.setButtonText ("Cancel");
        exportButton.setColour (juce::TextButton::buttonColourId, accentA().darker (0.25f));

        browseButton.onClick = [this] { chooseDestination(); };
        exportButton.onClick = [this]
        {
            commitFields();
            if (outputFile == juce::File())
                return;

            if (onExportRequested)
                onExportRequested (settings, outputFile);
        };
        cancelButton.onClick = [this]
        {
            if (onCancelRequested)
                onCancelRequested();
        };

        cyclesEditor.onReturnKey = [this] { commitFields(); };
        cyclesEditor.onFocusLost = [this] { commitFields(); };
        customEditor.onReturnKey = [this] { commitFields(); };
        customEditor.onFocusLost = [this] { commitFields(); };
        tailEditor.onReturnKey = [this] { commitFields(); };
        tailEditor.onFocusLost = [this] { commitFields(); };

        refreshFields();
        setSize (520, 282);
    }

    void paint (juce::Graphics& g) override
    {
        juce::ColourGradient bg (backgroundTop(), 0.0f, 0.0f, backgroundBottom(), 0.0f, static_cast<float> (getHeight()), false);
        g.setGradientFill (bg);
        g.fillAll();
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (18, 16);
        title.setBounds (area.removeFromTop (32));
        area.removeFromTop (10);
        auto rangeRow = area.removeFromTop (34);
        rangeLabel.setBounds (rangeRow.removeFromLeft (78).reduced (0, 5));
        rangeBox.setBounds (rangeRow.removeFromLeft (168).reduced (0, 4));
        rangeRow.removeFromLeft (14);
        cyclesLabel.setBounds (rangeRow.removeFromLeft (50).reduced (0, 5));
        cyclesEditor.setBounds (rangeRow.removeFromLeft (48).reduced (0, 4));
        rangeRow.removeFromLeft (14);
        customLabel.setBounds (rangeRow.removeFromLeft (62).reduced (0, 5));
        customEditor.setBounds (rangeRow.removeFromLeft (64).reduced (0, 4));

        auto formatRow = area.removeFromTop (34);
        tailLabel.setBounds (formatRow.removeFromLeft (78).reduced (0, 5));
        tailEditor.setBounds (formatRow.removeFromLeft (64).reduced (0, 4));
        formatRow.removeFromLeft (16);
        formatLabel.setBounds (formatRow.removeFromLeft (58).reduced (0, 5));
        formatBox.setBounds (formatRow.removeFromLeft (164).reduced (0, 4));

        auto destinationRow = area.removeFromTop (36);
        destinationLabel.setBounds (destinationRow.removeFromLeft (78).reduced (0, 5));
        browseButton.setBounds (destinationRow.removeFromRight (92).reduced (3, 4));
        destinationEditor.setBounds (destinationRow.reduced (0, 4));
        durationLabel.setBounds (area.removeFromTop (28));

        auto buttonRow = area.removeFromBottom (42);
        cancelButton.setBounds (buttonRow.removeFromRight (92).reduced (4, 5));
        exportButton.setBounds (buttonRow.removeFromRight (104).reduced (4, 5));
    }

private:
    static int rangeIndexFor (const juce::String& range)
    {
        if (range == "state") return 1;
        if (range == "custom") return 2;
        return 0;
    }

    static juce::String rangeForIndex (int index)
    {
        if (index == 1) return "state";
        if (index == 2) return "custom";
        return "cycle";
    }

    static int formatIndexFor (const juce::String& format)
    {
        if (format == "int24") return 1;
        if (format == "float") return 2;
        return 0;
    }

    static juce::String formatForIndex (int index)
    {
        if (index == 1) return "int24";
        if (index == 2) return "float";
        return "int16";
    }

    static void configureEditor (juce::TextEditor& editor)
    {
        editor.setMultiLine (false);
        editor.setFont (juce::FontOptions (12.5f));
        editor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff111318));
        editor.setColour (juce::TextEditor::textColourId, ink());
        editor.setColour (juce::TextEditor::outlineColourId, hairline());
        editor.setColour (juce::TextEditor::focusedOutlineColourId, accentA());
    }

    void refreshFields()
    {
        rangeBox.setSelectedItemIndex (rangeIndexFor (settings.range), juce::dontSendNotification);
        cyclesEditor.setText (juce::String (settings.cycles), false);
        customEditor.setText (juce::String (settings.customSeconds, 1), false);
        tailEditor.setText (juce::String (settings.tailSeconds, 1), false);
        formatBox.setSelectedItemIndex (formatIndexFor (settings.sampleFormat), juce::dontSendNotification);
        destinationEditor.setText (outputFile.getFullPathName(), false);
        refreshDuration();
    }

    void refreshDuration()
    {
        const auto seconds = getDurationSeconds ? getDurationSeconds (settings) : 0.0;
        durationLabel.setText ("Estimated length: " + juce::String (seconds, 1) + "s", juce::dontSendNotification);
        durationLabel.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        durationLabel.setColour (juce::Label::textColourId, mutedInk());
    }

    void commitFields()
    {
        settings.range = rangeForIndex (rangeBox.getSelectedItemIndex());
        settings.cycles = juce::jlimit (1, 16, cyclesEditor.getText().getIntValue() <= 0 ? 1 : cyclesEditor.getText().getIntValue());
        settings.customSeconds = juce::jlimit (1.0, 1800.0, customEditor.getText().getDoubleValue() <= 0.0 ? 30.0 : customEditor.getText().getDoubleValue());
        settings.tailSeconds = juce::jlimit (0.0, 60.0, tailEditor.getText().getDoubleValue());
        settings.sampleFormat = formatForIndex (formatBox.getSelectedItemIndex());
        refreshFields();
    }

    void chooseDestination()
    {
        chooser = std::make_unique<juce::FileChooser> ("Export of Audio", outputFile, "*.wav");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode
                                  | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safeThis = juce::Component::SafePointer<AudioExportComponent> (this)] (const juce::FileChooser& fileChooser)
                              {
                                  if (safeThis == nullptr)
                                      return;

                                  auto file = fileChooser.getResult();
                                  if (file == juce::File())
                                      return;

                                  if (file.getFileExtension().isEmpty())
                                      file = file.withFileExtension (".wav");

                                  safeThis->outputFile = file;
                                  safeThis->refreshFields();
                              });
    }

    AudioExportSettings settings;
    juce::File outputFile;
    std::function<double (const AudioExportSettings&)> getDurationSeconds;
    std::function<void (AudioExportSettings, juce::File)> onExportRequested;
    std::function<void()> onCancelRequested;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Label title;
    juce::Label rangeLabel;
    juce::ComboBox rangeBox;
    juce::Label cyclesLabel;
    juce::TextEditor cyclesEditor;
    juce::Label customLabel;
    juce::TextEditor customEditor;
    juce::Label tailLabel;
    juce::TextEditor tailEditor;
    juce::Label formatLabel;
    juce::ComboBox formatBox;
    juce::Label destinationLabel;
    juce::TextEditor destinationEditor;
    juce::TextButton browseButton;
    juce::Label durationLabel;
    juce::TextButton exportButton;
    juce::TextButton cancelButton;
};

class AudioExportWindow final : public juce::DocumentWindow
{
public:
    AudioExportWindow (AudioExportSettings settings,
                       juce::File defaultOutput,
                       std::function<double (const AudioExportSettings&)> durationProvider,
                       std::function<void (AudioExportSettings, juce::File)> exportRequested)
        : DocumentWindow ("Audio Export", backgroundTop(), DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new AudioExportComponent (std::move (settings),
                                                   std::move (defaultOutput),
                                                   std::move (durationProvider),
                                                   std::move (exportRequested),
                                                   [this] { setVisible (false); }), true);
        setResizable (false, false);
        centreWithSize (520, 282);
    }

    void closeButtonPressed() override
    {
        setVisible (false);
    }
};

class WelcomePanel final : public juce::Component
{
public:
    WelcomePanel()
    {
        addAndMakeVisible (title);
        addAndMakeVisible (subtitle);
        addAndMakeVisible (newProjectButton);
        addAndMakeVisible (openProjectButton);
        addAndMakeVisible (openDemoButton);
        addAndMakeVisible (settingsButton);
        addAndMakeVisible (dismissButton);
        addAndMakeVisible (recentTitle);

        title.setText ("of::", juce::dontSendNotification);
        title.setFont (juce::FontOptions (30.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, ink());
        title.setJustificationType (juce::Justification::centredLeft);

        subtitle.setText ("Start a machine, open a project, or load a demo.", juce::dontSendNotification);
        subtitle.setFont (juce::FontOptions (13.5f));
        subtitle.setColour (juce::Label::textColourId, mutedInk());
        subtitle.setJustificationType (juce::Justification::centredLeft);

        recentTitle.setText ("Recent projects", juce::dontSendNotification);
        recentTitle.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        recentTitle.setColour (juce::Label::textColourId, mutedInk());
        recentTitle.setJustificationType (juce::Justification::centredLeft);

        newProjectButton.setButtonText ("New Project");
        openProjectButton.setButtonText ("Open Project");
        openDemoButton.setButtonText ("Open Demo");
        settingsButton.setButtonText ("Audio Settings");
        dismissButton.setButtonText ("Close");

        newProjectButton.onClick = [this] { if (onNewProject) onNewProject(); };
        openProjectButton.onClick = [this] { if (onOpenProject) onOpenProject(); };
        openDemoButton.onClick = [this] { if (onOpenDemo) onOpenDemo(); };
        settingsButton.onClick = [this] { if (onSettings) onSettings(); };
        dismissButton.onClick = [this] { if (onDismiss) onDismiss(); };

        for (int i = 0; i < static_cast<int> (recentButtons.size()); ++i)
        {
            auto& button = recentButtons[static_cast<size_t> (i)];
            addAndMakeVisible (button);
            button.onClick = [this, i] { if (onRecentProject) onRecentProject (i); };
        }
    }

    void setRecentProjects (const juce::StringArray& paths)
    {
        recentPaths = paths;
        const auto count = juce::jmin (static_cast<int> (recentButtons.size()), recentPaths.size());
        for (int i = 0; i < static_cast<int> (recentButtons.size()); ++i)
        {
            auto& button = recentButtons[static_cast<size_t> (i)];
            button.setVisible (i < count);
            if (i < count)
                button.setButtonText (juce::File (recentPaths[i]).getFileName());
        }

        recentTitle.setVisible (count > 0);
        resized();
    }

    juce::String recentProjectPath (int index) const
    {
        return juce::isPositiveAndBelow (index, recentPaths.size()) ? recentPaths[index] : juce::String();
    }

    std::function<void()> onNewProject;
    std::function<void()> onOpenProject;
    std::function<void()> onOpenDemo;
    std::function<void()> onSettings;
    std::function<void()> onDismiss;
    std::function<void(int)> onRecentProject;

private:
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black.withAlpha (0.50f));

        auto panel = panelBounds().toFloat();
        juce::ColourGradient bg (panelFill().brighter (0.04f), panel.getTopLeft(),
                                 backgroundBottom().brighter (0.05f), panel.getBottomRight(), false);
        g.setGradientFill (bg);
        g.fillRoundedRectangle (panel, 8.0f);

        g.setColour (hairline().withAlpha (0.42f));
        g.drawRoundedRectangle (panel, 8.0f, 1.0f);

        auto strip = panel.removeFromTop (3.0f).reduced (1.0f, 0.0f);
        juce::ColourGradient rainbow (graphColour (0).withAlpha (0.85f), strip.getTopLeft(),
                                      graphColour (5).withAlpha (0.85f), strip.getTopRight(), false);
        rainbow.addColour (0.25, graphColour (2).withAlpha (0.85f));
        rainbow.addColour (0.50, graphColour (4).withAlpha (0.85f));
        rainbow.addColour (0.75, graphColour (7).withAlpha (0.85f));
        g.setGradientFill (rainbow);
        g.fillRect (strip);
    }

    void resized() override
    {
        auto panel = panelBounds().reduced (26, 24);
        auto heading = panel.removeFromTop (64);
        title.setBounds (heading.removeFromTop (34));
        subtitle.setBounds (heading.removeFromTop (24));

        panel.removeFromTop (10);
        auto actions = panel.removeFromTop (84);
        auto topActions = actions.removeFromTop (40);
        newProjectButton.setBounds (topActions.removeFromLeft (150).reduced (0, 2));
        topActions.removeFromLeft (8);
        openProjectButton.setBounds (topActions.removeFromLeft (150).reduced (0, 2));
        topActions.removeFromLeft (8);
        openDemoButton.setBounds (topActions.removeFromLeft (126).reduced (0, 2));

        auto secondActions = actions.removeFromTop (40);
        settingsButton.setBounds (secondActions.removeFromLeft (150).reduced (0, 2));
        secondActions.removeFromLeft (8);
        dismissButton.setBounds (secondActions.removeFromLeft (126).reduced (0, 2));

        panel.removeFromTop (12);
        recentTitle.setBounds (panel.removeFromTop (24));
        for (auto& button : recentButtons)
            button.setBounds (panel.removeFromTop (34).reduced (0, 3));
    }

    juce::Rectangle<int> panelBounds() const
    {
        auto bounds = getLocalBounds();
        const auto width = juce::jmin (560, bounds.getWidth() - 48);
        const auto height = juce::jmin (370, bounds.getHeight() - 48);
        return juce::Rectangle<int> (width, height).withCentre (bounds.getCentre());
    }

    juce::Label title;
    juce::Label subtitle;
    juce::Label recentTitle;
    juce::TextButton newProjectButton;
    juce::TextButton openProjectButton;
    juce::TextButton openDemoButton;
    juce::TextButton settingsButton;
    juce::TextButton dismissButton;
    std::array<juce::TextButton, 4> recentButtons;
    juce::StringArray recentPaths;
};

class MainComponent final : public juce::Component,
                            private juce::CodeDocument::Listener,
                            private juce::OSCReceiver,
                            private juce::OSCReceiver::ListenerWithOSCAddress<juce::OSCReceiver::MessageLoopCallback>,
                            private juce::Timer
{
public:
    MainComponent() : machine ("root", "", false), graph (machine), rules (machine), scriptEditor (codeDocument, &scTokeniser)
    {
        setLookAndFeel (&ofLookAndFeel);
        setSize (1180, 760);
        audioDeviceManager.initialise (0, 2, nullptr, true);
        audioDeviceManager.addAudioCallback (&renderedAudioPlayer);

        addAndMakeVisible (title);
        addAndMakeVisible (projectFileLabel);
        addAndMakeVisible (statusLabel);
        addAndMakeVisible (loadProjectButton);
        addAndMakeVisible (saveProjectButton);
        addAndMakeVisible (undoButton);
        addAndMakeVisible (redoButton);
        addAndMakeVisible (logButton);
        addAndMakeVisible (panicButton);
        addAndMakeVisible (topStateCountLabel);
        addAndMakeVisible (topStateCountMinus);
        addAndMakeVisible (topStateCountEditor);
        addAndMakeVisible (topStateCountPlus);
        addAndMakeVisible (masterGainLabel);
        addAndMakeVisible (masterGainSlider);
        addAndMakeVisible (runButton);
        addAndMakeVisible (stepButton);
        addAndMakeVisible (stopAllButton);
        addAndMakeVisible (rateSlider);
        addChildComponent (graph);
        addAndMakeVisible (orbitCanvas);
        addAndMakeVisible (graphFitButton);
        addAndMakeVisible (graphLayoutButton);
        addAndMakeVisible (arrangementViewButton);
        addAndMakeVisible (rules);
        addAndMakeVisible (graphBottomDivider);
        addAndMakeVisible (rulesTracksDivider);
        addAndMakeVisible (tracksCodeDivider);
        addAndMakeVisible (rightInspectorDivider);
        addAndMakeVisible (stateTabs);
        addAndMakeVisible (arrangementStrip);
        addAndMakeVisible (navigator);
        addAndMakeVisible (stateInfoTitle);
        addAndMakeVisible (nestedSectionTitle);
        addAndMakeVisible (trackSectionTitle);
        addAndMakeVisible (breadcrumbLabel);
        addAndMakeVisible (stateSummaryLabel);
        addAndMakeVisible (stateTempoLabel);
        addAndMakeVisible (stateTempoEditor);
        addAndMakeVisible (stateMeterLabel);
        addAndMakeVisible (stateMeterBeatsEditor);
        addAndMakeVisible (stateMeterSlashLabel);
        addAndMakeVisible (stateMeterUnitEditor);
        addAndMakeVisible (stateDurationModeButton);
        addAndMakeVisible (stateDurationBarsEditor);
        addAndMakeVisible (stateDurationBeatsEditor);
        addAndMakeVisible (stateDurationSecondsEditor);
        addAndMakeVisible (nestedTimingLabel);
        addAndMakeVisible (nestedModeBox);
        addAndMakeVisible (nestedDivisionLabel);
        addAndMakeVisible (nestedDivisionMinus);
        addAndMakeVisible (nestedDivisionEditor);
        addAndMakeVisible (nestedDivisionPlus);
        addAndMakeVisible (tracksModeButton);
        addAndMakeVisible (mixerModeButton);
        addAndMakeVisible (trackPaneTitle);
        addAndMakeVisible (trackNameEditor);
        addAndMakeVisible (shapeEditButton);
        addAndMakeVisible (resetShapeButton);
        addAndMakeVisible (renderAllButton);
        addAndMakeVisible (freezeStatusLabel);
        addAndMakeVisible (refreezeLaneButton);
        addAndMakeVisible (refreezeStaleButton);
        addAndMakeVisible (trackList);
        addAndMakeVisible (mixer);
        addAndMakeVisible (codePaneTitle);
        addAndMakeVisible (codeStatsLabel);
        addAndMakeVisible (codeCheckLabel);
        addAndMakeVisible (checkCodeButton);
        addAndMakeVisible (codeFontSizeEditor);
        addAndMakeVisible (tidyCodeButton);
        addAndMakeVisible (expandCodeButton);
        addAndMakeVisible (scriptEditor);
        addAndMakeVisible (addLaneButton);
        addAndMakeVisible (removeLaneButton);
        addAndMakeVisible (duplicateLaneButton);
        addAndMakeVisible (moveLaneUpButton);
        addAndMakeVisible (moveLaneDownButton);
        addAndMakeVisible (addChildMachineButton);
        addAndMakeVisible (removeChildMachineButton);
        addAndMakeVisible (playButton);
        addChildComponent (welcomePanel);

        title.setText ("of::", juce::dontSendNotification);
        title.setFont (juce::FontOptions (24.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, ink());

        projectFileLabel.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        projectFileLabel.setColour (juce::Label::textColourId, mutedInk().withAlpha (0.92f));
        projectFileLabel.setJustificationType (juce::Justification::centredLeft);

        breadcrumbLabel.setFont (juce::FontOptions (12.0f));
        breadcrumbLabel.setColour (juce::Label::textColourId, mutedInk());
        breadcrumbLabel.setJustificationType (juce::Justification::centredLeft);

        stateSummaryLabel.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        stateSummaryLabel.setColour (juce::Label::textColourId, ink());
        stateSummaryLabel.setJustificationType (juce::Justification::centredLeft);
        freezeStatusLabel.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        freezeStatusLabel.setColour (juce::Label::textColourId, mutedInk());
        freezeStatusLabel.setJustificationType (juce::Justification::centredLeft);

        stateInfoTitle.setText ("State", juce::dontSendNotification);
        nestedSectionTitle.setText ("Nested FSM", juce::dontSendNotification);
        trackSectionTitle.setText ("Tracks", juce::dontSendNotification);
        for (auto* sectionTitle : { &stateInfoTitle, &nestedSectionTitle, &trackSectionTitle })
        {
            sectionTitle->setFont (juce::FontOptions (11.5f, juce::Font::bold));
            sectionTitle->setColour (juce::Label::textColourId, mutedInk().withAlpha (0.82f));
            sectionTitle->setJustificationType (juce::Justification::centredLeft);
        }

        navigator.onStateChosen = [this] (MachineModel* targetMachine, int stateIndex)
        {
            navigateToMachineState (targetMachine, stateIndex);
        };

        stateTempoLabel.setText ("Tempo", juce::dontSendNotification);
        stateTempoLabel.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        stateTempoLabel.setColour (juce::Label::textColourId, mutedInk());
        stateMeterLabel.setText ("Meter", juce::dontSendNotification);
        stateMeterLabel.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        stateMeterLabel.setColour (juce::Label::textColourId, mutedInk());
        stateMeterSlashLabel.setText ("/", juce::dontSendNotification);
        stateMeterSlashLabel.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        stateMeterSlashLabel.setColour (juce::Label::textColourId, mutedInk());
        stateMeterSlashLabel.setJustificationType (juce::Justification::centred);

        configureSmallNumberEditor (stateTempoEditor, 6, "0123456789.");
        configureSmallNumberEditor (stateMeterBeatsEditor, 2, "0123456789");
        configureSmallNumberEditor (stateMeterUnitEditor, 2, "0123456789");
        configureSmallNumberEditor (stateDurationBarsEditor, 2, "0123456789");
        configureSmallNumberEditor (stateDurationBeatsEditor, 3, "0123456789");
        configureSmallNumberEditor (stateDurationSecondsEditor, 6, "0123456789.");
        stateDurationModeButton.setButtonText ("Bars");
        stateDurationModeButton.onClick = [this]
        {
            auto& inspected = currentInspectorMachine();
            auto& state = inspected.state (inspected.selectedState);
            state.durationUsesSeconds = ! state.durationUsesSeconds;
            commitStateTimingEditors();
            markMachineDirty();
            refreshControls();
        };
        stateTempoEditor.onReturnKey = [this] { commitStateTimingEditors(); };
        stateTempoEditor.onFocusLost = [this] { commitStateTimingEditors(); };
        stateMeterBeatsEditor.onReturnKey = [this] { commitStateTimingEditors(); };
        stateMeterBeatsEditor.onFocusLost = [this] { commitStateTimingEditors(); };
        stateMeterUnitEditor.onReturnKey = [this] { commitStateTimingEditors(); };
        stateMeterUnitEditor.onFocusLost = [this] { commitStateTimingEditors(); };
        stateDurationBarsEditor.onReturnKey = [this] { commitStateTimingEditors(); };
        stateDurationBarsEditor.onFocusLost = [this] { commitStateTimingEditors(); };
        stateDurationBeatsEditor.onReturnKey = [this] { commitStateTimingEditors(); };
        stateDurationBeatsEditor.onFocusLost = [this] { commitStateTimingEditors(); };
        stateDurationSecondsEditor.onReturnKey = [this] { commitStateTimingEditors(); };
        stateDurationSecondsEditor.onFocusLost = [this] { commitStateTimingEditors(); };

        nestedTimingLabel.setText ("Nested timing", juce::dontSendNotification);
        nestedTimingLabel.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        nestedTimingLabel.setColour (juce::Label::textColourId, mutedInk());

        nestedModeBox.addItem (nestedTimingModeName (NestedTimingMode::followParent), 1);
        nestedModeBox.addItem (nestedTimingModeName (NestedTimingMode::freeRun), 2);
        nestedModeBox.addItem (nestedTimingModeName (NestedTimingMode::oneShot), 3);
        nestedModeBox.addItem (nestedTimingModeName (NestedTimingMode::latch), 4);
        nestedModeBox.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff252a31));
        nestedModeBox.setColour (juce::ComboBox::textColourId, ink());
        nestedModeBox.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff59636e));
        nestedModeBox.onChange = [this]
        {
            if (auto* child = currentInspectorMachine().childMachine (currentInspectorMachine().selectedState))
            {
                child->timingMode = static_cast<NestedTimingMode> (juce::jlimit (0, 3, nestedModeBox.getSelectedItemIndex()));
                child->oneShotComplete = false;
                markMachineDirty();
                refreshControls();
            }
        };

        nestedDivisionLabel.setText ("Division", juce::dontSendNotification);
        nestedDivisionLabel.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        nestedDivisionLabel.setColour (juce::Label::textColourId, mutedInk());
        nestedDivisionMinus.setButtonText ("-");
        nestedDivisionPlus.setButtonText ("+");
        nestedDivisionEditor.setInputRestrictions (2, "0123456789");
        nestedDivisionEditor.setJustification (juce::Justification::centred);
        nestedDivisionEditor.setMultiLine (false);
        nestedDivisionEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff111318));
        nestedDivisionEditor.setColour (juce::TextEditor::textColourId, ink());
        nestedDivisionEditor.setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff34414a));
        nestedDivisionEditor.onReturnKey = [this] { commitNestedDivisionEditor(); };
        nestedDivisionEditor.onFocusLost = [this] { commitNestedDivisionEditor(); };
        nestedDivisionMinus.onClick = [this] { adjustNestedDivision (-1); };
        nestedDivisionPlus.onClick = [this] { adjustNestedDivision (1); };

        codePaneTitle.setText ("SC Code", juce::dontSendNotification);
        codePaneTitle.setFont (juce::FontOptions (13.5f, juce::Font::bold));
        codePaneTitle.setColour (juce::Label::textColourId, ink());
        codePaneTitle.setJustificationType (juce::Justification::centredLeft);
        codeStatsLabel.setFont (juce::FontOptions (11.5f));
        codeStatsLabel.setColour (juce::Label::textColourId, mutedInk());
        codeStatsLabel.setJustificationType (juce::Justification::centredRight);
        codeCheckLabel.setText ("Not checked", juce::dontSendNotification);
        codeCheckLabel.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        codeCheckLabel.setColour (juce::Label::textColourId, mutedInk());
        codeCheckLabel.setJustificationType (juce::Justification::centredLeft);

        trackPaneTitle.setText ("Tracks", juce::dontSendNotification);
        trackPaneTitle.setFont (juce::FontOptions (13.5f, juce::Font::bold));
        trackPaneTitle.setColour (juce::Label::textColourId, ink());
        trackPaneTitle.setJustificationType (juce::Justification::centredLeft);

        tracksModeButton.setButtonText ("Tracks");
        mixerModeButton.setButtonText ("Mixer");
        tracksModeButton.onClick = [this] { setInspectorMode (InspectorMode::tracks); };
        mixerModeButton.onClick = [this] { setInspectorMode (InspectorMode::mixer); };

        trackNameEditor.setMultiLine (false);
        trackNameEditor.setFont (juce::FontOptions (13.0f));
        trackNameEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff111318));
        trackNameEditor.setColour (juce::TextEditor::textColourId, ink());
        trackNameEditor.setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff34414a));
        trackNameEditor.onTextChange = [this]
        {
            currentInspectorMachine().selectedLaneRef().name = trackNameEditor.getText().trim();
            markMachineDirty (UndoGroup::text);
            trackList.repaint();
        };

        statusLabel.setText ("Audio offline", juce::dontSendNotification);
        statusLabel.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        statusLabel.setColour (juce::Label::textColourId, mutedInk());
        statusLabel.setJustificationType (juce::Justification::centredLeft);
        statusLabel.setMouseCursor (juce::MouseCursor::PointingHandCursor);
        statusLabel.onClick = [this]
        {
            startPrepareJob (false);
        };

        masterGainLabel.setText ("Vol", juce::dontSendNotification);
        masterGainLabel.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        masterGainLabel.setColour (juce::Label::textColourId, mutedInk());
        masterGainLabel.setJustificationType (juce::Justification::centredLeft);
        masterGainSlider.setRange (0.0, 5.0, 0.01);
        masterGainSlider.setValue (1.0, juce::dontSendNotification);
        masterGainSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 22);
        masterGainSlider.setTextValueSuffix ("x");
        masterGainSlider.onValueChange = [this]
        {
            const auto gain = static_cast<float> (masterGainSlider.getValue());
            host.setMasterGain (gain);
            renderedAudioPlayer.setMasterGain (gain);
        };

        rulesTracksDivider.onDragged = [this] (int delta)
        {
            rulesPaneWidth = juce::jlimit (300, 860, dividerDragStartRulesWidth + delta);
            resized();
        };
        rulesTracksDivider.onDragStarted = [this]
        {
            rulesPaneUserSized = true;
            dividerDragStartRulesWidth = rulesPaneWidth;
        };

        tracksCodeDivider.onDragged = [this] (int delta)
        {
            tracksPaneWidth = juce::jlimit (260, 460, dividerDragStartTracksWidth - delta);
            resized();
        };
        tracksCodeDivider.onDragStarted = [this]
        {
            tracksPaneUserSized = true;
            dividerDragStartTracksWidth = tracksPaneWidth;
        };

        rightInspectorDivider.onDragged = [this] (int delta)
        {
            rightStatePaneHeight = dividerDragStartRightStateHeight + delta;
            resized();
        };
        rightInspectorDivider.onDragStarted = [this]
        {
            rightInspectorUserSized = true;
            dividerDragStartRightStateHeight = rightStatePaneHeight;
        };

        graphBottomDivider.onDragged = [this] (int delta)
        {
            bottomPaneHeight = dividerDragStartBottomHeight - delta;
            resized();
        };
        graphBottomDivider.onDragStarted = [this]
        {
            bottomPaneUserSized = true;
            dividerDragStartBottomHeight = bottomPaneHeight;
            graph.beginNodePositionLock();
        };
        graphBottomDivider.onDragEnded = [this]
        {
            graph.endNodePositionLock();
        };

        logButton.setButtonText ("Log");
        loadProjectButton.setButtonText ("Load");
        saveProjectButton.setButtonText ("Save");
        undoButton.setButtonText ("Undo");
        redoButton.setButtonText ("Redo");
        panicButton.setButtonText ("Panic");
        panicButton.setColour (juce::TextButton::buttonColourId, accentC().darker (0.45f));

        logView.setMultiLine (true);
        logView.setReadOnly (true);
        logView.setScrollbarsShown (true);
        logView.setCaretVisible (false);
        logView.setFont (juce::FontOptions (12.5f));
        logView.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xee111318));
        logView.setColour (juce::TextEditor::textColourId, mutedInk());
        logView.setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff333a44));
        addChildComponent (logView);

        host.onStatusChanged = [this] (const juce::String& status)
        {
            statusLabel.setText (status, juce::dontSendNotification);
        };

        host.onLogMessage = [this] (const juce::String& message)
        {
            handleHostLogMessage (message);
            appendLog (message);
        };

        mixer.meterProvider = [this] (const juce::String& laneId)
        {
            return meterForLane (laneId);
        };

        if (connect (57142))
        {
            addListener (this, "/of/state");
            addListener (this, "/of/meter");
            addListener (this, "/of/pulse");
            addListener (this, "/of/scheduled");
            addListener (this, "/of/frozen");
            addListener (this, "/of/exported");
            addListener (this, "/of/exportProgress");
        }
        else
            appendLog ("Could not bind visual state OSC port 57142");

        topStateCountLabel.setText ("States", juce::dontSendNotification);
        topStateCountLabel.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        topStateCountLabel.setColour (juce::Label::textColourId, mutedInk());
        topStateCountLabel.setJustificationType (juce::Justification::centredLeft);

        topStateCountMinus.setButtonText ("-");
        topStateCountPlus.setButtonText ("+");
        topStateCountMinus.onClick = [this] { setTopLevelStateCount (machine.getStateCount() - 1); };
        topStateCountPlus.onClick = [this] { setTopLevelStateCount (machine.getStateCount() + 1); };

        topStateCountEditor.setText (juce::String (machine.getStateCount()), false);
        topStateCountEditor.setInputRestrictions (2, "0123456789");
        topStateCountEditor.setJustification (juce::Justification::centred);
        topStateCountEditor.setSelectAllWhenFocused (true);
        topStateCountEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff181b20));
        topStateCountEditor.setColour (juce::TextEditor::textColourId, ink());
        topStateCountEditor.setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff333a44));
        topStateCountEditor.setColour (juce::TextEditor::focusedOutlineColourId, accentA());
        topStateCountEditor.onReturnKey = [this] { commitTopLevelStateCountEditor(); };
        topStateCountEditor.onFocusLost = [this] { commitTopLevelStateCountEditor(); };

        runButton.setButtonText ("Run");
        stepButton.setButtonText ("Step");
        stopAllButton.setButtonText ("Stop");
        rateSlider.setRange (0.2, 4.0, 0.1);
        rateSlider.setValue (0.25);
        rateSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 22);

        graphFitButton.setButtonText ("Fit");
        graphLayoutButton.setButtonText ("Layout");
        arrangementViewButton.setButtonText ("Arrange");
        graphFitButton.onClick = [this] { graph.fitView(); };
        graphLayoutButton.onClick = [this] { graph.resetLayout(); };
        arrangementViewButton.onClick = [this]
        {
            arrangementViewMode = (arrangementViewMode + 1) % 3;
            saveAppState();
            resized();
            refreshControls();
        };

        runButton.onClick = [this]
        {
            if (! fsmRunning)
            {
                fsmRunning = true;
                if (machinePrepared && ! audioJobRunning)
                    startPreparedRun();
                else
                    startPrepareJob (true);
            }
            else
            {
                fsmRunning = false;
                ++playbackGeneration;
                stopTransport();
                renderedAudioPlayer.stopAll();
                host.pauseMachine();
                host.stopAll (machine);
                graph.clearTimingPulse();
                arrangementStrip.clearTimingPulse();
                orbitCanvas.clearReversePlayheads();
                orbitConnectionTransportStartMs = 0.0;
                previousOrbitConnectionPhases.clear();
                visualNextStateMs = 0.0;
                scheduledTransitionTargetMs = 0.0;
                scheduledVisualNextState = -1;
                runButton.setButtonText ("Run");
            }
        };

        stepButton.onClick = [this]
        {
            host.stepMachine();
        };

        stopAllButton.onClick = [this]
        {
            fsmRunning = false;
            ++playbackGeneration;
            stopTransport();
            renderedAudioPlayer.stopAll();
            runButton.setButtonText ("Run");
            host.pauseMachine();
            host.stopAll (machine);
            graph.clearTimingPulse();
            arrangementStrip.clearTimingPulse();
            orbitCanvas.clearReversePlayheads();
            orbitConnectionTransportStartMs = 0.0;
            previousOrbitConnectionPhases.clear();
            visualNextStateMs = 0.0;
            scheduledTransitionTargetMs = 0.0;
            scheduledVisualNextState = -1;
            refreshControls();
        };

        panicButton.onClick = [this]
        {
            fsmRunning = false;
            stopTransport();
            renderedAudioPlayer.stopAll();
            host.pauseMachine();
            runButton.setButtonText ("Run");
            host.panic (machine);
            graph.clearTimingPulse();
            arrangementStrip.clearTimingPulse();
            orbitCanvas.clearReversePlayheads();
            visualNextStateMs = 0.0;
            scheduledTransitionTargetMs = 0.0;
            scheduledVisualNextState = -1;
            refreshControls();
        };

        logButton.onClick = [this]
        {
            logVisible = ! logVisible;
            logView.setVisible (logVisible);
            flushLogViewIfNeeded (juce::Time::getMillisecondCounterHiRes(), true);
            preserveGraphNodePositionsDuringLayout();
        };

        loadProjectButton.onClick = [this]
        {
            chooseProjectToLoad();
        };

        saveProjectButton.onClick = [this]
        {
            saveCurrentProject();
        };

        undoButton.onClick = [this]
        {
            undoProjectEdit();
        };

        redoButton.onClick = [this]
        {
            redoProjectEdit();
        };

        rateSlider.onValueChange = [this]
        {
            transportIntervalMs = getTransportIntervalMs();
            if (fsmRunning)
            {
                restartTransport();
            }
        };

        stateTabs.onIndexSelected = [this] (int newIndex)
        {
            currentMachine().selectedState = newIndex;
            currentMachine().selectedLane = 0;
            inspectedMachine = &currentMachine();
            refreshControls();
        };

        arrangementStrip.onStateSelected = [this] (int newIndex)
        {
            machine.selectedState = juce::jlimit (0, machine.getStateCount() - 1, newIndex);
            machine.selectedLane = 0;
            inspectedMachine = &machine;
            refreshControls();
        };

        arrangementStrip.onNestedStateSelected = [this] (int parentStateIndex, int childStateIndex)
        {
            machine.selectedState = juce::jlimit (0, machine.getStateCount() - 1, parentStateIndex);
            if (auto* child = machine.childMachine (machine.selectedState))
            {
                child->selectedState = juce::jlimit (0, child->getStateCount() - 1, childStateIndex);
                child->selectedLane = 0;
                inspectedMachine = child;
            }
            else
            {
                inspectedMachine = &machine;
            }
            refreshControls();
        };

        arrangementStrip.onLaneSelected = [this] (int stateIndex, int laneIndex)
        {
            clearSelectedRenderedWaveform();
            machine.selectedState = juce::jlimit (0, machine.getStateCount() - 1, stateIndex);
            machine.selectedLane = juce::jlimit (0, machine.getLaneCount (machine.selectedState) - 1, laneIndex);
            inspectedMachine = &machine;
            refreshControls();
        };

        arrangementStrip.onDeleteSelectedLaneRequested = [this]
        {
            deleteSelectedLane();
        };

        arrangementStrip.onStateLengthChanged = [this] (int stateIndex, int bars)
        {
            machine.selectedState = juce::jlimit (0, machine.getStateCount() - 1, stateIndex);
            auto& state = machine.state (machine.selectedState);
            state.durationUsesSeconds = false;
            state.arrangementBars = juce::jlimit (1, 64, bars);
            inspectedMachine = &machine;
            transportIntervalMs = getTransportIntervalMs();
            refreezeRenderedLanesInState (machine, machine.selectedState);
            markMachineDirty (UndoGroup::continuous);
            refreshControls();
        };

        trackList.onTrackSelected = [this] (int newIndex)
        {
            selectInspectorLane (newIndex);
        };

        trackList.onEnabledToggled = [this] (int newIndex)
        {
            toggleInspectorLaneEnabled (newIndex);
        };

        trackList.onMuteToggled = [this] (int newIndex)
        {
            toggleInspectorLaneMute (newIndex);
        };

        trackList.onSoloToggled = [this] (int newIndex)
        {
            toggleInspectorLaneSolo (newIndex);
        };

        trackList.onFreezeToggled = [this] (int newIndex)
        {
            toggleInspectorLaneFreeze (newIndex);
        };

        trackList.onVolumeChanged = [this] (int newIndex, float volume)
        {
            setInspectorLaneVolume (newIndex, volume);
        };

        trackList.onDeleteSelectedLaneRequested = [this]
        {
            deleteSelectedLane();
        };

        mixer.onTrackSelected = [this] (int newIndex)
        {
            selectInspectorLane (newIndex);
        };

        mixer.onEnabledToggled = [this] (int newIndex)
        {
            toggleInspectorLaneEnabled (newIndex);
        };

        mixer.onMuteToggled = [this] (int newIndex)
        {
            toggleInspectorLaneMute (newIndex);
        };

        mixer.onSoloToggled = [this] (int newIndex)
        {
            toggleInspectorLaneSolo (newIndex);
        };

        mixer.onFreezeToggled = [this] (int newIndex)
        {
            toggleInspectorLaneFreeze (newIndex);
        };

        mixer.onVolumeChanged = [this] (int newIndex, float volume)
        {
            setInspectorLaneVolume (newIndex, volume);
        };

        mixer.onGainChanged = [this] (int newIndex, float gain)
        {
            setInspectorLaneGain (newIndex, gain);
        };

        mixer.onPanChanged = [this] (int newIndex, float pan)
        {
            setInspectorLanePan (newIndex, pan);
        };

        mixer.onDeleteSelectedLaneRequested = [this]
        {
            deleteSelectedLane();
        };

        codeDocument.addListener (this);
        scriptEditor.setLineNumbersShown (true);
        scriptEditor.setTabSize (4, true);
        scriptEditor.setScrollbarThickness (9);
        updateCodeEditorFont();
        scriptEditor.setColour (juce::CodeEditorComponent::backgroundColourId, juce::Colour (0xff111419));
        scriptEditor.setColour (juce::CodeEditorComponent::defaultTextColourId, ink());
        scriptEditor.setColour (juce::CodeEditorComponent::highlightColourId, graphColour (1).withAlpha (0.24f));
        scriptEditor.setColour (juce::CodeEditorComponent::lineNumberBackgroundId, juce::Colour (0xff14191f));
        scriptEditor.setColour (juce::CodeEditorComponent::lineNumberTextId, mutedInk().withAlpha (0.52f));
        scriptEditor.setColourScheme (scTokeniser.getDefaultColourScheme());

        tidyCodeButton.setButtonText ("Tidy");
        checkCodeButton.setButtonText ("Check");
        expandCodeButton.setButtonText ("Expand");
        configureSmallNumberEditor (codeFontSizeEditor, 2, "0123456789");
        codeFontSizeEditor.setText (juce::String (juce::roundToInt (codeFontSize)), false);
        codeFontSizeEditor.onReturnKey = [this] { commitCodeFontSizeEditor(); };
        codeFontSizeEditor.onFocusLost = [this] { commitCodeFontSizeEditor(); };
        tidyCodeButton.onClick = [this]
        {
            tidySelectedLaneScript();
        };
        checkCodeButton.onClick = [this]
        {
            checkSelectedLaneScript();
        };
        expandCodeButton.onClick = [this]
        {
            codeExpanded = ! codeExpanded;
            expandCodeButton.setButtonText (codeExpanded ? "Shrink" : "Expand");
            resized();
            scriptEditor.grabKeyboardFocus();
        };

        addLaneButton.setButtonText ("+L");
        removeLaneButton.setButtonText ("-L");
        duplicateLaneButton.setButtonText ("Dup");
        refreezeLaneButton.setButtonText ("Refreeze");
        refreezeStaleButton.setButtonText ("All stale");
        renderAllButton.setButtonText ("Render all");
        shapeEditButton.setButtonText ("Edit shape");
        shapeEditButton.setClickingTogglesState (true);
        resetShapeButton.setButtonText ("Reset shape");
        moveLaneUpButton.setButtonText ("^");
        moveLaneDownButton.setButtonText ("v");
        addChildMachineButton.setButtonText ("+ FSM");
        removeChildMachineButton.setButtonText ("- FSM");
        playButton.setButtonText ("Play");

        addLaneButton.onClick = [this]
        {
            currentInspectorMachine().addLaneToSelectedState();
            markMachineDirty();
            refreshControls();
        };

        removeLaneButton.onClick = [this]
        {
            deleteSelectedLane();
        };

        duplicateLaneButton.onClick = [this]
        {
            duplicateSelectedLane();
        };

        refreezeLaneButton.onClick = [this]
        {
            refreezeSelectedLane();
        };

        refreezeStaleButton.onClick = [this]
        {
            refreezeStaleFrozenLanes();
        };

        renderAllButton.onClick = [this]
        {
            renderAllPlacedLanes();
        };

        shapeEditButton.onClick = [this]
        {
            orbitShapeEditMode = shapeEditButton.getToggleState();
            orbitCanvas.setShapeEditMode (orbitShapeEditMode);
            refreshControls();
        };

        resetShapeButton.onClick = [this]
        {
            auto& inspected = currentInspectorMachine();
            inspected.state (inspected.selectedState).orbitWarp = {};
            markProjectLayoutDirty();
            orbitCanvas.repaint();
            refreshControls();
        };

        moveLaneUpButton.onClick = [this]
        {
            currentInspectorMachine().moveSelectedLane (-1);
            markMachineDirty();
            refreshControls();
        };

        moveLaneDownButton.onClick = [this]
        {
            currentInspectorMachine().moveSelectedLane (1);
            markMachineDirty();
            refreshControls();
        };

        addChildMachineButton.onClick = [this]
        {
            currentInspectorMachine().addChildToSelectedState();
            markMachineDirty();
            refreshControls();
        };

        removeChildMachineButton.onClick = [this]
        {
            auto& inspected = currentInspectorMachine();
            if (auto* child = inspected.childMachine (inspected.selectedState))
            {
                stopMachineRecursive (*child);
                inspected.removeChildFromSelectedState();
                inspectedMachine = &inspected;
                activeMachine = &inspected;
                graph.setMachine (inspected);
                graph.setInspectedMachine (&inspected);
                rules.setMachine (inspected);
                markMachineDirty();
                refreshControls();
            }
        };

        playButton.onClick = [this]
        {
            auto& inspected = currentInspectorMachine();
            auto& state = inspected.state (inspected.selectedState);
            auto& lane = inspected.selectedLaneRef();
            if (lane.playing)
            {
                renderedAudioPlayer.stopLane (lane.id);
                host.stop (lane);
            }
            else if (shouldPlayLane (state, lane))
            {
                primeMeterForLane (lane);
                host.playLive (lane, getSclangPathOverride());
                statusLabel.setText ("Auditioning live code", juce::dontSendNotification);
            }
            else
            {
                renderedAudioPlayer.stopLane (lane.id);
                host.stop (lane);
            }
            refreshControls();
        };

        welcomePanel.onNewProject = [this]
        {
            hideWelcomePanel();
            newProject();
        };
        welcomePanel.onOpenProject = [this]
        {
            chooseProjectToLoad();
        };
        welcomePanel.onOpenDemo = [this]
        {
            openWelcomeDemo();
        };
        welcomePanel.onSettings = [this]
        {
            showSettings();
        };
        welcomePanel.onDismiss = [this]
        {
            hideWelcomePanel();
        };
        welcomePanel.onRecentProject = [this] (int index)
        {
            const auto path = welcomePanel.recentProjectPath (index);
            if (path.isNotEmpty())
                loadProjectFromFile (juce::File (path));
        };

        graph.onStateChosen = [this] (int)
        {
            inspectedMachine = &currentMachine();
            refreshControls();
        };

        graph.onNestedBadgeChosen = [this] (int parentStateIndex)
        {
            if (auto* child = currentMachine().childMachine (parentStateIndex))
            {
                currentMachine().selectedState = parentStateIndex;
                child->selectedLane = 0;
                inspectedMachine = child;
                refreshControls();
            }
        };

        graph.onNestedStateChosen = [this] (int parentStateIndex, int childStateIndex)
        {
            if (auto* child = currentMachine().childMachine (parentStateIndex))
            {
                currentMachine().selectedState = parentStateIndex;
                child->selectedState = childStateIndex;
                child->selectedLane = 0;
                inspectedMachine = child;
                refreshControls();
            }
        };

        graph.onSecondLayerNestedStateChosen = [this] (int parentStateIndex, int childStateIndex, int grandchildStateIndex)
        {
            if (auto* child = currentMachine().childMachine (parentStateIndex))
            {
                currentMachine().selectedState = parentStateIndex;
                child->selectedState = childStateIndex;
                child->selectedLane = 0;

                if (auto* grandchild = child->childMachine (childStateIndex))
                {
                    grandchild->selectedState = grandchildStateIndex;
                    grandchild->selectedLane = 0;
                    inspectedMachine = grandchild;
                }
                else
                {
                    inspectedMachine = child;
                }

                refreshControls();
            }
        };

        graph.onNestedStateCountChanged = [this] (int parentStateIndex, int newCount)
        {
            if (auto* child = currentMachine().childMachine (parentStateIndex))
            {
                fsmRunning = false;
                stopTransport();
                renderedAudioPlayer.stopAll();
                host.stopAll (machine);
                runButton.setButtonText ("Run");

                child->setStateCount (newCount);
                child->regenerateRingRules();
                markMachineDirty();
                refreshControls();
            }
        };

        graph.onSecondLayerNestedStateCountChanged = [this] (int parentStateIndex, int childStateIndex, int newCount)
        {
            if (auto* child = currentMachine().childMachine (parentStateIndex))
            {
                if (auto* grandchild = child->childMachine (childStateIndex))
                {
                    fsmRunning = false;
                    stopTransport();
                    renderedAudioPlayer.stopAll();
                    host.stopAll (machine);
                    runButton.setButtonText ("Run");

                    grandchild->setStateCount (newCount);
                    grandchild->regenerateRingRules();
                    markMachineDirty();
                    refreshControls();
                }
            }
        };

        graph.onNodeLayoutChanged = [this]
        {
            markProjectLayoutDirty();
        };

        orbitCanvas.onTrackSelected = [this] (int stateIndex)
        {
            clearSelectedRenderedWaveform();
            machine.selectedState = juce::jlimit (0, machine.getStateCount() - 1, stateIndex);
            machine.selectedLane = juce::jlimit (0, machine.getLaneCount (machine.selectedState) - 1, machine.selectedLane);
            inspectedMachine = &machine;
            refreshControls();
        };

        orbitCanvas.onLaneSelected = [this] (int stateIndex, int laneIndex)
        {
            clearSelectedRenderedWaveform();
            machine.selectedState = juce::jlimit (0, machine.getStateCount() - 1, stateIndex);
            machine.selectedLane = juce::jlimit (0, machine.getLaneCount (machine.selectedState) - 1, laneIndex);
            inspectedMachine = &machine;
            refreshControls();
        };

        orbitCanvas.onTrackFocusRequested = [this] (int stateIndex)
        {
            clearSelectedRenderedWaveform();
            orbitFocusedTrack = juce::jlimit (0, machine.getStateCount() - 1, stateIndex);
            machine.selectedState = orbitFocusedTrack;
            machine.selectedLane = juce::jlimit (0, machine.getLaneCount (machine.selectedState) - 1, machine.selectedLane);
            inspectedMachine = &machine;
            refreshControls();
        };

        orbitCanvas.onTrackFocusCleared = [this]
        {
            orbitFocusedTrack = -1;
            refreshControls();
        };

        orbitCanvas.onScriptDropRequested = [this] (int stateIndex, float phase)
        {
            chooseSuperColliderFileForOrbit (stateIndex, phase);
        };

        orbitCanvas.onWarpChanged = [this] (int, int, float)
        {
            markMachineDirty (UndoGroup::continuous);
        };

        orbitCanvas.onLanePhaseChanged = [this] (int stateIndex, int laneIndex, float, bool finished)
        {
            const auto safeState = juce::jlimit (0, machine.getStateCount() - 1, stateIndex);
            const auto safeLane = juce::jlimit (0, machine.getLaneCount (safeState) - 1, laneIndex);
            auto& lane = machine.state (safeState).lanes[static_cast<size_t> (safeLane)];
            lane.preparedBridge = -1;

            if (finished)
            {
                if (lane.frozen && ! lane.freezeInProgress)
                {
                    orbitCanvas.invalidateWaveforms();
                    statusLabel.setText ("Moved " + lane.name, juce::dontSendNotification);
                }
                else
                {
                    statusLabel.setText ("Moved " + lane.name, juce::dontSendNotification);
                }

                markMachineDirty();
            }
            else
            {
                markMachineDirty (UndoGroup::continuous);
            }

            refreshControls();
        };

        orbitCanvas.onLaneTrimChanged = [this] (int stateIndex, int laneIndex, float startPhase, float endPhase, bool finished)
        {
            const auto safeState = juce::jlimit (0, machine.getStateCount() - 1, stateIndex);
            const auto safeLane = juce::jlimit (0, machine.getLaneCount (safeState) - 1, laneIndex);
            auto& state = machine.state (safeState);
            auto& lane = state.lanes[static_cast<size_t> (safeLane)];

            const auto trackSeconds = juce::jmax (0.25, state.secondsPerSection());
            const auto safeStart = juce::jlimit (0.0f, 0.997f, startPhase);
            const auto safeEnd = juce::jlimit (safeStart + 0.0025f, 0.9999f, endPhase);
            lane.orbitPhase = safeStart;
            lane.durationMode = LaneDurationMode::fixedSeconds;
            lane.durationValue = juce::jmax (0.05, static_cast<double> (safeEnd - safeStart) * trackSeconds);
            const auto maxFade = lane.durationValue * 0.5;
            lane.fadeInSeconds = juce::jlimit (0.0, maxFade, lane.fadeInSeconds);
            lane.fadeOutSeconds = juce::jlimit (0.0, maxFade, lane.fadeOutSeconds);
            lane.preparedBridge = -1;
            selectedRenderedWaveformState = safeState;
            selectedRenderedWaveformLane = safeLane;
            orbitCanvas.setSelectedRenderedLane (safeState, safeLane);

            if (finished)
            {
                if (lane.frozen && ! lane.freezeInProgress)
                {
                    orbitCanvas.invalidateWaveforms();
                    if (lane.frozenDurationSeconds <= 0.0 || lane.durationValue > lane.frozenDurationSeconds + 0.02)
                    {
                        lane.freezeStale = true;
                        beginFreezeLane (machine, safeState, safeLane);
                        statusLabel.setText ("Extended and re-rendering " + lane.name, juce::dontSendNotification);
                    }
                    else
                    {
                        renderedAudioPlayer.stopLane (lane.id);
                        lane.freezeStale = false;
                        statusLabel.setText ("Trimmed " + lane.name, juce::dontSendNotification);
                    }
                }
                else
                {
                    statusLabel.setText ("Trimmed " + lane.name, juce::dontSendNotification);
                }

                markMachineDirty();
            }
            else
            {
                statusLabel.setText ("Trimming " + lane.name, juce::dontSendNotification);
                markMachineDirty (UndoGroup::continuous);
            }

            refreshControls();
        };

        orbitCanvas.onLaneFadeChanged = [this] (int stateIndex, int laneIndex, double fadeIn, double fadeOut, bool finished)
        {
            const auto safeState = juce::jlimit (0, machine.getStateCount() - 1, stateIndex);
            const auto safeLane = juce::jlimit (0, machine.getLaneCount (safeState) - 1, laneIndex);
            auto& state = machine.state (safeState);
            auto& lane = state.lanes[static_cast<size_t> (safeLane)];
            const auto duration = juce::jmax (0.01, laneRenderDurationSeconds (state, lane));
            const auto maxFade = duration * 0.5;

            lane.fadeInSeconds = juce::jlimit (0.0, maxFade, fadeIn);
            lane.fadeOutSeconds = juce::jlimit (0.0, maxFade, fadeOut);
            selectedRenderedWaveformState = safeState;
            selectedRenderedWaveformLane = safeLane;
            orbitCanvas.setSelectedRenderedLane (safeState, safeLane);
            renderedAudioPlayer.stopLane (lane.id);

            statusLabel.setText ("Fade " + lane.name
                                  + "  in " + juce::String (lane.fadeInSeconds, 2)
                                  + "s / out " + juce::String (lane.fadeOutSeconds, 2) + "s",
                                  juce::dontSendNotification);
            markMachineDirty (finished ? UndoGroup::structural : UndoGroup::continuous);
            refreshControls();
        };

        orbitCanvas.onRenderedLaneSelected = [this] (int stateIndex, int laneIndex)
        {
            if (stateIndex < 0 || stateIndex >= machine.getStateCount())
                return;

            auto& state = machine.state (stateIndex);
            if (laneIndex < 0 || laneIndex >= static_cast<int> (state.lanes.size()))
                return;

            selectedRenderedWaveformState = stateIndex;
            selectedRenderedWaveformLane = laneIndex;
            orbitCanvas.setSelectedRenderedLane (stateIndex, laneIndex);
            statusLabel.setText ("Selected waveform: " + state.lanes[static_cast<size_t> (laneIndex)].name, juce::dontSendNotification);
            orbitCanvas.repaint();
        };

        orbitCanvas.onDeleteRenderedLaneRequested = [this] (int stateIndex, int laneIndex)
        {
            deleteRenderedWaveformAt (stateIndex, laneIndex);
        };

        orbitCanvas.onDeleteSelectedLaneRequested = [this]
        {
            deleteSelectedLane();
        };

        orbitCanvas.onConnectionRequested = [this] (int sourceState, int sourceLane, float sourcePhase, int targetState)
        {
            promptOrbitConnectionAction (sourceState, sourceLane, sourcePhase, targetState);
        };

        rules.onRulesChanged = [this]
        {
            markMachineDirty();
            graph.repaint();
            rules.repaint();
        };

        refreshControls();
        restoreLastProject();
        resetUndoHistory();
        showWelcomePanel();
        setWantsKeyboardFocus (true);
        startTimerHz (30);
        juce::Timer::callAfterDelay (350, [safeThis = juce::Component::SafePointer<MainComponent> (this)]
        {
            if (safeThis != nullptr)
                safeThis->startPrepareJob (false);
        });
    }

    ~MainComponent() override
    {
        exportWindow = nullptr;
        settingsWindow = nullptr;
        audioDeviceManager.removeAudioCallback (&renderedAudioPlayer);
        renderedAudioPlayer.stopAll();
        autosaveIfNeeded (true);
        saveAppState();
        stopTimer();
        codeDocument.removeListener (this);
        removeListener (this);
        disconnect();
        stopTransport();
        stopMachineRecursive (machine);
        host.onLogMessage = nullptr;
        host.onStatusChanged = nullptr;
        setLookAndFeel (nullptr);
    }

    void newProject()
    {
        fsmRunning = false;
        stopTransport();
        renderedAudioPlayer.stopAll();
        requestAudioProjectReset();
        prepareQueued = false;
        prepareQueuedStartAfter = false;
        host.pauseMachine();
        host.panic (machine);
        runButton.setButtonText ("Run");

        machine = MachineModel ("root", "", false);
        machineStack.clear();
        activeMachine = &machine;
        inspectedMachine = &machine;
        currentProjectFile = juce::File();
        laneMeters.clear();
        invalidatePreparedAudio();
        dirtyProject = false;
        cachedProjectMediaStatus = {};
        lastProjectMediaStatus = "New project";
        graph.setMachine (machine);
        graph.setInspectedMachine (&machine);
        rules.setMachine (machine);
        topStateCountEditor.setText (juce::String (machine.getStateCount()), false);
        saveProjectButton.setButtonText ("Save");
        updateProjectFileLabel();
        statusLabel.setText ("New project", juce::dontSendNotification);
        hideWelcomePanel();
        resetUndoHistory();
        refreshControls();
    }

    void loadProject()
    {
        chooseProjectToLoad();
    }

    void showWelcomePanel()
    {
        welcomePanel.setRecentProjects (recentProjects);
        welcomePanel.setBounds (getLocalBounds());
        welcomePanel.setVisible (true);
        welcomePanel.toFront (false);
    }

    void hideWelcomePanel()
    {
        welcomePanel.setVisible (false);
    }

    void openWelcomeDemo()
    {
        auto demo = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                        .getChildFile ("radigue-expanded.markovfsm");
        if (! demo.existsAsFile())
            demo = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                       .getChildFile ("demo1.markovfsm");

        if (demo.existsAsFile() && loadProjectFromFile (demo))
            return;

        hideWelcomePanel();
        newProject();
        statusLabel.setText ("Demo file not found", juce::dontSendNotification);
    }

    void saveCurrentProject()
    {
        if (currentProjectFile.existsAsFile())
        {
            if (saveProjectToFile (currentProjectFile))
                statusLabel.setText (lastProjectMediaStatus, juce::dontSendNotification);
            else
                statusLabel.setText ("Save failed", juce::dontSendNotification);
        }
        else
        {
            saveProjectAs();
        }
    }

    void saveProjectAs()
    {
        chooseProjectToSave();
    }

    void exportAudio()
    {
        exportWindow = std::make_unique<AudioExportWindow> (exportSettings,
                                                            defaultAudioExportFile(),
                                                            [safeThis = juce::Component::SafePointer<MainComponent> (this)] (const AudioExportSettings& settings)
                                                            {
                                                                return safeThis == nullptr ? 0.0 : safeThis->exportDurationSeconds (settings);
                                                            },
                                                            [safeThis = juce::Component::SafePointer<MainComponent> (this)] (AudioExportSettings settings, juce::File file)
                                                            {
                                                                if (safeThis == nullptr)
                                                                    return;

                                                                safeThis->setAudioExportSettings (std::move (settings));
                                                                if (safeThis->exportWindow != nullptr)
                                                                    safeThis->exportWindow->setVisible (false);
                                                                safeThis->beginAudioExport (file);
                                                            });
        exportWindow->setVisible (true);
        exportWindow->toFront (true);
    }

    void cancelAudioExport()
    {
        if (! exportInProgress)
        {
            statusLabel.setText ("No export running", juce::dontSendNotification);
            return;
        }

        exportCancelRequested = true;
        statusLabel.setText ("Cancelling export", juce::dontSendNotification);
        host.cancelExport();
    }

    void showSettings()
    {
        if (settingsWindow == nullptr)
        {
            settingsWindow = std::make_unique<SettingsWindow> (audioDeviceManager,
                                                               colourblindSafeMode,
                                                               [safeThis = juce::Component::SafePointer<MainComponent> (this)] (bool shouldUse)
                                                               {
                                                                   if (safeThis != nullptr)
                                                                       safeThis->setColourblindSafeMode (shouldUse);
                                                               },
                                                               scAudioSettings,
                                                               [safeThis = juce::Component::SafePointer<MainComponent> (this)] (SuperColliderAudioSettings settings)
                                                               {
                                                                   if (safeThis != nullptr)
                                                                       safeThis->setSuperColliderAudioSettings (std::move (settings));
                                                               });
        }

        settingsWindow->setVisible (true);
        settingsWindow->toFront (true);
    }

    void showAbout()
    {
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::InfoIcon,
                                                "of::",
                                                "By matd.space");
    }

    void setColourblindSafeMode (bool shouldUse)
    {
        if (colourblindSafeMode == shouldUse)
            return;

        colourblindSafeMode = shouldUse;
        setColourblindSafePalette (shouldUse);
        saveAppState();
        refreshVisualTheme();
    }

    void setSuperColliderAudioSettings (SuperColliderAudioSettings settings)
    {
        settings.outputDevice = settings.outputDevice.trim();
        settings.sampleRate = settings.sampleRate <= 0.0 ? 0.0 : juce::jlimit (8000.0, 384000.0, settings.sampleRate);
        settings.hardwareBufferSize = juce::jlimit (16, 4096, settings.hardwareBufferSize <= 0 ? 64 : settings.hardwareBufferSize);
        settings.outputChannels = juce::jlimit (1, 64, settings.outputChannels <= 0 ? 2 : settings.outputChannels);

        if (scAudioSettings == settings)
            return;

        fsmRunning = false;
        stopTransport();
        renderedAudioPlayer.stopAll();
        host.pauseMachine();
        host.stopAll (machine);
        requestAudioProjectReset();
        runButton.setButtonText ("Run");
        scAudioSettings = std::move (settings);
        host.setAudioSettings (scAudioSettings);
        invalidatePreparedAudio();
        statusLabel.setText ("SC audio settings changed", juce::dontSendNotification);
        saveAppState();
        refreshControls();
    }

    void setAudioExportSettings (AudioExportSettings settings)
    {
        if (settings.range != "state" && settings.range != "custom")
            settings.range = "cycle";
        settings.cycles = juce::jlimit (1, 16, settings.cycles <= 0 ? 1 : settings.cycles);
        settings.customSeconds = juce::jlimit (1.0, 1800.0, settings.customSeconds <= 0.0 ? 30.0 : settings.customSeconds);
        settings.tailSeconds = juce::jlimit (0.0, 60.0, settings.tailSeconds);
        if (settings.sampleFormat != "int24" && settings.sampleFormat != "float")
            settings.sampleFormat = "int16";

        if (exportSettings == settings)
            return;

        exportSettings = std::move (settings);
        statusLabel.setText ("Export settings changed", juce::dontSendNotification);
        saveAppState();
    }

    void paint (juce::Graphics& g) override
    {
        juce::ColourGradient bg (backgroundTop(), getLocalBounds().getTopLeft().toFloat(),
                                 backgroundBottom(), getLocalBounds().getBottomRight().toFloat(), false);
        bg.addColour (0.72, juce::Colour (0xff161b20));
        g.setGradientFill (bg);
        g.fillAll();

        auto strip = getLocalBounds().removeFromTop (2).toFloat();
        juce::ColourGradient rainbow (graphColour (0).withAlpha (0.98f), strip.getTopLeft(),
                                      graphColour (4).withAlpha (0.98f), strip.getTopRight(), false);
        rainbow.addColour (0.18, graphColour (1).withAlpha (0.98f));
        rainbow.addColour (0.36, graphColour (2).withAlpha (0.98f));
        rainbow.addColour (0.54, graphColour (3).withAlpha (0.98f));
        rainbow.addColour (0.72, graphColour (5).withAlpha (0.98f));
        rainbow.addColour (0.88, graphColour (7).withAlpha (0.98f));
        g.setGradientFill (rainbow);
        g.fillRect (strip);

        auto chrome = getLocalBounds().reduced (18);
        auto header = chrome.removeFromTop (46).toFloat();
        auto tabs = chrome.removeFromTop (36).toFloat();

        g.setColour (panelFill().withAlpha (0.48f));
        g.fillRoundedRectangle (header.reduced (0.0f, 3.0f), 4.0f);
        g.setColour (hairline().withAlpha (0.22f));
        g.drawRoundedRectangle (header.reduced (0.0f, 3.0f), 4.0f, 0.75f);

        g.setColour (panelFill().withAlpha (0.36f));
        g.fillRoundedRectangle (tabs.reduced (0.0f, 2.0f), 4.0f);
        g.setColour (hairline().withAlpha (0.18f));
        g.drawRoundedRectangle (tabs.reduced (0.0f, 2.0f), 4.0f, 0.75f);

        auto divider = tabs.withY (tabs.getBottom() + 4.0f).withHeight (1.0f).reduced (8.0f, 0.0f);
        juce::ColourGradient line (juce::Colours::transparentBlack, divider.getTopLeft(),
                                   hairline().withAlpha (0.38f), divider.getCentre(), false);
        line.addColour (1.0, juce::Colours::transparentBlack);
        g.setGradientFill (line);
        g.fillRect (divider);
    }

    void resized() override
    {
        welcomePanel.setBounds (getLocalBounds());

        auto area = getLocalBounds().reduced (18);
        auto header = area.removeFromTop (46);
        auto headerInner = header.reduced (10, 7);
        title.setBounds (headerInner.removeFromLeft (52));
        projectFileLabel.setBounds (headerInner.removeFromLeft (132).reduced (2, 2));
        statusLabel.setBounds (headerInner.removeFromLeft (86).reduced (4, 2));

        auto topCountArea = headerInner.removeFromRight (148);
        topStateCountLabel.setBounds (topCountArea.removeFromLeft (48).reduced (2, 2));
        topStateCountMinus.setBounds (topCountArea.removeFromLeft (26).reduced (2, 0));
        topStateCountEditor.setBounds (topCountArea.removeFromLeft (36).reduced (2, 0));
        topStateCountPlus.setBounds (topCountArea.removeFromLeft (26).reduced (2, 0));
        headerInner.removeFromRight (8);
        rateSlider.setBounds (headerInner.removeFromRight (122).reduced (4, 0));

        auto buttonRow = headerInner;
        const auto headerControlWidth = buttonRow.getWidth();
        headerCompactLevel = headerControlWidth < 780 ? 2 : (headerControlWidth < 920 ? 1 : 0);
        const auto compact = headerCompactLevel > 0;
        const auto tiny = headerCompactLevel > 1;

        undoButton.setButtonText (tiny ? "U" : "Undo");
        redoButton.setButtonText (tiny ? "R" : "Redo");
        logButton.setButtonText (tiny ? "L" : "Log");
        updateArrangementButtonText();

        auto addButton = [&buttonRow] (juce::Button& button, int width)
        {
            button.setVisible (true);
            button.setBounds (buttonRow.removeFromLeft (width).reduced (3, 0));
        };
        auto hideButton = [] (juce::Button& button)
        {
            button.setVisible (false);
            button.setBounds ({});
        };
        auto addGap = [&buttonRow] (int width) { buttonRow.removeFromLeft (width); };

        masterGainLabel.setBounds (buttonRow.removeFromLeft (28).reduced (3, 2));
        masterGainSlider.setBounds (buttonRow.removeFromLeft (compact ? 76 : 96).reduced (3, 0));
        addGap (5);
        addButton (runButton, compact ? 64 : 72);
        addButton (stepButton, compact ? 52 : 62);
        addButton (stopAllButton, compact ? 62 : 66);
        addButton (panicButton, compact ? 66 : 74);
        addGap (7);
        addButton (renderAllButton, tiny ? 68 : (compact ? 82 : 94));
        addGap (5);
        addButton (loadProjectButton, compact ? 52 : 62);
        addButton (saveProjectButton, compact ? 52 : 62);
        addGap (6);
        if (! tiny)
        {
            addButton (undoButton, compact ? 54 : 64);
            addButton (redoButton, compact ? 52 : 60);
            addGap (6);
        }
        else
        {
            hideButton (undoButton);
            hideButton (redoButton);
        }
        addButton (logButton, tiny ? 38 : (compact ? 48 : 54));
        if (buttonRow.getWidth() >= (tiny ? 58 : 74))
            addButton (arrangementViewButton, tiny ? 54 : (compact ? 66 : 88));
        else
            hideButton (arrangementViewButton);
        hideButton (graphFitButton);
        hideButton (graphLayoutButton);

        const auto horizontalDividerHeight = 8;
        constexpr int compactArrangementHeight = 108;
        const auto topWorkspaceHeight = 36;
        const auto minGraphHeight = codeExpanded ? 112 : 300;
        const auto minBottomHeight = codeExpanded ? 320 : 150;
        const auto maxBottomHeight = juce::jmax (minBottomHeight, area.getHeight() - topWorkspaceHeight - minGraphHeight - horizontalDividerHeight);
        if (codeExpanded)
        {
            bottomPaneHeight = juce::jlimit (minBottomHeight, maxBottomHeight,
                                             juce::roundToInt (static_cast<float> (area.getHeight()) * 0.58f));
        }
        else
        {
            if (! bottomPaneUserSized)
                bottomPaneHeight = juce::jlimit (190, 260, juce::roundToInt (static_cast<float> (area.getHeight()) * 0.24f));
            bottomPaneHeight = juce::jlimit (minBottomHeight, maxBottomHeight, bottomPaneHeight);
        }

        const auto dividerWidth = 8;
        const auto minWorkspace = 760;
        const auto minTracks = 260;
        const auto maxTracks = juce::jmax (minTracks, area.getWidth() - minWorkspace - dividerWidth);
        if (! tracksPaneUserSized)
            tracksPaneWidth = juce::jlimit (300, 360, juce::roundToInt (static_cast<float> (area.getWidth()) * 0.17f));
        tracksPaneWidth = juce::jlimit (minTracks, juce::jmin (460, maxTracks), tracksPaneWidth);

        auto tracksPane = area.removeFromRight (tracksPaneWidth);
        tracksCodeDivider.setBounds (area.removeFromRight (dividerWidth).reduced (0, 8));
        auto workspace = area;

        auto lower = workspace.removeFromBottom (bottomPaneHeight).reduced (0, 8);
        graphBottomDivider.setBounds (workspace.removeFromBottom (horizontalDividerHeight).reduced (0, 1));
        const auto minRules = codeExpanded ? 190 : 300;
        const auto minCode = 440;
        const auto maxRules = juce::jmax (minRules, lower.getWidth() - minCode - dividerWidth);
        if (codeExpanded)
            rulesPaneWidth = juce::roundToInt (static_cast<float> (lower.getWidth()) * 0.26f);
        else if (! rulesPaneUserSized)
            rulesPaneWidth = juce::roundToInt (static_cast<float> (lower.getWidth()) * 0.43f);
        rulesPaneWidth = juce::jlimit (minRules, maxRules, rulesPaneWidth);

        auto bottom = lower;
        auto rulesPane = bottom.removeFromLeft (rulesPaneWidth);
        auto dividerA = bottom.removeFromLeft (dividerWidth);
        auto codePane = bottom;

        rules.setBounds (rulesPane);
        rulesTracksDivider.setBounds (dividerA);

        auto inspectorArea = tracksPane.reduced (10, 8);
        const auto rightDividerHeight = 8;
        const auto minStatePaneHeight = 136;
        const auto minTrackPaneHeight = 300;
        const auto maxStatePaneHeight = juce::jmax (minStatePaneHeight,
                                                    inspectorArea.getHeight() - minTrackPaneHeight - rightDividerHeight);
        if (! rightInspectorUserSized)
            rightStatePaneHeight = juce::jlimit (170, 270, juce::roundToInt (static_cast<float> (inspectorArea.getHeight()) * 0.30f));
        rightStatePaneHeight = juce::jlimit (minStatePaneHeight, maxStatePaneHeight, rightStatePaneHeight);

        if (inspectorMode == InspectorMode::tracks)
        {
            const auto& inspectedForLayout = currentInspectorMachine();
            const auto laneCount = inspectedForLayout.getStateCount() > 0
                ? static_cast<int> (inspectedForLayout.state (inspectedForLayout.selectedState).lanes.size())
                : 0;
            const auto trackControlsHeight = 18 + 34 + 34 + 30 + 42 + 18;
            const auto desiredTrackPaneHeight = trackControlsHeight + laneCount * 32;
            const auto laneAwareMaxStatePaneHeight = inspectorArea.getHeight() - desiredTrackPaneHeight - rightDividerHeight;

            if (laneAwareMaxStatePaneHeight >= minStatePaneHeight)
                rightStatePaneHeight = juce::jmin (rightStatePaneHeight, laneAwareMaxStatePaneHeight);
        }

        auto statePane = inspectorArea.removeFromTop (rightStatePaneHeight);
        rightInspectorDivider.setBounds (inspectorArea.removeFromTop (rightDividerHeight).reduced (0, 1));
        auto tracksPaneLower = inspectorArea;

        auto statePaneInner = statePane.reduced (0, 2);
        breadcrumbLabel.setBounds (statePaneInner.removeFromTop (22).reduced (2, 0));
        navigator.setBounds (statePaneInner.removeFromTop (juce::jlimit (94, 138, statePaneInner.getHeight() / 3)).reduced (0, 5));
        statePaneInner.removeFromTop (2);
        stateInfoTitle.setBounds (statePaneInner.removeFromTop (18).reduced (2, 0));
        stateSummaryLabel.setBounds (statePaneInner.removeFromTop (24).reduced (2, 0));
        auto stateTimingRow = statePaneInner.removeFromTop (34);
        stateTempoLabel.setBounds (stateTimingRow.removeFromLeft (54).reduced (2, 4));
        stateTempoEditor.setBounds (stateTimingRow.removeFromLeft (66).reduced (2, 4));
        stateTimingRow.removeFromLeft (8);
        stateMeterLabel.setBounds (stateTimingRow.removeFromLeft (48).reduced (2, 4));
        stateMeterBeatsEditor.setBounds (stateTimingRow.removeFromLeft (36).reduced (2, 4));
        stateMeterSlashLabel.setBounds (stateTimingRow.removeFromLeft (14).reduced (0, 4));
        stateMeterUnitEditor.setBounds (stateTimingRow.removeFromLeft (36).reduced (2, 4));
        stateTimingRow.removeFromLeft (8);
        stateDurationModeButton.setBounds (stateTimingRow.removeFromLeft (52).reduced (2, 4));
        stateDurationBarsEditor.setBounds (stateTimingRow.removeFromLeft (34).reduced (2, 4));
        stateDurationBeatsEditor.setBounds (stateTimingRow.removeFromLeft (34).reduced (2, 4));
        stateDurationSecondsEditor.setBounds (stateTimingRow.removeFromLeft (58).reduced (2, 4));
        statePaneInner.removeFromTop (6);
        auto nestedHeaderRow = statePaneInner.removeFromTop (32);
        nestedSectionTitle.setBounds (nestedHeaderRow.removeFromLeft (104).reduced (2, 4));
        removeChildMachineButton.setBounds (nestedHeaderRow.removeFromRight (74).reduced (2, 4));
        addChildMachineButton.setBounds (nestedHeaderRow.removeFromRight (74).reduced (2, 4));
        auto nestedModeRow = statePaneInner.removeFromTop (32);
        nestedTimingLabel.setBounds (nestedModeRow.removeFromLeft (96).reduced (2, 4));
        nestedModeBox.setBounds (nestedModeRow.reduced (2, 4));
        auto divisionRow = statePaneInner.removeFromTop (32);
        nestedDivisionLabel.setBounds (divisionRow.removeFromLeft (76).reduced (2, 4));
        nestedDivisionMinus.setBounds (divisionRow.removeFromLeft (28).reduced (2, 4));
        nestedDivisionEditor.setBounds (divisionRow.removeFromLeft (42).reduced (2, 4));
        nestedDivisionPlus.setBounds (divisionRow.removeFromLeft (28).reduced (2, 4));
        auto trackPaneInner = tracksPaneLower.reduced (0, 2);
        trackSectionTitle.setBounds (trackPaneInner.removeFromTop (18).reduced (2, 0));
        auto inspectorModeRow = trackPaneInner.removeFromTop (34);
        tracksModeButton.setBounds (inspectorModeRow.removeFromLeft (88).reduced (0, 3));
        mixerModeButton.setBounds (inspectorModeRow.removeFromLeft (88).reduced (6, 3));

        const auto showTracks = inspectorMode == InspectorMode::tracks;
        trackNameEditor.setVisible (showTracks);
        shapeEditButton.setVisible (showTracks);
        resetShapeButton.setVisible (showTracks);
        freezeStatusLabel.setVisible (showTracks);
        refreezeLaneButton.setVisible (showTracks);
        refreezeStaleButton.setVisible (showTracks);
        renderAllButton.setVisible (true);
        trackPaneTitle.setVisible (showTracks);
        moveLaneUpButton.setVisible (showTracks);
        moveLaneDownButton.setVisible (showTracks);
        addLaneButton.setVisible (showTracks);
        removeLaneButton.setVisible (showTracks);
        duplicateLaneButton.setVisible (showTracks);
        trackList.setVisible (showTracks);
        mixer.setVisible (! showTracks);

        if (showTracks)
        {
            auto trackNameRow = trackPaneInner.removeFromTop (34);
            trackNameEditor.setBounds (trackNameRow.reduced (0, 2));
            auto freezeRow = trackPaneInner.removeFromTop (30);
            refreezeStaleButton.setBounds (freezeRow.removeFromRight (76).reduced (2, 4));
            refreezeLaneButton.setBounds (freezeRow.removeFromRight (76).reduced (2, 4));
            freezeStatusLabel.setBounds (freezeRow.reduced (2, 4));
            auto trackHeader = trackPaneInner.removeFromTop (42);
            trackPaneTitle.setBounds (trackHeader.removeFromLeft (58).reduced (2, 4));
            resetShapeButton.setBounds (trackHeader.removeFromRight (78).reduced (2, 5));
            shapeEditButton.setBounds (trackHeader.removeFromRight (78).reduced (2, 5));
            trackHeader.removeFromRight (4);
            moveLaneUpButton.setBounds (trackHeader.removeFromRight (30).reduced (2, 5));
            moveLaneDownButton.setBounds (trackHeader.removeFromRight (30).reduced (2, 5));
            trackHeader.removeFromRight (6);
            removeLaneButton.setBounds (trackHeader.removeFromRight (38).reduced (2, 5));
            addLaneButton.setBounds (trackHeader.removeFromRight (38).reduced (2, 5));
            duplicateLaneButton.setBounds (trackHeader.removeFromRight (48).reduced (2, 5));
            trackList.setBounds (trackPaneInner.reduced (0, 4));
            mixer.setBounds ({});
        }
        else
        {
            trackNameEditor.setBounds ({});
            shapeEditButton.setBounds ({});
            resetShapeButton.setBounds ({});
            freezeStatusLabel.setBounds ({});
            refreezeLaneButton.setBounds ({});
            refreezeStaleButton.setBounds ({});
            trackPaneTitle.setBounds ({});
            moveLaneUpButton.setBounds ({});
            moveLaneDownButton.setBounds ({});
            addLaneButton.setBounds ({});
            removeLaneButton.setBounds ({});
            duplicateLaneButton.setBounds ({});
            trackList.setBounds ({});
            mixer.setBounds (trackPaneInner.reduced (0, 4));
        }

        auto codePaneInner = codePane.reduced (8, 0);
        auto codeHeader = codePaneInner.removeFromTop (34);
        codePaneTitle.setBounds (codeHeader.removeFromLeft (74).reduced (3));
        codeStatsLabel.setBounds (codeHeader.removeFromRight (132).reduced (3));
        codeFontSizeEditor.setBounds (codeHeader.removeFromRight (50).reduced (3));
        tidyCodeButton.setBounds (codeHeader.removeFromRight (58).reduced (3));
        expandCodeButton.setBounds (codeHeader.removeFromRight (72).reduced (3));
        playButton.setBounds (codeHeader.removeFromRight (76).reduced (3));
        checkCodeButton.setBounds (codeHeader.removeFromRight (66).reduced (3));
        codeCheckLabel.setBounds (codeHeader.reduced (3));
        scriptEditor.setBounds (codePaneInner.reduced (0, 6));

        stateTabs.setBounds (workspace.removeFromTop (36));

        auto graphArea = workspace.reduced (0, 10);
        if (logVisible)
            logView.setBounds (graphArea.removeFromBottom (142).reduced (0, 8));

        if (arrangementViewMode == 1)
        {
            orbitCanvas.setBounds (graphArea);
            graph.setBounds ({});
            arrangementStrip.setBounds (graphArea.removeFromTop (compactArrangementHeight).reduced (5, 6));
            arrangementStrip.toFront (false);
        }
        else if (arrangementViewMode == 2)
        {
            graph.setBounds ({});
            orbitCanvas.setBounds ({});
            arrangementStrip.setBounds (graphArea);
        }
        else
        {
            graph.setBounds ({});
            orbitCanvas.setBounds (graphArea);
        }
    }

    void preserveGraphNodePositionsDuringLayout()
    {
        graph.beginNodePositionLock();
        resized();
        graph.endNodePositionLock();
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        const auto mods = key.getModifiers();
        const auto keyCode = key.getKeyCode();

        if (isLaneDeleteKey (key) && ! mods.isCommandDown() && ! mods.isCtrlDown() && ! mods.isAltDown())
            return deleteSelectedLane();

        if (mods.isCommandDown() && (keyCode == 'z' || keyCode == 'Z'))
        {
            if (mods.isShiftDown())
                redoProjectEdit();
            else
                undoProjectEdit();

            return true;
        }

        return false;
    }

private:
    void configureSmallNumberEditor (juce::TextEditor& editor, int maxChars, const juce::String& allowedChars)
    {
        editor.setInputRestrictions (maxChars, allowedChars);
        editor.setJustification (juce::Justification::centred);
        editor.setMultiLine (false);
        editor.setSelectAllWhenFocused (true);
        editor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff111318));
        editor.setColour (juce::TextEditor::textColourId, ink());
        editor.setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff34414a));
        editor.setColour (juce::TextEditor::focusedOutlineColourId, accentA());
    }

    void updateCodeEditorFont()
    {
        scriptEditor.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), codeFontSize, juce::Font::plain)));
    }

    void commitCodeFontSizeEditor()
    {
        const auto requestedSize = codeFontSizeEditor.getText().getIntValue();
        codeFontSize = static_cast<float> (juce::jlimit (11, 24, requestedSize <= 0 ? juce::roundToInt (codeFontSize) : requestedSize));
        codeFontSizeEditor.setText (juce::String (juce::roundToInt (codeFontSize)), false);
        updateCodeEditorFont();
        scriptEditor.grabKeyboardFocus();
    }

    enum class InspectorMode
    {
        tracks,
        mixer
    };

    enum class UndoGroup
    {
        structural,
        text,
        continuous
    };

    struct LaneMeterState
    {
        float rms = 0.0f;
        float peak = 0.0f;
        double lastSeenMs = 0.0;
        bool provisional = false;
    };

    void setInspectorMode (InspectorMode mode)
    {
        inspectorMode = mode;
        updateInspectorModeButtons();
        resized();
    }

    void updateInspectorModeButtons()
    {
        const auto tracksActive = inspectorMode == InspectorMode::tracks;
        tracksModeButton.setColour (juce::TextButton::buttonColourId, tracksActive ? rowFill().interpolatedWith (graphColour (0), 0.18f) : panelFill().brighter (0.04f));
        tracksModeButton.setColour (juce::TextButton::textColourOffId, tracksActive ? graphColour (0).brighter (0.08f) : mutedInk());
        mixerModeButton.setColour (juce::TextButton::buttonColourId, ! tracksActive ? rowFill().interpolatedWith (graphColour (1), 0.18f) : panelFill().brighter (0.04f));
        mixerModeButton.setColour (juce::TextButton::textColourOffId, ! tracksActive ? graphColour (1).brighter (0.08f) : mutedInk());
    }

    LaneMeterValues meterForLane (const juce::String& laneId) const
    {
        const auto found = laneMeters.find (laneId.toStdString());
        if (found == laneMeters.end())
            return {};

        const auto ageMs = juce::Time::getMillisecondCounterHiRes() - found->second.lastSeenMs;
        const auto expiryMs = found->second.provisional ? 950.0 : 360.0;
        if (ageMs > expiryMs)
            return {};

        const auto hold = static_cast<float> (juce::jlimit (0.0, 1.0, 1.0 - (ageMs / expiryMs)));
        return { found->second.rms * hold, found->second.peak * hold, true };
    }

    void updateCodeStats()
    {
        const auto lineCount = codeDocument.getNumLines();
        const auto caret = scriptEditor.getCaretPos();
        const auto chars = currentInspectorMachine().selectedLaneRef().script.length();

        codeStatsLabel.setText ("Ln " + juce::String (caret.getLineNumber() + 1)
                                + ", Col " + juce::String (caret.getIndexInLine() + 1)
                                + "  |  " + juce::String (lineCount) + " lines"
                                + "  |  " + juce::String (chars) + " chars",
                                juce::dontSendNotification);
    }

    void setCodeCheckStatus (const juce::String& text, juce::Colour colour)
    {
        codeCheckLabel.setText (text, juce::dontSendNotification);
        codeCheckLabel.setColour (juce::Label::textColourId, colour);
    }

    void checkSelectedLaneScript()
    {
        const auto checkId = host.checkScript (codeDocument.getAllContent(), getSclangPathOverride());
        if (checkId.isEmpty())
        {
            pendingCheckId.clear();
            setCodeCheckStatus ("Check failed: audio offline", accentC());
            return;
        }

        pendingCheckId = checkId;
        setCodeCheckStatus ("Checking...", accentA());
        pollCheckResult (checkId, 0);
    }

    void pollCheckResult (juce::String checkId, int attempt)
    {
        juce::Timer::callAfterDelay (120, [safeThis = juce::Component::SafePointer<MainComponent> (this), checkId, attempt]
        {
            if (safeThis == nullptr || safeThis->pendingCheckId != checkId)
                return;

            const auto result = safeThis->host.readCheckResult (checkId);
            if (result.isNotEmpty())
            {
                safeThis->handleCheckResultText (checkId, result);
                return;
            }

            if (attempt < 60)
                safeThis->pollCheckResult (checkId, attempt + 1);
            else
            {
                safeThis->pendingCheckId.clear();
                safeThis->setCodeCheckStatus ("Check timed out", accentC());
            }
        });
    }

    void handleCheckResultText (const juce::String& checkId, const juce::String& result)
    {
        if (pendingCheckId != checkId)
            return;

        pendingCheckId.clear();

        if (result.startsWith ("OK"))
        {
            setCodeCheckStatus ("OK", juce::Colour (0xff7bd88f));
            scriptEditor.deselectAll();
            return;
        }

        auto errorText = result.fromFirstOccurrenceOf ("ERROR", false, false).trim();
        if (errorText.isEmpty())
            errorText = "SuperCollider reported an error";

        auto line = extractErrorLineNumber (errorText);
        if (line <= 0)
            line = scriptEditor.getCaretPos().getLineNumber() + 1;

        highlightCodeLine (line);

        setCodeCheckStatus ("Error" + (line > 0 ? " line " + juce::String (line) : "") + ": " + errorText.upToFirstOccurrenceOf ("\n", false, false),
                            accentC());
    }

    void handleHostLogMessage (const juce::String& message)
    {
        handleSchedulerStateMessage (message);

        if (pendingCheckId.isEmpty())
            return;

        const auto okMarker = "OF_CHECK_OK " + pendingCheckId;
        const auto errorMarker = "OF_CHECK_ERROR " + pendingCheckId;

        if (message.contains (okMarker))
        {
            pendingCheckId.clear();
            setCodeCheckStatus ("OK", juce::Colour (0xff7bd88f));
            scriptEditor.deselectAll();
            return;
        }

        const auto errorIndex = message.indexOf (errorMarker);
        if (errorIndex >= 0)
        {
            auto errorText = message.substring (errorIndex + errorMarker.length()).trim();
            if (errorText.isEmpty())
                errorText = "SuperCollider reported an error";

            pendingCheckId.clear();
            auto line = extractErrorLineNumber (message);
            if (line <= 0)
                line = scriptEditor.getCaretPos().getLineNumber() + 1;

            highlightCodeLine (line);

            setCodeCheckStatus ("Error" + (line > 0 ? " line " + juce::String (line) : "") + ": " + errorText.upToFirstOccurrenceOf ("\n", false, false),
                                accentC());
        }
    }

    void handleSchedulerStateMessage (const juce::String& message)
    {
        const auto marker = "OF_STATE ";
        const auto markerIndex = message.indexOf (marker);
        if (markerIndex < 0)
            return;

        auto payload = message.substring (markerIndex + juce::String (marker).length()).trim();
        auto parts = juce::StringArray::fromTokens (payload, " \t\r\n", "");
        if (parts.size() < 2 || parts[0] != machine.machineId)
            return;

        applySchedulerState (parts[0], parts[1].getIntValue());
    }

    void oscMessageReceived (const juce::OSCMessage& message) override
    {
        const auto address = message.getAddressPattern().toString();
        if (address == "/of/meter")
        {
            handleMeterMessage (message);
            return;
        }

        if (address == "/of/pulse")
        {
            handlePulseMessage (message);
            return;
        }

        if (address == "/of/scheduled")
        {
            handleScheduledTransitionMessage (message);
            return;
        }

        if (address == "/of/frozen")
        {
            handleFrozenMessage (message);
            return;
        }

        if (address == "/of/exported")
        {
            handleExportedMessage (message);
            return;
        }

        if (address == "/of/exportProgress")
        {
            handleExportProgressMessage (message);
            return;
        }

        if (address != "/of/state")
            return;

        if (message.size() < 2 || ! message[0].isString())
            return;

        auto machineId = message[0].getString();
        auto stateIndex = -1;

        if (message[1].isInt32())
            stateIndex = message[1].getInt32();
        else if (message[1].isFloat32())
            stateIndex = static_cast<int> (message[1].getFloat32());
        else if (message[1].isString())
            stateIndex = message[1].getString().getIntValue();

        applySchedulerState (machineId, stateIndex);
    }

    void handlePulseMessage (const juce::OSCMessage& message)
    {
        if (message.size() < 5 || ! message[0].isString())
            return;

        const auto machineId = message[0].getString();
        const auto stateIndex = message[1].isInt32() ? message[1].getInt32() : static_cast<int> (getOscFloat (message[1]));
        const auto phase = getOscFloat (message[2]);
        const auto beatIndex = message[3].isInt32() ? message[3].getInt32() : static_cast<int> (getOscFloat (message[3]));
        const auto beatCount = message[4].isInt32() ? message[4].getInt32() : static_cast<int> (getOscFloat (message[4]));

        graph.setTimingPulse (machineId, stateIndex, phase, beatIndex, beatCount);
        arrangementStrip.setTimingPulse (machineId, stateIndex, phase, beatIndex, beatCount);
        updateTransitionPreview();
    }

    void handleScheduledTransitionMessage (const juce::OSCMessage& message)
    {
        if (message.size() < 4 || ! message[0].isString())
            return;

        const auto machineId = message[0].getString();
        if (machineId != machine.machineId)
            return;

        const auto fromState = message[1].isInt32() ? message[1].getInt32() : static_cast<int> (getOscFloat (message[1]));
        const auto nextState = message[2].isInt32() ? message[2].getInt32() : static_cast<int> (getOscFloat (message[2]));
        const auto durationSeconds = juce::jmax (0.05f, getOscFloat (message[3]));

        if (nextState < 0 || nextState >= machine.getStateCount())
            return;

        scheduledVisualFromState = fromState;
        scheduledVisualNextState = nextState;
        scheduledTransitionTargetMs = juce::Time::getMillisecondCounterHiRes() + static_cast<double> (durationSeconds) * 1000.0;
        visualNextStateMs = scheduledTransitionTargetMs;
        appendLog ("Scheduled visual: "
                   + (fromState >= 0 && fromState < machine.getStateCount() ? machine.state (fromState).name : juce::String ("?"))
                   + " -> " + machine.state (nextState).name
                   + " in " + juce::String (durationSeconds, 3) + "s");
        graph.setTransitionPreview (nextState, 1.0f);
        graph.repaint();
        arrangementStrip.repaint();
    }

    void handleFrozenMessage (const juce::OSCMessage& message)
    {
        if (message.size() < 2 || ! message[0].isString() || ! message[1].isString())
            return;

        auto* lane = findLaneById (machine, message[0].getString());
        if (lane == nullptr)
            return;

        lane->frozen = true;
        lane->freezeStale = false;
        lane->freezeInProgress = false;
        lane->frozenAudioPath = message[1].getString();
        if (lane->frozenDurationSeconds <= 0.0)
            lane->frozenDurationSeconds = laneRenderDurationSeconds (machine.state (machine.selectedState), *lane) / juce::jmax (0.05, rateSlider.getValue());
        lane->preparedBridge = -1;
        statusLabel.setText ("Freeze ready", juce::dontSendNotification);
        orbitCanvas.invalidateWaveforms();
        markMachineDirty (UndoGroup::continuous);
        refreshProjectMediaStatus();
        refreshControls();
    }

    void handleExportedMessage (const juce::OSCMessage& message)
    {
        if (message.size() < 2 || ! message[0].isString())
            return;

        const auto outputPath = message[0].getString();
        const auto status = message[1].isInt32() ? message[1].getInt32() : static_cast<int> (getOscFloat (message[1]));
        const auto ok = status > 0;
        const auto cancelled = status < 0;
        audioJobRunning = false;
        exportInProgress = false;
        exportCancelRequested = false;
        exportElapsedSeconds = 0.0;
        exportTotalSeconds = 0.0;
        statusLabel.setText (ok ? "Audio exported" : (cancelled ? "Audio export cancelled" : "Audio export failed"), juce::dontSendNotification);
        appendLog ((ok ? "Audio export ready: " : (cancelled ? "Audio export cancelled: " : "Audio export failed: ")) + outputPath);
        refreshControls();
    }

    void handleExportProgressMessage (const juce::OSCMessage& message)
    {
        if (message.size() < 3 || ! message[0].isString())
            return;

        if (! exportInProgress)
            return;

        exportElapsedSeconds = juce::jlimit (0.0, 1800.0, static_cast<double> (getOscFloat (message[1])));
        exportTotalSeconds = juce::jlimit (1.0, 1800.0, static_cast<double> (getOscFloat (message[2])));
        statusLabel.setText ("Exporting " + juce::String (exportElapsedSeconds, 1)
                             + " / " + juce::String (exportTotalSeconds, 1) + "s",
                             juce::dontSendNotification);
        if (arrangementViewMode > 0)
            arrangementStrip.setMachine (machine, rateSlider.getValue(), arrangementViewMode == 2, exportInProgress, exportElapsedSeconds, exportTotalSeconds);
    }

    void handleMeterMessage (const juce::OSCMessage& message)
    {
        if (message.size() < 3 || ! message[0].isString())
            return;

        const auto laneId = message[0].getString();
        const auto rms = getOscFloat (message[1]);
        const auto peak = getOscFloat (message[2]);
        auto& meter = laneMeters[laneId.toStdString()];
        meter.rms = juce::jlimit (0.0f, 1.0f, rms);
        meter.peak = juce::jlimit (0.0f, 1.0f, peak);
        meter.lastSeenMs = juce::Time::getMillisecondCounterHiRes();
        meter.provisional = false;
    }

    void timerCallback() override
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        flushLogViewIfNeeded (now, false);

        if (now - lastAutosaveTimerMs >= 1000.0)
        {
            lastAutosaveTimerMs = now;
            autosaveIfNeeded (false);
        }

        tickVisualScheduler (now);
        tickOrbitConnections (now);
    }

    float getOscFloat (const juce::OSCArgument& argument) const
    {
        if (argument.isFloat32())
            return argument.getFloat32();

        if (argument.isInt32())
            return static_cast<float> (argument.getInt32());

        if (argument.isString())
            return argument.getString().getFloatValue();

        return 0.0f;
    }

    static juce::String timingModeToProjectString (NestedTimingMode mode)
    {
        switch (mode)
        {
            case NestedTimingMode::followParent: return "follow";
            case NestedTimingMode::freeRun: return "free";
            case NestedTimingMode::oneShot: return "oneShot";
            case NestedTimingMode::latch: return "latch";
        }

        return "follow";
    }

    static NestedTimingMode timingModeFromProjectString (const juce::String& text)
    {
        if (text == "free") return NestedTimingMode::freeRun;
        if (text == "oneShot") return NestedTimingMode::oneShot;
        if (text == "latch") return NestedTimingMode::latch;
        return NestedTimingMode::followParent;
    }

    juce::File appStateFile() const
    {
        auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                       .getChildFile ("of");
        dir.createDirectory();
        return dir.getChildFile ("app-state.json");
    }

    juce::File autosaveFile() const
    {
        auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                       .getChildFile ("of")
                       .getChildFile ("autosave");
        dir.createDirectory();
        return dir.getChildFile ("Last Session.markovfsm");
    }

    void addRecentProject (const juce::File& file)
    {
        if (file == juce::File())
            return;

        recentProjects.removeString (file.getFullPathName());
        recentProjects.insert (0, file.getFullPathName());
        while (recentProjects.size() > 8)
            recentProjects.remove (recentProjects.size() - 1);
    }

    void saveAppState()
    {
        auto object = new juce::DynamicObject();
        object->setProperty ("lastProject", currentProjectFile.getFullPathName());
        object->setProperty ("lastAutosave", autosaveFile().getFullPathName());
        object->setProperty ("colourblindSafeMode", colourblindSafeMode);
        object->setProperty ("arrangementViewMode", arrangementViewMode);
        object->setProperty ("arrangementViewVisible", arrangementViewMode > 0);
        object->setProperty ("scOutputDevice", scAudioSettings.outputDevice);
        object->setProperty ("scSampleRate", scAudioSettings.sampleRate);
        object->setProperty ("scHardwareBufferSize", scAudioSettings.hardwareBufferSize);
        object->setProperty ("scOutputChannels", scAudioSettings.outputChannels);
        object->setProperty ("exportRange", exportSettings.range);
        object->setProperty ("exportCycles", exportSettings.cycles);
        object->setProperty ("exportCustomSeconds", exportSettings.customSeconds);
        object->setProperty ("exportTailSeconds", exportSettings.tailSeconds);
        object->setProperty ("exportSampleFormat", exportSettings.sampleFormat);
        object->setProperty ("masterGain", masterGainSlider.getValue());

        juce::Array<juce::var> recent;
        for (const auto& path : recentProjects)
            recent.add (path);
        object->setProperty ("recentProjects", recent);

        appStateFile().replaceWithText (juce::JSON::toString (juce::var (object), true));
    }

    void loadAppState()
    {
        auto parsed = juce::JSON::parse (appStateFile());
        if (! parsed.isObject())
            return;

        recentProjects.clear();
        if (auto* recent = parsed.getProperty ("recentProjects", {}).getArray())
            for (const auto& path : *recent)
                if (path.toString().isNotEmpty())
                    recentProjects.addIfNotAlreadyThere (path.toString());

        auto lastProject = parsed.getProperty ("lastProject", {}).toString();
        if (lastProject.isNotEmpty())
            currentProjectFile = juce::File (lastProject);

        colourblindSafeMode = static_cast<bool> (parsed.getProperty ("colourblindSafeMode", false));
        setColourblindSafePalette (colourblindSafeMode);
        arrangementViewMode = juce::jlimit (0, 2, static_cast<int> (parsed.getProperty ("arrangementViewMode",
                                                                                         static_cast<bool> (parsed.getProperty ("arrangementViewVisible", false)) ? 1 : 0)));
        scAudioSettings.outputDevice = parsed.getProperty ("scOutputDevice", {}).toString().trim();
        scAudioSettings.sampleRate = static_cast<double> (parsed.getProperty ("scSampleRate", 0.0));
        scAudioSettings.hardwareBufferSize = static_cast<int> (parsed.getProperty ("scHardwareBufferSize", 64));
        scAudioSettings.outputChannels = static_cast<int> (parsed.getProperty ("scOutputChannels", 2));
        scAudioSettings.sampleRate = scAudioSettings.sampleRate <= 0.0 ? 0.0 : juce::jlimit (8000.0, 384000.0, scAudioSettings.sampleRate);
        scAudioSettings.hardwareBufferSize = juce::jlimit (16, 4096, scAudioSettings.hardwareBufferSize <= 0 ? 64 : scAudioSettings.hardwareBufferSize);
        scAudioSettings.outputChannels = juce::jlimit (1, 64, scAudioSettings.outputChannels <= 0 ? 2 : scAudioSettings.outputChannels);
        host.setAudioSettings (scAudioSettings);
        exportSettings.range = parsed.getProperty ("exportRange", "cycle").toString();
        if (exportSettings.range != "state" && exportSettings.range != "custom")
            exportSettings.range = "cycle";
        exportSettings.cycles = juce::jlimit (1, 16, static_cast<int> (parsed.getProperty ("exportCycles", 1)));
        exportSettings.customSeconds = juce::jlimit (1.0, 1800.0, static_cast<double> (parsed.getProperty ("exportCustomSeconds", 30.0)));
        exportSettings.tailSeconds = juce::jlimit (0.0, 60.0, static_cast<double> (parsed.getProperty ("exportTailSeconds", 2.0)));
        exportSettings.sampleFormat = parsed.getProperty ("exportSampleFormat", "int16").toString();
        if (exportSettings.sampleFormat != "int24" && exportSettings.sampleFormat != "float")
            exportSettings.sampleFormat = "int16";
        const auto masterGain = juce::jlimit (0.0, 5.0, static_cast<double> (parsed.getProperty ("masterGain", masterGainSlider.getValue())));
        masterGainSlider.setValue (masterGain, juce::dontSendNotification);
        host.setMasterGain (static_cast<float> (masterGain));
        renderedAudioPlayer.setMasterGain (static_cast<float> (masterGain));
    }

    bool restoreLastProject()
    {
        loadAppState();

        const auto autosave = autosaveFile();
        if (autosave.existsAsFile())
        {
            loadingProjectInternally = true;
            const auto loaded = loadProjectFromFile (autosave, false);
            loadingProjectInternally = false;
            if (loaded)
            {
                statusLabel.setText ("Restored last session", juce::dontSendNotification);
                return true;
            }
        }

        return false;
    }

    juce::File projectMediaFreezeDirectory (const juce::File& projectFile) const
    {
        return projectFile.getSiblingFile (projectFile.getFileNameWithoutExtension() + " Media")
                          .getChildFile ("Freezes");
    }

    juce::File resolveProjectMediaFile (const juce::String& path, const juce::File& projectFile) const
    {
        if (path.isEmpty())
            return {};

        if (juce::File::isAbsolutePath (path))
            return juce::File (path);

        auto resolved = projectFile.getParentDirectory().getChildFile (path);
        if (resolved.existsAsFile() || ! currentProjectFile.existsAsFile())
            return resolved;

        auto oldProjectRelative = currentProjectFile.getParentDirectory().getChildFile (path);
        return oldProjectRelative.existsAsFile() ? oldProjectRelative : resolved;
    }

    juce::String resolveProjectMediaPathForLoad (const juce::String& path) const
    {
        if (path.isEmpty() || juce::File::isAbsolutePath (path))
            return path;

        const auto base = loadingProjectDirectory.isDirectory()
            ? loadingProjectDirectory
            : (currentProjectFile.existsAsFile() ? currentProjectFile.getParentDirectory() : juce::File());

        if (base == juce::File())
            return path;

        return base.getChildFile (path).getFullPathName();
    }

    juce::String relativeMediaPathForProject (const juce::File& mediaFile, const juce::File& projectFile) const
    {
        return mediaFile.getRelativePathFrom (projectFile.getParentDirectory()).replaceCharacter ('\\', '/');
    }

    struct ProjectMediaStatus
    {
        int frozen = 0;
        int stale = 0;
        int missing = 0;
        int unbundled = 0;

        bool needsAttention() const { return stale > 0 || missing > 0 || unbundled > 0; }
    };

    bool fileIsInsideDirectory (const juce::File& file, const juce::File& directory) const
    {
        if (file == juce::File() || directory == juce::File())
            return false;

        const auto dirPath = directory.getFullPathName().trimCharactersAtEnd (juce::File::getSeparatorString());
        const auto filePath = file.getFullPathName();
        return filePath == dirPath || filePath.startsWith (dirPath + juce::File::getSeparatorString());
    }

    void validateProjectMediaInMachine (const MachineModel& model, const juce::File& projectFile, ProjectMediaStatus& status) const
    {
        const auto mediaDirectory = projectFile.existsAsFile() || projectFile.getFileName().isNotEmpty()
            ? projectMediaFreezeDirectory (projectFile)
            : juce::File();

        for (const auto& state : model.states)
        {
            for (const auto& lane : state.lanes)
            {
                if (! lane.frozen)
                    continue;

                ++status.frozen;
                if (lane.freezeStale)
                    ++status.stale;

                const auto path = lane.frozenAudioPath;
                const auto file = projectFile == juce::File() ? juce::File (path) : resolveProjectMediaFile (path, projectFile);
                if (path.isEmpty() || ! file.existsAsFile())
                {
                    ++status.missing;
                    continue;
                }

                if (projectFile != juce::File() && ! fileIsInsideDirectory (file, mediaDirectory))
                    ++status.unbundled;
            }

            if (auto* child = model.childMachine (state.index))
                validateProjectMediaInMachine (*child, projectFile, status);
        }
    }

    ProjectMediaStatus validateProjectMedia (const juce::File& projectFile = {}) const
    {
        ProjectMediaStatus status;
        const auto file = projectFile != juce::File() ? projectFile : currentProjectFile;
        validateProjectMediaInMachine (machine, file, status);
        return status;
    }

    juce::String mediaStatusSummary (const ProjectMediaStatus& status) const
    {
        juce::StringArray parts;
        if (status.missing > 0)
            parts.add (juce::String (status.missing) + " missing");
        if (status.stale > 0)
            parts.add (juce::String (status.stale) + " stale");
        if (status.unbundled > 0)
            parts.add (juce::String (status.unbundled) + " unbundled");

        return parts.joinIntoString (", ");
    }

    juce::String projectStatusAfterMediaCheck (const juce::String& prefix, const ProjectMediaStatus& status) const
    {
        const auto summary = mediaStatusSummary (status);
        return summary.isEmpty() ? prefix : prefix + ": " + summary;
    }

    void refreshProjectMediaStatus (const juce::File& projectFile = {})
    {
        cachedProjectMediaStatus = validateProjectMedia (projectFile);
    }

    void bundleFrozenMediaForProject (MachineModel& model, const juce::File& projectFile)
    {
        auto mediaDirectory = projectMediaFreezeDirectory (projectFile);
        mediaDirectory.createDirectory();

        for (auto& state : model.states)
        {
            for (auto& lane : state.lanes)
            {
                if (! lane.frozen || lane.freezeStale || lane.frozenAudioPath.isEmpty())
                    continue;

                auto source = resolveProjectMediaFile (lane.frozenAudioPath, projectFile);
                if (! source.existsAsFile())
                {
                    lane.freezeStale = true;
                    continue;
                }

                auto safeId = lane.id.retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_");
                if (safeId.isEmpty())
                    safeId = juce::Uuid().toString().substring (0, 8);

                const auto destination = mediaDirectory.getChildFile (safeId + ".wav");
                if (source != destination)
                {
                    destination.deleteFile();
                    if (! source.copyFileTo (destination))
                    {
                        lane.freezeStale = true;
                        continue;
                    }
                }

                lane.frozenAudioPath = relativeMediaPathForProject (destination, projectFile);
            }

            if (auto* child = model.childMachine (state.index))
                bundleFrozenMediaForProject (*child, projectFile);
        }
    }

    juce::var laneToProjectVar (const Lane& lane) const
    {
        auto object = new juce::DynamicObject();
        object->setProperty ("id", lane.id);
        object->setProperty ("name", lane.name);
        object->setProperty ("script", lane.script);
        object->setProperty ("volume", lane.volume);
        object->setProperty ("gain", lane.gain);
        object->setProperty ("pan", lane.pan);
        object->setProperty ("enabled", lane.enabled);
        object->setProperty ("muted", lane.muted);
        object->setProperty ("solo", lane.solo);
        object->setProperty ("frozen", lane.frozen);
        object->setProperty ("freezeStale", lane.freezeStale);
        object->setProperty ("frozenAudioPath", lane.frozenAudioPath);
        object->setProperty ("frozenDurationSeconds", lane.frozenDurationSeconds);
        object->setProperty ("sourceScriptPath", lane.sourceScriptPath);
        object->setProperty ("orbitPhase", lane.orbitPhase);
        object->setProperty ("durationMode", laneDurationModeToString (lane.durationMode));
        object->setProperty ("durationValue", lane.durationValue);
        object->setProperty ("fadeInSeconds", lane.fadeInSeconds);
        object->setProperty ("fadeOutSeconds", lane.fadeOutSeconds);
        return object;
    }

    juce::var stateToProjectVar (const MachineModel& model, const State& state) const
    {
        auto object = new juce::DynamicObject();
        object->setProperty ("name", state.name);
        object->setProperty ("tempoBpm", state.tempoBpm);
        object->setProperty ("beatsPerBar", state.beatsPerBar);
        object->setProperty ("beatUnit", state.beatUnit);
        object->setProperty ("arrangementBars", state.arrangementBars);
        object->setProperty ("arrangementBeats", state.arrangementBeats);
        object->setProperty ("durationUsesSeconds", state.durationUsesSeconds);
        object->setProperty ("durationSeconds", state.durationSeconds);
        juce::Array<juce::var> orbitWarp;
        for (const auto warp : state.orbitWarp)
            orbitWarp.add (warp);
        object->setProperty ("orbitWarp", orbitWarp);

        juce::Array<juce::var> lanes;
        for (const auto& lane : state.lanes)
            lanes.add (laneToProjectVar (lane));
        object->setProperty ("lanes", lanes);

        if (auto* child = model.childMachine (state.index))
            object->setProperty ("child", machineToProjectVar (*child));

        return object;
    }

    juce::var machineToProjectVar (const MachineModel& model) const
    {
        auto object = new juce::DynamicObject();
        object->setProperty ("machineId", model.machineId);
        object->setProperty ("lanePrefix", model.lanePrefix);
        object->setProperty ("timingMode", timingModeToProjectString (model.timingMode));
        object->setProperty ("parentDivision", model.parentDivision);
        object->setProperty ("selectedState", model.selectedState);
        object->setProperty ("selectedLane", model.selectedLane);
        object->setProperty ("entryState", model.entryState);

        juce::Array<juce::var> states;
        for (const auto& state : model.states)
            states.add (stateToProjectVar (model, state));
        object->setProperty ("states", states);

        juce::Array<juce::var> nodeOffsets;
        for (int i = 0; i < model.getStateCount(); ++i)
        {
            const auto offset = i < static_cast<int> (model.nodeOffsets.size())
                ? model.nodeOffsets[static_cast<size_t> (i)]
                : juce::Point<float>();
            auto offsetObject = new juce::DynamicObject();
            offsetObject->setProperty ("x", offset.x);
            offsetObject->setProperty ("y", offset.y);
            nodeOffsets.add (offsetObject);
        }
        object->setProperty ("nodeOffsets", nodeOffsets);

        juce::Array<juce::var> projectRules;
        for (const auto& rule : model.rules)
        {
            auto ruleObject = new juce::DynamicObject();
            ruleObject->setProperty ("from", rule.from);
            ruleObject->setProperty ("to", rule.to);
            ruleObject->setProperty ("weight", rule.weight);
            projectRules.add (ruleObject);
        }
        object->setProperty ("rules", projectRules);

        juce::Array<juce::var> projectConnections;
        for (const auto& connection : model.orbitConnections)
        {
            auto connectionObject = new juce::DynamicObject();
            connectionObject->setProperty ("sourceState", connection.sourceState);
            connectionObject->setProperty ("sourceLane", connection.sourceLane);
            connectionObject->setProperty ("sourcePhase", connection.sourcePhase);
            connectionObject->setProperty ("targetState", connection.targetState);
            connectionObject->setProperty ("action", orbitConnectionActionToString (connection.action));
            connectionObject->setProperty ("fabricScript", connection.fabricScript);
            projectConnections.add (connectionObject);
        }
        object->setProperty ("orbitConnections", projectConnections);

        return object;
    }

    juce::var makeProjectVar() const
    {
        auto object = new juce::DynamicObject();
        object->setProperty ("format", "of:: Project");
        object->setProperty ("version", 1);
        object->setProperty ("rate", rateSlider.getValue());
        object->setProperty ("masterGain", masterGainSlider.getValue());
        object->setProperty ("machine", machineToProjectVar (machine));
        return object;
    }

    juce::String makeProjectSnapshotString() const
    {
        return juce::JSON::toString (makeProjectVar(), false);
    }

    void updateUndoRedoButtons()
    {
        undoButton.setEnabled (! undoSnapshots.empty());
        redoButton.setEnabled (! redoSnapshots.empty());
    }

    void updateArrangementButtonText()
    {
        if (headerCompactLevel > 1)
            arrangementViewButton.setButtonText (arrangementViewMode == 2 ? "Arr+" : "Arr");
        else if (headerCompactLevel > 0)
            arrangementViewButton.setButtonText (arrangementViewMode == 2 ? "Arr+" : "Arr.");
        else
            arrangementViewButton.setButtonText (arrangementViewMode == 2 ? "Arrange+" : "Arrange");
    }

    void resetUndoHistory()
    {
        undoSnapshots.clear();
        redoSnapshots.clear();
        lastProjectSnapshot = makeProjectSnapshotString();
        lastUndoGroup = UndoGroup::structural;
        lastUndoSnapshotMs = juce::Time::getMillisecondCounterHiRes();
        updateUndoRedoButtons();
    }

    void recordUndoSnapshotAfterMutation (UndoGroup group)
    {
        if (suppressUndoCapture)
            return;

        const auto currentSnapshot = makeProjectSnapshotString();
        const auto now = juce::Time::getMillisecondCounterHiRes();
        if (lastProjectSnapshot.isEmpty())
        {
            lastProjectSnapshot = currentSnapshot;
            lastUndoGroup = group;
            lastUndoSnapshotMs = now;
            updateUndoRedoButtons();
            return;
        }

        if (currentSnapshot == lastProjectSnapshot)
            return;

        const auto coalesceWindowMs = group == UndoGroup::text ? 1400.0 : 700.0;
        const auto canCoalesce = group != UndoGroup::structural
                              && group == lastUndoGroup
                              && now - lastUndoSnapshotMs < coalesceWindowMs
                              && ! undoSnapshots.empty();

        if (canCoalesce)
        {
            lastProjectSnapshot = currentSnapshot;
            lastUndoSnapshotMs = now;
            redoSnapshots.clear();
            updateUndoRedoButtons();
            return;
        }

        undoSnapshots.push_back (lastProjectSnapshot);
        constexpr size_t maxUndoSnapshots = 64;
        if (undoSnapshots.size() > maxUndoSnapshots)
            undoSnapshots.erase (undoSnapshots.begin());

        redoSnapshots.clear();
        lastProjectSnapshot = currentSnapshot;
        lastUndoGroup = group;
        lastUndoSnapshotMs = now;
        updateUndoRedoButtons();
    }

    bool applyProjectSnapshotString (const juce::String& snapshot)
    {
        auto parsed = juce::JSON::parse (snapshot);
        if (! parsed.isObject())
            return false;

        auto machineVar = parsed.getProperty ("machine", {});
        if (! machineVar.isObject())
            return false;

        MachineModel loadedMachine ("root", "", false);
        if (! machineFromProjectVar (loadedMachine, machineVar))
            return false;

        const juce::ScopedValueSetter<bool> guard (suppressUndoCapture, true);

        fsmRunning = false;
        stopTransport();
        requestAudioProjectReset();
        prepareQueued = false;
        prepareQueuedStartAfter = false;
        host.pauseMachine();
        host.panic (machine);
        runButton.setButtonText ("Run");

        machine = std::move (loadedMachine);

        const auto rate = static_cast<double> (parsed.getProperty ("rate", rateSlider.getValue()));
        rateSlider.setValue (juce::jlimit (0.2, 4.0, rate), juce::dontSendNotification);
        const auto masterGain = juce::jlimit (0.0, 5.0, static_cast<double> (parsed.getProperty ("masterGain", masterGainSlider.getValue())));
        masterGainSlider.setValue (masterGain, juce::dontSendNotification);
        host.setMasterGain (static_cast<float> (masterGain));
        renderedAudioPlayer.setMasterGain (static_cast<float> (masterGain));
        machineStack.clear();
        activeMachine = &machine;
        inspectedMachine = &machine;
        graph.setMachine (machine);
        graph.setInspectedMachine (&machine);
        rules.setMachine (machine);
        laneMeters.clear();
        invalidatePreparedAudio();
        topStateCountEditor.setText (juce::String (machine.getStateCount()), false);
        refreshControls();
        startPrepareJob (false);
        return true;
    }

    void undoProjectEdit()
    {
        if (undoSnapshots.empty())
            return;

        const auto currentSnapshot = lastProjectSnapshot.isNotEmpty() ? lastProjectSnapshot : makeProjectSnapshotString();
        const auto targetSnapshot = undoSnapshots.back();
        undoSnapshots.pop_back();

        if (! applyProjectSnapshotString (targetSnapshot))
        {
            undoSnapshots.push_back (targetSnapshot);
            updateUndoRedoButtons();
            statusLabel.setText ("Undo failed", juce::dontSendNotification);
            return;
        }

        redoSnapshots.push_back (currentSnapshot);
        lastProjectSnapshot = targetSnapshot;
        dirtyProject = true;
        lastDirtyMs = juce::Time::getMillisecondCounterHiRes();
        saveProjectButton.setButtonText ("Save*");
        updateProjectFileLabel();
        statusLabel.setText ("Undone", juce::dontSendNotification);
        updateUndoRedoButtons();
    }

    void redoProjectEdit()
    {
        if (redoSnapshots.empty())
            return;

        const auto currentSnapshot = lastProjectSnapshot.isNotEmpty() ? lastProjectSnapshot : makeProjectSnapshotString();
        const auto targetSnapshot = redoSnapshots.back();
        redoSnapshots.pop_back();

        if (! applyProjectSnapshotString (targetSnapshot))
        {
            redoSnapshots.push_back (targetSnapshot);
            updateUndoRedoButtons();
            statusLabel.setText ("Redo failed", juce::dontSendNotification);
            return;
        }

        undoSnapshots.push_back (currentSnapshot);
        lastProjectSnapshot = targetSnapshot;
        dirtyProject = true;
        lastDirtyMs = juce::Time::getMillisecondCounterHiRes();
        saveProjectButton.setButtonText ("Save*");
        updateProjectFileLabel();
        statusLabel.setText ("Redone", juce::dontSendNotification);
        updateUndoRedoButtons();
    }

    bool laneFromProjectVar (Lane& lane, const juce::var& value, const MachineModel& model, int stateIndex, int laneIndex) const
    {
        if (! value.isObject())
            return false;

        lane.id = value.getProperty ("id", model.makeLaneId (stateIndex, laneIndex)).toString();
        lane.name = value.getProperty ("name", "Lane " + juce::String (laneIndex + 1)).toString();
        lane.script = value.getProperty ("script", OfDemo::defaultScriptFor (stateIndex, laneIndex)).toString();
        lane.volume = juce::jlimit (0.0f, 1.0f, static_cast<float> (static_cast<double> (value.getProperty ("volume", 1.0))));
        lane.gain = juce::jlimit (0.0f, 2.0f, static_cast<float> (static_cast<double> (value.getProperty ("gain", 1.0))));
        lane.pan = juce::jlimit (-1.0f, 1.0f, static_cast<float> (static_cast<double> (value.getProperty ("pan", 0.0))));
        lane.enabled = static_cast<bool> (value.getProperty ("enabled", true));
        lane.muted = static_cast<bool> (value.getProperty ("muted", false));
        lane.solo = static_cast<bool> (value.getProperty ("solo", false));
        lane.frozen = static_cast<bool> (value.getProperty ("frozen", false));
        lane.freezeStale = static_cast<bool> (value.getProperty ("freezeStale", false));
        lane.frozenAudioPath = resolveProjectMediaPathForLoad (value.getProperty ("frozenAudioPath", {}).toString());
        lane.frozenDurationSeconds = juce::jlimit (0.0, 3600.0, static_cast<double> (value.getProperty ("frozenDurationSeconds", 0.0)));
        lane.sourceScriptPath = resolveProjectMediaPathForLoad (value.getProperty ("sourceScriptPath", {}).toString());
        lane.orbitPhase = juce::jlimit (0.0f, 0.9999f, static_cast<float> (static_cast<double> (value.getProperty ("orbitPhase", 0.0))));
        lane.durationMode = laneDurationModeFromString (value.getProperty ("durationMode", "natural").toString());
        lane.durationValue = juce::jlimit (0.01, 3600.0, static_cast<double> (value.getProperty ("durationValue", 1.0)));
        lane.fadeInSeconds = juce::jlimit (0.0, 3600.0, static_cast<double> (value.getProperty ("fadeInSeconds", 0.0)));
        lane.fadeOutSeconds = juce::jlimit (0.0, 3600.0, static_cast<double> (value.getProperty ("fadeOutSeconds", 0.0)));
        if (lane.frozen && lane.frozenAudioPath.isNotEmpty() && ! juce::File (lane.frozenAudioPath).existsAsFile())
            lane.freezeStale = true;
        lane.freezeInProgress = false;
        lane.playing = false;
        lane.preparedBridge = -1;
        return true;
    }

    bool machineFromProjectVar (MachineModel& model, const juce::var& value)
    {
        if (! value.isObject())
            return false;

        auto* statesArray = value.getProperty ("states", {}).getArray();
        if (statesArray == nullptr || statesArray->isEmpty())
            return false;

        const auto stateCount = juce::jlimit (1, maxStateCount, statesArray->size());
        model.machineId = value.getProperty ("machineId", model.machineId).toString();
        model.lanePrefix = value.getProperty ("lanePrefix", model.lanePrefix).toString();
        model.setStateCount (stateCount);
        model.childMachines.clear();
        model.childMachines.resize (static_cast<size_t> (stateCount));
        model.rules.clear();
        model.orbitConnections.clear();
        model.timingMode = timingModeFromProjectString (value.getProperty ("timingMode", "follow").toString());
        model.parentDivision = juce::jlimit (1, 32, static_cast<int> (value.getProperty ("parentDivision", 1)));
        model.parentTickCounter = 0;
        model.oneShotComplete = false;
        model.latchedActive = false;

        for (int i = 0; i < stateCount; ++i)
        {
            const auto stateVar = statesArray->getReference (i);
            auto& state = model.state (i);
            state.index = i;
            state.name = stateVar.getProperty ("name", "State " + juce::String (i + 1)).toString();
            state.tempoBpm = juce::jlimit (20.0, 320.0, static_cast<double> (stateVar.getProperty ("tempoBpm", 120.0)));
            state.beatsPerBar = juce::jlimit (1, 32, static_cast<int> (stateVar.getProperty ("beatsPerBar", 4)));
            state.beatUnit = juce::jlimit (1, 32, static_cast<int> (stateVar.getProperty ("beatUnit", 4)));
            state.arrangementBars = juce::jlimit (0, 64, static_cast<int> (stateVar.getProperty ("arrangementBars", 1)));
            state.arrangementBeats = juce::jlimit (0, 256, static_cast<int> (stateVar.getProperty ("arrangementBeats", 0)));
            state.durationUsesSeconds = static_cast<bool> (stateVar.getProperty ("durationUsesSeconds", false));
            state.durationSeconds = juce::jlimit (0.25, 3600.0, static_cast<double> (stateVar.getProperty ("durationSeconds", state.secondsPerSection())));
            state.orbitWarp = {};
            if (auto* warpArray = stateVar.getProperty ("orbitWarp", {}).getArray())
            {
                const auto warpCount = juce::jmin (8, warpArray->size());
                for (int warpIndex = 0; warpIndex < warpCount; ++warpIndex)
                    state.orbitWarp[static_cast<size_t> (warpIndex)] = juce::jlimit (-0.32f, 0.42f,
                        static_cast<float> (static_cast<double> (warpArray->getReference (warpIndex))));
            }
            state.lanes.clear();

            if (auto* lanesArray = stateVar.getProperty ("lanes", {}).getArray())
            {
                for (int laneIndex = 0; laneIndex < lanesArray->size(); ++laneIndex)
                {
                    Lane lane;
                    if (laneFromProjectVar (lane, lanesArray->getReference (laneIndex), model, i, laneIndex))
                        state.lanes.push_back (std::move (lane));
                }
            }

            if (state.lanes.empty())
                state.lanes.push_back ({ model.makeLaneId (i, 0), "Lane 1", OfDemo::defaultScriptFor (i, 0) });

            const auto childVar = stateVar.getProperty ("child", {});
            if (childVar.isObject())
            {
                auto childId = childVar.getProperty ("machineId", model.machineId + "_state" + juce::String (i) + "_child").toString();
                auto childPrefix = childVar.getProperty ("lanePrefix", model.lanePrefix + "n" + juce::String (i) + "-").toString();
                auto child = std::make_unique<MachineModel> (childId, childPrefix);
                if (machineFromProjectVar (*child, childVar))
                    model.childMachines[static_cast<size_t> (i)] = std::move (child);
            }
        }

        model.nodeOffsets.resize (static_cast<size_t> (stateCount));
        if (auto* offsetsArray = value.getProperty ("nodeOffsets", {}).getArray())
        {
            const auto count = juce::jmin (stateCount, offsetsArray->size());
            for (int i = 0; i < count; ++i)
            {
                const auto offsetVar = offsetsArray->getReference (i);
                if (! offsetVar.isObject())
                    continue;

                model.nodeOffsets[static_cast<size_t> (i)] = {
                    static_cast<float> (static_cast<double> (offsetVar.getProperty ("x", 0.0))),
                    static_cast<float> (static_cast<double> (offsetVar.getProperty ("y", 0.0)))
                };
            }
        }

        if (auto* rulesArray = value.getProperty ("rules", {}).getArray())
        {
            for (const auto& ruleVar : *rulesArray)
            {
                const auto from = juce::jlimit (0, stateCount - 1, static_cast<int> (ruleVar.getProperty ("from", 0)));
                const auto to = juce::jlimit (0, stateCount - 1, static_cast<int> (ruleVar.getProperty ("to", 0)));
                const auto weight = juce::jmax (0.0f, static_cast<float> (static_cast<double> (ruleVar.getProperty ("weight", 1.0))));
                model.rules.push_back ({ from, to, weight });
            }
        }

        if (model.rules.empty())
            model.regenerateRingRules();

        if (auto* connectionsArray = value.getProperty ("orbitConnections", {}).getArray())
        {
            for (const auto& connectionVar : *connectionsArray)
            {
                const auto sourceState = juce::jlimit (0, stateCount - 1, static_cast<int> (connectionVar.getProperty ("sourceState", 0)));
                const auto sourceLane = juce::jlimit (0, model.getLaneCount (sourceState) - 1, static_cast<int> (connectionVar.getProperty ("sourceLane", 0)));
                const auto targetState = juce::jlimit (0, stateCount - 1, static_cast<int> (connectionVar.getProperty ("targetState", 0)));
                const auto sourcePhase = juce::jlimit (0.0f, 0.9999f,
                                                       static_cast<float> (static_cast<double> (connectionVar.getProperty ("sourcePhase", 0.0))));
                model.orbitConnections.push_back ({ sourceState,
                                                    sourceLane,
                                                    sourcePhase,
                                                    targetState,
                                                    orbitConnectionActionFromString (connectionVar.getProperty ("action", "start").toString()),
                                                    connectionVar.getProperty ("fabricScript", {}).toString() });
            }
        }

        model.selectedState = juce::jlimit (0, stateCount - 1, static_cast<int> (value.getProperty ("selectedState", 0)));
        model.selectedLane = juce::jlimit (0, model.getLaneCount (model.selectedState) - 1, static_cast<int> (value.getProperty ("selectedLane", 0)));
        model.entryState = juce::jlimit (0, stateCount - 1, static_cast<int> (value.getProperty ("entryState", 0)));
        model.stepsSinceEntry = 0;
        return true;
    }

    bool loadProjectFromFile (const juce::File& file, bool rememberProject = true)
    {
        auto parsed = juce::JSON::parse (file);
        if (! parsed.isObject())
            return false;

        auto machineVar = parsed.getProperty ("machine", {});
        if (! machineVar.isObject())
            return false;

        MachineModel loadedMachine ("root", "", false);
        {
            const juce::ScopedValueSetter<juce::File> mediaBase (loadingProjectDirectory, file.getParentDirectory());
            if (! machineFromProjectVar (loadedMachine, machineVar))
                return false;
        }

        fsmRunning = false;
        stopTransport();
        requestAudioProjectReset();
        prepareQueued = false;
        prepareQueuedStartAfter = false;
        host.pauseMachine();
        host.panic (machine);
        runButton.setButtonText ("Run");
        machine = std::move (loadedMachine);

        const auto rate = static_cast<double> (parsed.getProperty ("rate", rateSlider.getValue()));
        rateSlider.setValue (juce::jlimit (0.2, 4.0, rate), juce::dontSendNotification);
        const auto masterGain = juce::jlimit (0.0, 5.0, static_cast<double> (parsed.getProperty ("masterGain", masterGainSlider.getValue())));
        masterGainSlider.setValue (masterGain, juce::dontSendNotification);
        host.setMasterGain (static_cast<float> (masterGain));
        renderedAudioPlayer.setMasterGain (static_cast<float> (masterGain));
        machineStack.clear();
        activeMachine = &machine;
        inspectedMachine = &machine;
        graph.setMachine (machine);
        graph.setInspectedMachine (&machine);
        rules.setMachine (machine);
        laneMeters.clear();
        invalidatePreparedAudio();
        if (rememberProject)
        {
            currentProjectFile = file;
            addRecentProject (file);
            saveAppState();
        }
        refreshProjectMediaStatus (file);
        lastProjectMediaStatus = projectStatusAfterMediaCheck ("Project loaded", cachedProjectMediaStatus);
        topStateCountEditor.setText (juce::String (machine.getStateCount()), false);
        refreshControls();
        if (! loadingProjectInternally)
        {
            auto safeThis = juce::Component::SafePointer<MainComponent> (this);
            juce::Timer::callAfterDelay (250, [safeThis]
            {
                if (safeThis != nullptr && ! safeThis->loadingProjectInternally)
                    safeThis->startPrepareJob (false);
            });
        }
        dirtyProject = false;
        saveProjectButton.setButtonText ("Save");
        updateProjectFileLabel();
        hideWelcomePanel();
        resetUndoHistory();
        return true;
    }

    bool saveProjectToFile (juce::File file)
    {
        if (file.getFileExtension().isEmpty() || file.getFileExtension().compareIgnoreCase (".markovfsm") != 0)
            file = file.withFileExtension (".markovfsm");

        bundleFrozenMediaForProject (machine, file);
        const auto text = juce::JSON::toString (makeProjectVar(), true);
        if (! file.replaceWithText (text))
            return false;

        currentProjectFile = file;
        addRecentProject (file);
        dirtyProject = false;
        saveProjectButton.setButtonText ("Save");
        updateProjectFileLabel();
        lastProjectSnapshot = makeProjectSnapshotString();
        saveAppState();
        refreshProjectMediaStatus (file);
        lastProjectMediaStatus = projectStatusAfterMediaCheck ("Project saved", cachedProjectMediaStatus);
        return true;
    }

    void chooseProjectToSave()
    {
        const auto start = currentProjectFile.existsAsFile()
            ? currentProjectFile
            : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("of.markovfsm");

        projectChooser = std::make_unique<juce::FileChooser> ("Save of Project", start, "*.markovfsm;*.json");
        auto safeThis = juce::Component::SafePointer<MainComponent> (this);
        projectChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                         | juce::FileBrowserComponent::canSelectFiles
                                         | juce::FileBrowserComponent::warnAboutOverwriting,
                                     [safeThis] (const juce::FileChooser& chooser)
                                     {
                                         if (safeThis == nullptr)
                                             return;

                                         auto file = chooser.getResult();
                                         if (file == juce::File())
                                             return;

                                         if (safeThis->saveProjectToFile (file))
                                             safeThis->statusLabel.setText (safeThis->lastProjectMediaStatus, juce::dontSendNotification);
                                         else
                                             safeThis->statusLabel.setText ("Save failed", juce::dontSendNotification);
                                     });
    }

    void chooseProjectToLoad()
    {
        const auto start = currentProjectFile.existsAsFile()
            ? currentProjectFile.getParentDirectory()
            : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);

        projectChooser = std::make_unique<juce::FileChooser> ("Load of Project", start, "*.markovfsm;*.json");
        auto safeThis = juce::Component::SafePointer<MainComponent> (this);
        projectChooser->launchAsync (juce::FileBrowserComponent::openMode
                                         | juce::FileBrowserComponent::canSelectFiles,
                                     [safeThis] (const juce::FileChooser& chooser)
                                     {
                                         if (safeThis == nullptr)
                                             return;

                                         auto file = chooser.getResult();
                                         if (file == juce::File())
                                             return;

                                         if (safeThis->loadProjectFromFile (file))
                                             safeThis->statusLabel.setText (safeThis->lastProjectMediaStatus, juce::dontSendNotification);
                                         else
                                             safeThis->statusLabel.setText ("Load failed", juce::dontSendNotification);
                                     });
    }

    juce::File defaultAudioExportFile() const
    {
        auto base = currentProjectFile.existsAsFile()
            ? currentProjectFile.getSiblingFile (currentProjectFile.getFileNameWithoutExtension() + " Export.wav")
            : juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("of Export.wav");

        return base;
    }

    double exportDurationSeconds() const
    {
        return exportDurationSeconds (exportSettings);
    }

    double exportDurationSeconds (const AudioExportSettings& settings) const
    {
        const auto rate = juce::jmax (0.05, rateSlider.getValue());
        auto musicalSeconds = 0.0;

        if (settings.range == "custom")
        {
            musicalSeconds = settings.customSeconds;
        }
        else if (settings.range == "state")
        {
            musicalSeconds = machine.state (machine.selectedState).secondsPerSection() / rate;
        }
        else
        {
            for (const auto& state : machine.states)
                musicalSeconds += state.secondsPerSection() / rate;

            musicalSeconds *= static_cast<double> (settings.cycles);
        }

        return juce::jlimit (1.0, 1800.0, musicalSeconds + musicalReleaseSeconds + settings.tailSeconds);
    }

    void chooseAudioExportFile()
    {
        projectChooser = std::make_unique<juce::FileChooser> ("Export of Audio", defaultAudioExportFile(), "*.wav");
        auto safeThis = juce::Component::SafePointer<MainComponent> (this);
        projectChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                         | juce::FileBrowserComponent::canSelectFiles
                                         | juce::FileBrowserComponent::warnAboutOverwriting,
                                     [safeThis] (const juce::FileChooser& chooser)
                                     {
                                         if (safeThis == nullptr)
                                             return;

                                         auto file = chooser.getResult();
                                         if (file == juce::File())
                                             return;

                                         if (file.getFileExtension().isEmpty())
                                             file = file.withFileExtension (".wav");

                                         safeThis->beginAudioExport (file);
                                     });
    }

    void beginAudioExport (const juce::File& outputFile)
    {
        if (audioJobRunning.exchange (true))
        {
            statusLabel.setText ("Audio busy", juce::dontSendNotification);
            return;
        }

        fsmRunning = false;
        stopTransport();
        renderedAudioPlayer.stopAll();
        host.pauseMachine();
        host.stopAll (machine);
        runButton.setButtonText ("Run");

        const auto duration = exportDurationSeconds();
        const auto rate = rateSlider.getValue();
        const auto startState = machine.selectedState;
        const auto sampleFormat = exportSettings.sampleFormat;
        const auto path = getSclangPathOverride();
        exportInProgress = true;
        exportCancelRequested = false;
        exportElapsedSeconds = 0.0;
        exportTotalSeconds = duration;
        exportOutputPath = outputFile.getFullPathName();
        statusLabel.setText ("Exporting WAV", juce::dontSendNotification);

        auto safeThis = juce::Component::SafePointer<MainComponent> (this);
        juce::Thread::launch ([safeThis, outputFile, duration, rate, startState, sampleFormat, path]
        {
            if (safeThis == nullptr)
                return;

            const auto ok = safeThis->host.exportMachine (safeThis->machine, path, outputFile, duration, rate, startState, sampleFormat);
            juce::MessageManager::callAsync ([safeThis, ok, outputFile]
            {
                if (safeThis == nullptr)
                    return;

                if (! ok)
                {
                    safeThis->audioJobRunning = false;
                    safeThis->exportInProgress = false;
                    safeThis->exportCancelRequested = false;
                    safeThis->statusLabel.setText ("Audio export failed", juce::dontSendNotification);
                    safeThis->appendLog ("Audio export failed: " + outputFile.getFullPathName());
                }
                else
                {
                    safeThis->appendLog ("Audio export started: " + outputFile.getFullPathName());
                }
            });
        });
    }

    void autosaveIfNeeded (bool force)
    {
        if (! dirtyProject)
            return;

        const auto now = juce::Time::getMillisecondCounterHiRes();
        if (! force && now - lastDirtyMs < 4500.0)
            return;

        auto file = autosaveFile();
        const auto text = juce::JSON::toString (makeProjectVar(), true);
        if (file.replaceWithText (text))
        {
            dirtyProject = false;
            saveProjectButton.setButtonText ("Save");
            updateProjectFileLabel();
            saveAppState();
            if (! force)
                statusLabel.setText ("Autosaved", juce::dontSendNotification);
        }
    }

    void applySchedulerState (const juce::String& machineId, int stateIndex)
    {
        if (machineId != machine.machineId)
            return;

        if (stateIndex < 0 || stateIndex >= machine.getStateCount())
            return;

        logSchedulerDrift (stateIndex);
        setVisualStateImmediate (stateIndex, true);
        updateTransitionPreview();
        deferPostStateUiRefresh();
    }

    void logSchedulerDrift (int confirmedState)
    {
        if (scheduledTransitionTargetMs <= 0.0 || scheduledVisualNextState < 0)
            return;

        const auto now = juce::Time::getMillisecondCounterHiRes();
        const auto driftMs = now - scheduledTransitionTargetMs;
        appendLog ("Transition drift: planned "
                   + (scheduledVisualFromState >= 0 ? machine.state (scheduledVisualFromState).name : juce::String ("?"))
                   + " -> "
                   + (scheduledVisualNextState >= 0 && scheduledVisualNextState < machine.getStateCount()
                        ? machine.state (scheduledVisualNextState).name
                        : juce::String ("?"))
                   + ", confirmed "
                   + (confirmedState >= 0 && confirmedState < machine.getStateCount()
                        ? machine.state (confirmedState).name
                        : juce::String ("?"))
                   + ", drift " + juce::String (driftMs, 1) + " ms");

        scheduledTransitionTargetMs = 0.0;
        scheduledVisualNextState = -1;
        scheduledVisualFromState = -1;
    }

    std::pair<int, float> mostLikelyNextState (const MachineModel& model) const
    {
        const auto selected = model.selectedState;
        auto total = 0.0f;
        auto bestWeight = -1.0f;
        auto bestState = -1;

        for (const auto& rule : model.rules)
        {
            if (rule.from != selected || rule.weight <= 0.0f)
                continue;

            total += rule.weight;
            if (rule.to != selected && rule.weight > bestWeight)
            {
                bestWeight = rule.weight;
                bestState = rule.to;
            }
        }

        if (bestState < 0)
        {
            for (const auto& rule : model.rules)
            {
                if (rule.from == selected && rule.weight > bestWeight)
                {
                    bestWeight = rule.weight;
                    bestState = rule.to;
                }
            }
        }

        if (bestState < 0 || total <= 0.0f)
            return { -1, 0.0f };

        return { bestState, bestWeight / total };
    }

    void setVisualStateImmediate (int stateIndex, bool fromScheduler)
    {
        if (stateIndex < 0 || stateIndex >= machine.getStateCount())
            return;

        machine.selectedState = stateIndex;
        machine.selectedLane = juce::jlimit (0, machine.getLaneCount (stateIndex) - 1, 0);
        arrangementStrip.setPlaybackState (machine.machineId, stateIndex);
        scheduleNextVisualBoundary();

        if (fromScheduler)
            lastSchedulerStateMs = juce::Time::getMillisecondCounterHiRes();

        graph.repaint();
        arrangementStrip.repaint();
        stateTabs.repaint();
    }

    void scheduleNextVisualBoundary()
    {
        if (! fsmRunning || machine.selectedState < 0 || machine.selectedState >= machine.getStateCount())
        {
            visualNextStateMs = 0.0;
            return;
        }

        const auto durationMs = machine.state (machine.selectedState).secondsPerSection()
                              * 1000.0 / juce::jmax (0.05, rateSlider.getValue());
        visualNextStateMs = juce::Time::getMillisecondCounterHiRes() + juce::jmax (40.0, durationMs);
    }

    void tickVisualScheduler (double now)
    {
        if (! fsmRunning || visualNextStateMs <= 0.0 || now < visualNextStateMs)
            return;

        const auto nextState = scheduledVisualNextState >= 0 ? scheduledVisualNextState
                                                             : (machine.selectedState + 1) % machine.getStateCount();
        setVisualStateImmediate (nextState, false);
        updateTransitionPreview();
    }

    void deferPostStateUiRefresh()
    {
        if (deferredStateRefreshPending)
            return;

        deferredStateRefreshPending = true;
        juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer<MainComponent> (this)]
        {
            if (safeThis == nullptr)
                return;

            safeThis->deferredStateRefreshPending = false;
            safeThis->primeMetersForActiveState (safeThis->machine);
            safeThis->refreshControls();
        });
    }

    void updateTransitionPreview()
    {
        if (scheduledVisualNextState >= 0)
        {
            graph.setTransitionPreview (scheduledVisualNextState, 1.0f);
            return;
        }

        const auto [stateIndex, probability] = mostLikelyNextState (currentMachine());
        graph.setTransitionPreview (stateIndex, probability);
    }

    int extractErrorLineNumber (const juce::String& text) const
    {
        const auto lower = text.toLowerCase();
        auto index = lower.indexOf ("line ");
        if (index < 0)
            index = lower.indexOf ("line:");

        if (index < 0)
            return -1;

        index += 4;
        while (index < text.length() && ! juce::CharacterFunctions::isDigit (text[index]))
            ++index;

        juce::String digits;
        while (index < text.length() && juce::CharacterFunctions::isDigit (text[index]))
            digits << juce::String::charToString (text[index++]);

        return digits.getIntValue();
    }

    void highlightCodeLine (int oneBasedLine)
    {
        const auto line = juce::jlimit (0, codeDocument.getNumLines() - 1, oneBasedLine - 1);
        const juce::CodeDocument::Position start (codeDocument, line, 0);
        const juce::CodeDocument::Position end (codeDocument, line, codeDocument.getLine (line).length());
        scriptEditor.selectRegion (start, end);
        scriptEditor.scrollToLine (juce::jmax (0, line - 2));
    }

    juce::String tidyScriptText (const juce::String& source) const
    {
        juce::StringArray lines;
        lines.addLines (source);

        juce::StringArray tidied;
        int indent = 0;

        for (auto line : lines)
        {
            auto trimmed = line.trim();
            if (trimmed.isEmpty())
            {
                tidied.add ({});
                continue;
            }

            if (trimmed.startsWithChar ('}') || trimmed.startsWithChar (')') || trimmed.startsWithChar (']'))
                indent = juce::jmax (0, indent - 1);

            tidied.add (juce::String::repeatedString ("    ", indent) + trimmed);

            const auto opensBlock = trimmed.endsWithChar ('{')
                                  || trimmed.endsWithChar ('(')
                                  || trimmed.endsWithChar ('[')
                                  || trimmed.endsWith ("|");
            const auto closesBlock = trimmed.endsWithChar ('}')
                                  || trimmed.endsWithChar (')')
                                  || trimmed.endsWithChar (']');

            if (opensBlock && ! closesBlock)
                ++indent;
        }

        return tidied.joinIntoString ("\n") + "\n";
    }

    void tidySelectedLaneScript()
    {
        auto& lane = currentInspectorMachine().selectedLaneRef();
        const auto tidied = tidyScriptText (codeDocument.getAllContent());
        lane.script = tidied;
        lane.preparedBridge = -1;
        if (lane.frozen)
            lane.freezeStale = true;
        loadingCodeDocument = true;
        scriptEditor.loadContent (tidied);
        loadingCodeDocument = false;
        updateCodeStats();
        markMachineDirty (UndoGroup::text);
    }

    void codeDocumentTextInserted (const juce::String&, int) override
    {
        codeDocumentChanged();
    }

    void codeDocumentTextDeleted (int, int) override
    {
        codeDocumentChanged();
    }

    void codeDocumentChanged()
    {
        if (loadingCodeDocument)
            return;

        auto& lane = currentInspectorMachine().selectedLaneRef();
        lane.script = codeDocument.getAllContent();
        lane.preparedBridge = -1;
        if (lane.frozen)
            lane.freezeStale = true;
        if (pendingCheckId.isEmpty())
            setCodeCheckStatus ("Modified", mutedInk());
        updateCodeStats();
        markMachineDirty (UndoGroup::text);
    }

    MachineModel& currentMachine() const
    {
        return *activeMachine;
    }

    MachineModel& currentInspectorMachine() const
    {
        return inspectedMachine != nullptr ? *inspectedMachine : *activeMachine;
    }

    bool buildMachinePath (MachineModel* model, MachineModel* target, std::vector<MachineModel*>& parents)
    {
        if (model == nullptr)
            return false;

        if (model == target)
            return true;

        for (int i = 0; i < model->getStateCount(); ++i)
        {
            if (auto* child = model->childMachine (i))
            {
                parents.push_back (model);
                if (buildMachinePath (child, target, parents))
                    return true;
                parents.pop_back();
            }
        }

        return false;
    }

    void navigateToMachineState (MachineModel* targetMachine, int stateIndex)
    {
        if (targetMachine == nullptr)
            return;

        targetMachine->selectedState = juce::jlimit (0, targetMachine->getStateCount() - 1, stateIndex);
        targetMachine->selectedLane = juce::jlimit (0, targetMachine->getLaneCount (targetMachine->selectedState) - 1,
                                                   targetMachine->selectedLane);

        std::vector<MachineModel*> parents;
        if (! buildMachinePath (&machine, targetMachine, parents))
            return;

        machineStack = parents;
        setActiveMachine (*targetMachine);
    }

    MachineModel* selectedNestedMachine() const
    {
        return currentInspectorMachine().childMachine (currentInspectorMachine().selectedState);
    }

    juce::String makeBreadcrumb() const
    {
        juce::StringArray parts;
        parts.add ("Top FSM");
        addBreadcrumbParts (&machine, inspectedMachine, parts);
        parts.add (currentInspectorMachine().state (currentInspectorMachine().selectedState).name);
        return parts.joinIntoString (" / ");
    }

    bool addBreadcrumbParts (const MachineModel* model, const MachineModel* target, juce::StringArray& parts) const
    {
        if (model == target)
            return true;

        for (int i = 0; i < model->getStateCount(); ++i)
        {
            if (auto* child = model->childMachine (i))
            {
                parts.add (model->state (i).name + " FSM");
                if (addBreadcrumbParts (child, target, parts))
                    return true;
                parts.remove (parts.size() - 1);
            }
        }

        return false;
    }

    juce::String makeStateSummary() const
    {
        const auto& inspected = currentInspectorMachine();
        const auto& s = inspected.state (inspected.selectedState);
        const auto laneCount = static_cast<int> (s.lanes.size());
        auto activeText = (&inspected == activeMachine) ? "active" : "inspecting";
        const auto nestedText = inspected.hasChildMachine (inspected.selectedState) ? "nested FSM" : "no nested FSM";
        const auto durationText = s.durationUsesSeconds
            ? juce::String (s.durationSeconds, 1) + " sec"
            : juce::String (s.arrangementBars) + "b " + juce::String (s.arrangementBeats) + "bt";
        return s.name + "  |  " + juce::String (laneCount) + (laneCount == 1 ? " track" : " tracks")
             + "  |  " + juce::String (s.tempoBpm, 1) + " BPM"
             + "  |  " + juce::String (s.beatsPerBar) + "/" + juce::String (s.beatUnit)
             + "  |  " + durationText
             + "  |  " + activeText + "  |  " + nestedText;
    }

    juce::String getSclangPathOverride() const
    {
        return {};
    }

    juce::File freezeFileForLane (const Lane& lane) const
    {
        auto safeId = lane.id.retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_");
        auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                       .getChildFile ("of")
                       .getChildFile ("freezes");
        dir.createDirectory();
        return dir.getChildFile (safeId + ".wav");
    }

    Lane* findLaneById (MachineModel& model, const juce::String& laneId)
    {
        for (auto& state : model.states)
        {
            for (auto& lane : state.lanes)
                if (lane.id == laneId)
                    return &lane;

            if (auto* child = model.childMachine (state.index))
                if (auto* found = findLaneById (*child, laneId))
                    return found;
        }

        return nullptr;
    }

    Lane* findLaneByIdInState (State& state, const juce::String& laneId)
    {
        for (auto& lane : state.lanes)
            if (lane.id == laneId)
                return &lane;

        return nullptr;
    }

    void promptOrbitConnectionAction (int sourceState, int sourceLane, float sourcePhase, int targetState)
    {
        if (sourceState < 0 || sourceState >= machine.getStateCount()
            || targetState < 0 || targetState >= machine.getStateCount()
            || sourceLane < 0 || sourceLane >= machine.getLaneCount (sourceState))
            return;

        juce::PopupMenu menu;
        menu.addItem (1, "Start target");
        menu.addItem (2, "Pause target");
        menu.addItem (3, "Restart target");
        menu.addItem (4, "Reverse target");
        menu.addSeparator();
        menu.addItem (5, "Programmable Fabric script");

        menu.showMenuAsync (juce::PopupMenu::Options(),
                            [safeThis = juce::Component::SafePointer<MainComponent> (this),
                             sourceState, sourceLane, sourcePhase, targetState] (int result)
        {
            if (safeThis == nullptr || result == 0)
                return;

            auto action = OrbitConnectionAction::start;
            if (result == 2)
                action = OrbitConnectionAction::pause;
            else if (result == 3)
                action = OrbitConnectionAction::restart;
            else if (result == 4)
                action = OrbitConnectionAction::reverse;
            else if (result == 5)
                action = OrbitConnectionAction::programmable;

            safeThis->createOrbitConnection (sourceState, sourceLane, sourcePhase, targetState, action);
        });
    }

    void createOrbitConnection (int sourceState,
                                int sourceLane,
                                float sourcePhase,
                                int targetState,
                                OrbitConnectionAction action)
    {
        OrbitConnection connection;
        connection.sourceState = juce::jlimit (0, machine.getStateCount() - 1, sourceState);
        connection.sourceLane = juce::jlimit (0, machine.getLaneCount (connection.sourceState) - 1, sourceLane);
        connection.sourcePhase = juce::jlimit (0.0f, 0.9999f, sourcePhase);
        connection.targetState = juce::jlimit (0, machine.getStateCount() - 1, targetState);
        connection.action = action;
        if (action == OrbitConnectionAction::programmable)
            connection.fabricScript = "// Fabric script hook\n"
                                      "target.start();";

        machine.orbitConnections.push_back (std::move (connection));
        previousOrbitConnectionPhases.assign (machine.orbitConnections.size(), 0.0f);
        statusLabel.setText ("Connection: "
                             + machine.state (sourceState).name + " -> "
                             + machine.state (targetState).name + " / "
                             + orbitConnectionActionLabel (action),
                             juce::dontSendNotification);
        markMachineDirty();
        orbitCanvas.repaint();
        refreshControls();
    }

    void commitNestedDivisionEditor()
    {
        if (auto* child = selectedNestedMachine())
        {
            child->parentDivision = juce::jlimit (1, 16, nestedDivisionEditor.getText().getIntValue());
            child->parentTickCounter = 0;
            markMachineDirty();
            refreshControls();
        }
    }

    void adjustNestedDivision (int delta)
    {
        if (auto* child = selectedNestedMachine())
        {
            child->parentDivision = juce::jlimit (1, 16, child->parentDivision + delta);
            child->parentTickCounter = 0;
            markMachineDirty();
            refreshControls();
        }
    }

    void commitStateTimingEditors()
    {
        auto& inspected = currentInspectorMachine();
        auto& state = inspected.state (inspected.selectedState);
        auto bpm = stateTempoEditor.getText().getDoubleValue();
        auto beats = stateMeterBeatsEditor.getText().getIntValue();
        auto unit = stateMeterUnitEditor.getText().getIntValue();
        auto durationBars = stateDurationBarsEditor.getText().getIntValue();
        auto durationBeats = stateDurationBeatsEditor.getText().getIntValue();
        auto durationSeconds = stateDurationSecondsEditor.getText().getDoubleValue();

        const auto oldTempo = state.tempoBpm;
        const auto oldBeats = state.beatsPerBar;
        const auto oldUnit = state.beatUnit;
        const auto oldArrangementBars = state.arrangementBars;
        const auto oldArrangementBeats = state.arrangementBeats;
        const auto oldDurationUsesSeconds = state.durationUsesSeconds;
        const auto oldDurationSeconds = state.durationSeconds;
        const auto newTempo = juce::jlimit (20.0, 320.0, bpm <= 0.0 ? state.tempoBpm : bpm);
        const auto newBeats = juce::jlimit (1, 32, beats <= 0 ? state.beatsPerBar : beats);
        const auto newUnit = juce::jlimit (1, 32, unit <= 0 ? state.beatUnit : unit);

        state.tempoBpm = newTempo;
        state.beatsPerBar = newBeats;
        state.beatUnit = newUnit;
        state.arrangementBars = juce::jlimit (0, 64, durationBars < 0 ? state.arrangementBars : durationBars);
        state.arrangementBeats = juce::jlimit (0, 256, durationBeats < 0 ? state.arrangementBeats : durationBeats);
        state.durationSeconds = juce::jlimit (0.25, 3600.0, durationSeconds <= 0.0 ? state.durationSeconds : durationSeconds);
        if (! state.durationUsesSeconds && state.arrangementBars == 0 && state.arrangementBeats == 0)
            state.arrangementBars = 1;

        const auto timingChanged = std::abs (oldTempo - state.tempoBpm) > 0.001
                                || oldBeats != state.beatsPerBar
                                || oldUnit != state.beatUnit
                                || oldArrangementBars != state.arrangementBars
                                || oldArrangementBeats != state.arrangementBeats
                                || oldDurationUsesSeconds != state.durationUsesSeconds
                                || std::abs (oldDurationSeconds - state.durationSeconds) > 0.001;
        transportIntervalMs = getTransportIntervalMs();

        if (timingChanged)
        {
            const auto refreezeCount = refreezeRenderedLanesInState (inspected, inspected.selectedState);
            markMachineDirty();
            if (refreezeCount > 0)
                statusLabel.setText ("Re-rendering " + juce::String (refreezeCount) + " lane" + (refreezeCount == 1 ? "" : "s"), juce::dontSendNotification);
            else
                statusLabel.setText ("Timing updated", juce::dontSendNotification);
        }

        refreshControls();
    }

    void setActiveMachine (MachineModel& newMachine)
    {
        activeMachine = &newMachine;
        inspectedMachine = &newMachine;
        graph.setMachine (newMachine);
        graph.setInspectedMachine (&newMachine);
        rules.setMachine (newMachine);
        topStateCountEditor.setText (juce::String (machine.getStateCount()), false);
        refreshControls();
    }

    void commitTopLevelStateCountEditor()
    {
        setTopLevelStateCount (topStateCountEditor.getText().getIntValue());
    }

    void setTopLevelStateCount (int newCount)
    {
        newCount = juce::jlimit (1, maxStateCount, newCount);
        topStateCountEditor.setText (juce::String (newCount), false);

        if (newCount == machine.getStateCount())
            return;

        fsmRunning = false;
        stopTransport();
        renderedAudioPlayer.stopAll();
        host.stopAll (machine);
        runButton.setButtonText ("Run");

        machineStack.clear();
        activeMachine = &machine;
        machine.setStateCount (newCount);
        graph.setMachine (machine);
        graph.setInspectedMachine (&machine);
        rules.setMachine (machine);
        markMachineDirty();
        refreshControls();
    }

    std::vector<LaneSnapshot> makeLaneSnapshots() const
    {
        std::vector<LaneSnapshot> lanes;
        lanes.reserve (64);
        collectLaneSnapshots (machine, lanes);

        return lanes;
    }

    void collectLaneSnapshots (const MachineModel& model, std::vector<LaneSnapshot>& lanes) const
    {
        for (const auto& state : model.states)
        {
            for (const auto& lane : state.lanes)
                lanes.push_back ({ lane.id, lane.name, lane.script, lane.volume, lane.gain, lane.pan, lane.frozen, lane.freezeStale, lane.frozenAudioPath });

            if (auto* child = model.childMachine (state.index))
                collectLaneSnapshots (*child, lanes);
        }
    }

    void markPreparedLanes (const std::vector<LaneSnapshot>& lanes, int bridge)
    {
        std::unordered_set<std::string> preparedIds;
        preparedIds.reserve (lanes.size());
        for (const auto& lane : lanes)
            preparedIds.insert (lane.id.toStdString());

        markPreparedLanesInMachine (machine, preparedIds, bridge);
    }

    void markPreparedLanesInMachine (MachineModel& model, const std::unordered_set<std::string>& preparedIds, int bridge)
    {
        for (auto& state : model.states)
        {
            for (auto& lane : state.lanes)
                if (preparedIds.find (lane.id.toStdString()) != preparedIds.end())
                    lane.preparedBridge = bridge;

            if (auto* child = model.childMachine (state.index))
                markPreparedLanesInMachine (*child, preparedIds, bridge);
        }
    }

    void markMachineDirty (UndoGroup group = UndoGroup::structural)
    {
        recordUndoSnapshotAfterMutation (group);
        invalidatePreparedAudio();
        dirtyProject = true;
        lastDirtyMs = juce::Time::getMillisecondCounterHiRes();
        saveProjectButton.setButtonText ("Save*");
        updateProjectFileLabel();
        statusLabel.setText ("Edited", juce::dontSendNotification);
    }

    void markProjectLayoutDirty()
    {
        recordUndoSnapshotAfterMutation (UndoGroup::continuous);
        dirtyProject = true;
        lastDirtyMs = juce::Time::getMillisecondCounterHiRes();
        saveProjectButton.setButtonText ("Save*");
        updateProjectFileLabel();
        statusLabel.setText ("Layout edited", juce::dontSendNotification);
    }

    void startPreparedRun()
    {
        runButton.setButtonText ("Pause");
        stopTransport();
        machine.entryState = 0;
        machine.selectedState = 0;
        visualNextStateMs = 0.0;
        scheduledTransitionTargetMs = 0.0;
        scheduledVisualNextState = -1;
        graph.clearTimingPulse();
        arrangementStrip.clearTimingPulse();
        orbitCanvas.resetTransportStart();
        orbitCanvas.clearReversePlayheads();
        orbitConnectionTransportStartMs = juce::Time::getMillisecondCounterHiRes();
        previousOrbitConnectionPhases.assign (machine.orbitConnections.size(), 0.0f);
        host.configureMachine (machine);
        applyAllMixToHost();
        primeMetersForAllPlayableLanes (machine);
        setVisualStateImmediate (0, false);
        ++playbackGeneration;
        renderedLaneLoopVersions.clear();
        scheduleAllTracksFromBarZero (playbackGeneration);
        refreshControls();
    }

    void startPrepareJob (bool startAfterPrepare)
    {
        if (audioJobRunning.exchange (true))
        {
            prepareQueued = true;
            prepareQueuedStartAfter = prepareQueuedStartAfter || startAfterPrepare;
            return;
        }

        prepareQueued = false;
        prepareQueuedStartAfter = false;
        runButton.setButtonText ("Run");
        statusLabel.setText ("Booting audio", juce::dontSendNotification);

        auto lanes = makeLaneSnapshots();
        auto path = getSclangPathOverride();
        const auto prepareRevision = audioPrepareRevision.load();
        const auto resetBeforePrepare = resetAudioBeforePrepare.exchange (false);
        auto safeThis = juce::Component::SafePointer<MainComponent> (this);

        juce::Thread::launch ([safeThis, lanes, path, startAfterPrepare, prepareRevision, resetBeforePrepare]
        {
            if (safeThis == nullptr)
                return;

            int preparedBridge = -1;
            if (resetBeforePrepare)
                safeThis->host.resetProjectState (path);

            for (const auto& lane : lanes)
            {
                if (safeThis == nullptr)
                    return;

                if (prepareRevision != safeThis->audioPrepareRevision.load())
                {
                    safeThis->resetAudioBeforePrepare = true;
                    preparedBridge = -2;
                    break;
                }

                preparedBridge = safeThis->host.prepareData (lane, path);
                if (preparedBridge < 0)
                    break;
            }

            juce::MessageManager::callAsync ([safeThis, lanes, preparedBridge, startAfterPrepare, prepareRevision]
            {
                if (safeThis == nullptr)
                    return;

                safeThis->audioJobRunning = false;
                const auto launchQueuedPrepare = [safeThis]
                {
                    if (safeThis == nullptr || ! safeThis->prepareQueued)
                        return false;

                    const auto queuedStartAfter = safeThis->prepareQueuedStartAfter;
                    safeThis->prepareQueued = false;
                    safeThis->prepareQueuedStartAfter = false;
                    safeThis->startPrepareJob (queuedStartAfter);
                    return true;
                };

                if (prepareRevision != safeThis->audioPrepareRevision.load())
                {
                    safeThis->machinePrepared = false;
                    if (! launchQueuedPrepare())
                        safeThis->runButton.setButtonText ("Run");
                    safeThis->refreshControls();
                    return;
                }

                if (preparedBridge >= 0)
                {
                    safeThis->markPreparedLanes (lanes, preparedBridge);
                    safeThis->host.configureMachine (safeThis->machine);
                    safeThis->applyAllMixToHost();
                    safeThis->machinePrepared = true;

                    if (startAfterPrepare && safeThis->fsmRunning)
                    {
                        safeThis->startPreparedRun();
                    }
                    else
                    {
                        safeThis->runButton.setButtonText ("Run");
                    }

                    launchQueuedPrepare();
                }
                else
                {
                    safeThis->fsmRunning = false;
                    safeThis->runButton.setButtonText ("Run");
                    launchQueuedPrepare();
                }

                safeThis->refreshControls();
            });
        });
    }

    void appendLog (const juce::String& message)
    {
        scLog << juce::Time::getCurrentTime().toString (true, true, true, true) << "  " << message << "\n";

        constexpr int maxLogChars = 24000;
        if (scLog.length() > maxLogChars)
            scLog = scLog.substring (scLog.length() - maxLogChars);

        logDirty = true;
        flushLogViewIfNeeded (juce::Time::getMillisecondCounterHiRes(), false);
    }

    void flushLogViewIfNeeded (double now, bool immediateIfVisible)
    {
        if (! logDirty)
            return;

        if (! logVisible)
            return;

        if (! immediateIfVisible && now - lastLogFlushMs < 180.0)
            return;

        lastLogFlushMs = now;
        logDirty = false;
        logView.setText (scLog, juce::dontSendNotification);
        logView.moveCaretToEnd();
    }

    int getTransportIntervalMs() const
    {
        const auto& state = machine.state (machine.selectedState);
        const auto rate = juce::jmax (0.05, rateSlider.getValue());
        return juce::jlimit (80, 120000, static_cast<int> (state.secondsPerSection() * 1000.0 / rate));
    }

    double getTransportRateHz() const
    {
        return 1000.0 / static_cast<double> (getTransportIntervalMs());
    }

    int getTransportLookaheadMs() const
    {
        return juce::jlimit (30, 180, transportIntervalMs.load() / 3);
    }

    void startTransport()
    {
        stopTransport();
        transportShouldRun = true;
        ++transportCallbackGeneration;

        auto safeThis = juce::Component::SafePointer<MainComponent> (this);
        transportIntervalMs = getTransportIntervalMs();
        transportThread = std::thread ([safeThis]
        {
            auto nextTick = std::chrono::steady_clock::now()
                          + std::chrono::milliseconds (safeThis != nullptr ? safeThis->transportIntervalMs.load() : 1000);

            while (safeThis != nullptr && safeThis->transportShouldRun)
            {
                const auto lookaheadMs = safeThis != nullptr ? safeThis->getTransportLookaheadMs() : 40;
                const auto scheduleTime = nextTick - std::chrono::milliseconds (lookaheadMs);

                {
                    std::unique_lock<std::mutex> lock (safeThis->transportMutex);
                    if (safeThis->transportCv.wait_until (lock, scheduleTime, [safeThis]
                    {
                        return safeThis == nullptr || ! safeThis->transportShouldRun.load();
                    }))
                    {
                        break;
                    }
                }

                if (safeThis == nullptr || ! safeThis->transportShouldRun)
                    break;

                auto result = std::make_shared<std::promise<int>>();
                auto future = result->get_future();
                const auto tickId = ++safeThis->transportCallbackGeneration;
                const auto targetTick = nextTick;

                juce::MessageManager::callAsync ([safeThis, result, tickId, targetTick]
                {
                    auto nextInterval = safeThis != nullptr ? safeThis->transportIntervalMs.load() : 1000;

                    if (safeThis != nullptr
                        && safeThis->transportShouldRun
                        && safeThis->transportCallbackGeneration.load() == tickId)
                    {
                        const auto remaining = std::chrono::duration_cast<std::chrono::microseconds> (targetTick - std::chrono::steady_clock::now()).count();
                        const auto delaySeconds = juce::jlimit (0.0, 0.5, static_cast<double> (remaining) / 1000000.0);
                        nextInterval = safeThis->advanceStateVisualOnly (delaySeconds);
                    }

                    result->set_value (nextInterval);
                });

                auto nextInterval = safeThis->transportIntervalMs.load();
                if (future.wait_for (std::chrono::milliseconds (750)) == std::future_status::ready)
                    nextInterval = future.get();

                if (safeThis != nullptr)
                    safeThis->transportIntervalMs = nextInterval;
                nextTick += std::chrono::milliseconds (nextInterval);

                const auto now = std::chrono::steady_clock::now();
                if (nextTick <= now)
                    nextTick = now + std::chrono::milliseconds (nextInterval);
            }
        });
    }

    void stopTransport()
    {
        transportShouldRun = false;
        ++transportCallbackGeneration;
        transportCv.notify_all();
        if (transportThread.joinable())
        {
            if (transportThread.get_id() != std::this_thread::get_id())
                transportThread.join();
            else
                transportThread.detach();
        }
    }

    void restartTransport()
    {
        if (! fsmRunning)
            return;

        startTransport();
    }

    void playState (int stateIndex)
    {
        auto& s = currentMachine().state (stateIndex);
        for (auto& lane : s.lanes)
            if (shouldPlayLane (s, lane))
                host.play (lane, getSclangPathOverride());
    }

    void prepareAllLanes()
    {
        for (auto& state : currentMachine().states)
            for (auto& lane : state.lanes)
                host.prepare (lane, getSclangPathOverride());
    }

    void invalidatePreparedAudio()
    {
        ++audioPrepareRevision;
        machinePrepared = false;
    }

    void requestAudioProjectReset()
    {
        resetAudioBeforePrepare = true;
        invalidatePreparedAudio();
    }

    void stopState (int stateIndex)
    {
        for (auto& lane : currentMachine().state (stateIndex).lanes)
            host.stop (lane);
    }

    int advanceStateVisualOnly (double audioDelaySeconds = 0.0)
    {
        scheduledTransitionDelaySeconds = audioDelaySeconds;
        advanceMachineTree (machine);
        scheduledTransitionDelaySeconds = 0.0;
        const auto nextInterval = getTransportIntervalMs();
        transportIntervalMs = nextInterval;
        refreshControls();
        return nextInterval;
    }

    int chooseNextState (const MachineModel& model) const
    {
        float total = 0.0f;
        auto lastCandidate = model.selectedState;

        for (const auto& rule : model.rules)
        {
            const auto weight = juce::jmax (0.0f, rule.weight);
            if (rule.from == model.selectedState && weight > 0.0f)
            {
                total += weight;
                lastCandidate = rule.to;
            }
        }

        if (total <= 0.0f)
            return (model.selectedState + 1) % model.getStateCount();

        auto pick = juce::Random::getSystemRandom().nextFloat() * total;
        for (const auto& rule : model.rules)
        {
            const auto weight = juce::jmax (0.0f, rule.weight);
            if (rule.from == model.selectedState && weight > 0.0f)
            {
                pick -= weight;
                if (pick <= 0.0f)
                    return rule.to;
            }
        }

        return lastCandidate;
    }

    void startMachine (MachineModel& model, int stateIndex)
    {
        model.entryState = juce::jlimit (0, model.getStateCount() - 1, stateIndex);
        model.stepsSinceEntry = 0;
        model.parentTickCounter = 0;
        model.oneShotComplete = false;
        model.latchedActive = true;
        enterState (model, model.entryState, true);
    }

    bool advanceMachineTree (MachineModel& model)
    {
        if (auto* child = model.childMachine (model.selectedState))
        {
            if (child->timingMode == NestedTimingMode::oneShot && ! child->oneShotComplete)
            {
                advanceSelectedChildMachine (model, true);
                ++model.stepsSinceEntry;
                return false;
            }
        }

        const auto nextState = chooseNextState (model);
        const auto parentIsHolding = nextState == model.selectedState;

        if (parentIsHolding)
            if (! advanceSelectedChildMachine (model, true))
                return false;

        enterState (model, nextState);
        ++model.stepsSinceEntry;

        return model.stepsSinceEntry > 0 && model.selectedState == model.entryState;
    }

    void enterState (MachineModel& model, int stateIndex, bool forceStart = false)
    {
        const auto previousState = model.selectedState;
        const auto changingState = previousState != stateIndex;

        if (! changingState && ! forceStart)
        {
            model.selectedLane = juce::jlimit (0, model.getLaneCount (model.selectedState) - 1, model.selectedLane);
            return;
        }

        std::vector<Lane*> lanesToStop;
        std::vector<Lane*> lanesToStart;

        if (changingState && previousState >= 0 && previousState < model.getStateCount())
        {
            for (auto& lane : model.state (previousState).lanes)
                lanesToStop.push_back (&lane);

            if (auto* child = model.childMachine (previousState))
                if (child->timingMode != NestedTimingMode::latch)
                    collectStopMachineRecursive (*child, lanesToStop);
        }

        model.selectedState = stateIndex;
        model.selectedLane = 0;

        auto& state = model.state (stateIndex);
        for (auto& lane : state.lanes)
            if (shouldPlayLane (state, lane))
                lanesToStart.push_back (&lane);

        host.transition (lanesToStop, lanesToStart, getSclangPathOverride(), musicalReleaseSeconds, scheduledTransitionDelaySeconds);

        if (auto* child = model.childMachine (stateIndex))
            startChildMachineForParentState (*child);
    }

    void startChildMachineForParentState (MachineModel& child)
    {
        child.parentTickCounter = 0;
        child.oneShotComplete = false;

        if (child.timingMode != NestedTimingMode::latch || ! child.latchedActive)
            startMachine (child, child.entryState);
        else if (child.timingMode == NestedTimingMode::latch)
            child.latchedActive = true;
    }

    bool advanceSelectedChildMachine (MachineModel& parent, bool parentIsHolding)
    {
        auto* child = parent.childMachine (parent.selectedState);
        if (child == nullptr)
            return true;

        ++child->parentTickCounter;
        if (child->parentTickCounter < child->parentDivision)
            return true;

        child->parentTickCounter = 0;

        switch (child->timingMode)
        {
            case NestedTimingMode::followParent:
                advanceMachineTree (*child);
                return true;

            case NestedTimingMode::freeRun:
                advanceMachineTree (*child);
                return true;

            case NestedTimingMode::oneShot:
                if (! child->oneShotComplete)
                {
                    child->oneShotComplete = advanceMachineTree (*child);
                    if (child->oneShotComplete)
                        stopMachineRecursive (*child);
                }
                return child->oneShotComplete || parentIsHolding;

            case NestedTimingMode::latch:
                advanceMachineTree (*child);
                return true;
        }

        return true;
    }

    void stopMachineRecursive (MachineModel& model)
    {
        model.latchedActive = false;
        model.oneShotComplete = false;
        for (auto& state : model.states)
        {
            for (auto& lane : state.lanes)
            {
                renderedAudioPlayer.stopLane (lane.id);
                host.stop (lane);
            }

            if (auto* child = model.childMachine (state.index))
                stopMachineRecursive (*child);
        }
    }

    void collectStopMachineRecursive (MachineModel& model, std::vector<Lane*>& lanesToStop)
    {
        model.latchedActive = false;
        model.oneShotComplete = false;

        for (auto& state : model.states)
        {
            for (auto& lane : state.lanes)
                lanesToStop.push_back (&lane);

            if (auto* child = model.childMachine (state.index))
                collectStopMachineRecursive (*child, lanesToStop);
        }
    }

    bool shouldPlayLane (const State& state, const Lane& lane) const
    {
        if (! lane.enabled || lane.muted)
            return false;

        const auto anySolo = std::any_of (state.lanes.begin(), state.lanes.end(), [] (const Lane& l)
        {
            return l.enabled && l.solo;
        });

        return ! anySolo || lane.solo;
    }

    float effectiveLaneVolume (const State& state, const Lane& lane) const
    {
        return shouldPlayLane (state, lane) ? juce::jlimit (0.0f, 2.0f, lane.volume * lane.gain) : 0.0f;
    }

    void applyMixToHostRecursive (MachineModel& model)
    {
        for (auto& state : model.states)
        {
            for (const auto& lane : state.lanes)
            {
                const auto volume = effectiveLaneVolume (state, lane);
                host.setLaneEffectiveMix (lane, volume);
                renderedAudioPlayer.setLaneMix (lane.id, volume, lane.pan);
            }

            if (auto* child = model.childMachine (state.index))
                applyMixToHostRecursive (*child);
        }
    }

    void applyAllMixToHost()
    {
        applyMixToHostRecursive (machine);
    }

    void primeMeterForLane (const Lane& lane)
    {
        auto& meter = laneMeters[lane.id.toStdString()];
        meter.rms = juce::jmax (meter.rms, 0.030f);
        meter.peak = juce::jmax (meter.peak, 0.070f);
        meter.lastSeenMs = juce::Time::getMillisecondCounterHiRes();
        meter.provisional = true;
    }

    void primeMetersForActiveState (MachineModel& model)
    {
        auto& state = model.state (model.selectedState);
        for (const auto& lane : state.lanes)
            if (shouldPlayLane (state, lane))
                primeMeterForLane (lane);

        if (auto* child = model.childMachine (model.selectedState))
        {
            child->selectedState = juce::jlimit (0, child->getStateCount() - 1, child->selectedState);
            primeMetersForActiveState (*child);
        }
    }

    void primeMetersForAllPlayableLanes (MachineModel& model)
    {
        for (auto& state : model.states)
        {
            const auto hasPlacedAudio = std::any_of (state.lanes.begin(), state.lanes.end(), [] (const Lane& lane)
            {
                return lane.sourceScriptPath.isNotEmpty() || lane.frozenAudioPath.isNotEmpty();
            });

            for (const auto& lane : state.lanes)
                if (shouldRunPlacedLaneOnOrbit (state, lane, hasPlacedAudio))
                    primeMeterForLane (lane);

            if (auto* child = model.childMachine (state.index))
                primeMetersForAllPlayableLanes (*child);
        }
    }

    void tickOrbitConnections (double now)
    {
        if (! fsmRunning || orbitConnectionTransportStartMs <= 0.0 || machine.orbitConnections.empty())
            return;

        if (previousOrbitConnectionPhases.size() != machine.orbitConnections.size())
        {
            previousOrbitConnectionPhases.resize (machine.orbitConnections.size(), 0.0f);
            for (size_t i = 0; i < machine.orbitConnections.size(); ++i)
                previousOrbitConnectionPhases[i] = currentPhaseForConnection (machine.orbitConnections[i], now);
            return;
        }

        for (size_t i = 0; i < machine.orbitConnections.size(); ++i)
        {
            const auto& connection = machine.orbitConnections[i];
            if (connection.sourceState < 0 || connection.sourceState >= machine.getStateCount()
                || connection.targetState < 0 || connection.targetState >= machine.getStateCount())
                continue;

            const auto previous = previousOrbitConnectionPhases[i];
            const auto current = currentPhaseForConnection (connection, now);
            previousOrbitConnectionPhases[i] = current;

            const auto root = juce::jlimit (0.0f, 0.9999f, connection.sourcePhase);
            const auto crossed = current >= previous
                ? (root > previous && root <= current)
                : (root > previous || root <= current);

            if (crossed)
                fireOrbitConnection (connection);
        }
    }

    float currentPhaseForConnection (const OrbitConnection& connection, double now) const
    {
        if (connection.sourceState < 0 || connection.sourceState >= machine.getStateCount())
            return 0.0f;

        const auto rate = juce::jmax (0.05, rateSlider.getValue());
        const auto duration = juce::jmax (0.1, machine.state (connection.sourceState).secondsPerSection() / rate);
        const auto elapsed = juce::jmax (0.0, (now - orbitConnectionTransportStartMs) * 0.001);
        return static_cast<float> (std::fmod (elapsed, duration) / duration);
    }

    void fireOrbitConnection (const OrbitConnection& connection)
    {
        if (connection.targetState < 0 || connection.targetState >= machine.getStateCount())
            return;

        switch (connection.action)
        {
            case OrbitConnectionAction::start:
                if (orbitCanvas.isPlayheadPaused (connection.targetState))
                {
                    const auto phase = orbitCanvas.playheadPhaseForState (connection.targetState);
                    orbitCanvas.setRestartPlayhead (connection.targetState);
                    startOrbitConnectionTargetFromPhase (connection.targetState, phase);
                }
                else
                {
                    orbitCanvas.setReversePlayhead (connection.targetState, false);
                    startOrbitConnectionTarget (connection.targetState);
                }
                break;

            case OrbitConnectionAction::pause:
                orbitCanvas.setPausedPlayhead (connection.targetState);
                pauseOrbitConnectionTarget (connection.targetState);
                break;

            case OrbitConnectionAction::restart:
                stopOrbitConnectionTarget (connection.targetState);
                orbitCanvas.setRestartPlayhead (connection.targetState);
                startOrbitConnectionTarget (connection.targetState);
                break;

            case OrbitConnectionAction::reverse:
            {
                const auto phase = orbitCanvas.playheadPhaseForState (connection.targetState);
                const auto currentlyReversed = orbitCanvas.isPlayheadReversed (connection.targetState);
                stopRenderedOrbitConnectionTarget (connection.targetState);
                startRenderedOrbitConnectionTargetFromPhase (connection.targetState, phase, ! currentlyReversed);
                if (currentlyReversed)
                    orbitCanvas.setForwardPlayheadFromPhase (connection.targetState, phase);
                else
                    orbitCanvas.setReversePlayheadFromPhase (connection.targetState, phase);
                break;
            }

            case OrbitConnectionAction::programmable:
                appendLog ("Fabric connection fired for " + machine.state (connection.targetState).name
                           + ": " + connection.fabricScript);
                break;
        }

        statusLabel.setText ("Connection fired: " + orbitConnectionActionLabel (connection.action)
                             + " " + machine.state (connection.targetState).name,
                             juce::dontSendNotification);
    }

    void startOrbitConnectionTarget (int stateIndex, bool reverse = false)
    {
        auto& state = machine.state (stateIndex);
        const auto hasPlacedAudio = std::any_of (state.lanes.begin(), state.lanes.end(), [] (const Lane& lane)
        {
            return lane.sourceScriptPath.isNotEmpty() || lane.frozenAudioPath.isNotEmpty();
        });

        for (auto& lane : state.lanes)
        {
            if (! shouldRunPlacedLaneOnOrbit (state, lane, hasPlacedAudio))
                continue;

            const auto rate = juce::jmax (0.05, rateSlider.getValue());
            const auto trackMs = state.secondsPerSection() * 1000.0 / rate;
            const auto delaySeconds = juce::jlimit (0.0, trackMs, trackMs * static_cast<double> (lane.orbitPhase)) / 1000.0;
            const auto delayMs = juce::roundToInt (delaySeconds * 1000.0);
            const auto laneId = lane.id;
            const auto isRenderedLane = lane.frozen && ! lane.freezeStale && lane.frozenAudioPath.isNotEmpty();
            if (isRenderedLane)
            {
                scheduleRenderedLaneOrbitLoop (stateIndex, lane.id, delaySeconds, playbackGeneration, reverse);
            }
            else if (delayMs <= 1)
            {
                host.play (lane, getSclangPathOverride());
                lane.playing = true;
            }
            else
            {
                const auto generation = playbackGeneration;
                juce::Timer::callAfterDelay (delayMs, [safeThis = juce::Component::SafePointer<MainComponent> (this), laneId, generation]
                {
                    if (safeThis == nullptr || ! safeThis->fsmRunning || safeThis->playbackGeneration != generation)
                        return;

                    if (auto* laneToPlay = safeThis->findLaneById (safeThis->machine, laneId))
                    {
                        safeThis->host.play (*laneToPlay, safeThis->getSclangPathOverride());
                        laneToPlay->playing = true;
                    }
                });
            }
        }
    }

    void startOrbitConnectionTargetFromPhase (int stateIndex, float phase)
    {
        auto& state = machine.state (stateIndex);
        const auto safePhase = juce::jlimit (0.0f, 0.9999f, phase);
        const auto rate = juce::jmax (0.05, rateSlider.getValue());
        const auto trackSeconds = state.secondsPerSection() / rate;
        const auto hasPlacedAudio = std::any_of (state.lanes.begin(), state.lanes.end(), [] (const Lane& lane)
        {
            return lane.sourceScriptPath.isNotEmpty() || lane.frozenAudioPath.isNotEmpty();
        });

        for (auto& lane : state.lanes)
        {
            if (! shouldRunPlacedLaneOnOrbit (state, lane, hasPlacedAudio))
                continue;

            const auto laneStart = juce::jlimit (0.0f, 0.9999f, lane.orbitPhase);
            const auto span = lanePhaseSpanForState (state, lane);
            const auto laneEnd = juce::jlimit (laneStart + 0.002f, 0.9999f, laneStart + span);
            const auto isRenderedLane = lane.frozen && ! lane.freezeStale && lane.frozenAudioPath.isNotEmpty();
            auto delaySeconds = 0.0;
            auto sourceOffsetSeconds = 0.0;

            if (safePhase < laneStart)
            {
                delaySeconds = static_cast<double> (laneStart - safePhase) * trackSeconds;
            }
            else if (safePhase <= laneEnd)
            {
                sourceOffsetSeconds = static_cast<double> (safePhase - laneStart) * trackSeconds;
            }
            else
            {
                delaySeconds = static_cast<double> ((1.0f - safePhase) + laneStart) * trackSeconds;
            }

            if (isRenderedLane)
            {
                scheduleRenderedLaneOrbitLoop (stateIndex, lane.id, delaySeconds, playbackGeneration, false, sourceOffsetSeconds);
                continue;
            }

            const auto delayMs = juce::roundToInt (delaySeconds * 1000.0);
            if (sourceOffsetSeconds > 0.0)
                continue;

            if (delayMs <= 1)
            {
                host.play (lane, getSclangPathOverride());
                lane.playing = true;
            }
            else
            {
                const auto generation = playbackGeneration;
                const auto laneId = lane.id;
                juce::Timer::callAfterDelay (delayMs, [safeThis = juce::Component::SafePointer<MainComponent> (this), laneId, generation]
                {
                    if (safeThis == nullptr || ! safeThis->fsmRunning || safeThis->playbackGeneration != generation)
                        return;

                    if (auto* laneToPlay = safeThis->findLaneById (safeThis->machine, laneId))
                    {
                        safeThis->host.play (*laneToPlay, safeThis->getSclangPathOverride());
                        laneToPlay->playing = true;
                    }
                });
            }
        }
    }

    void startRenderedOrbitConnectionTargetFromPhase (int stateIndex, float phase, bool reverse)
    {
        auto& state = machine.state (stateIndex);
        const auto safePhase = juce::jlimit (0.0f, 0.9999f, phase);
        const auto rate = juce::jmax (0.05, rateSlider.getValue());
        const auto trackSeconds = state.secondsPerSection() / rate;

        for (auto& lane : state.lanes)
        {
            if (! shouldPlayLane (state, lane))
                continue;
            if (! lane.frozen || lane.freezeStale || lane.frozenAudioPath.isEmpty())
                continue;

            const auto laneStart = juce::jlimit (0.0f, 0.9999f, lane.orbitPhase);
            const auto span = lanePhaseSpanForState (state, lane);
            const auto laneEnd = juce::jlimit (laneStart + 0.002f, 0.9999f, laneStart + span);
            auto delaySeconds = 0.0;
            auto sourceOffsetSeconds = 0.0;

            if (! reverse)
            {
                if (safePhase < laneStart)
                {
                    delaySeconds = static_cast<double> (laneStart - safePhase) * trackSeconds;
                }
                else if (safePhase <= laneEnd)
                {
                    sourceOffsetSeconds = static_cast<double> (safePhase - laneStart) * trackSeconds;
                }
                else
                {
                    delaySeconds = static_cast<double> ((1.0f - safePhase) + laneStart) * trackSeconds;
                }
            }
            else if (safePhase >= laneStart && safePhase <= laneEnd)
            {
                sourceOffsetSeconds = static_cast<double> (safePhase - laneStart) * trackSeconds;
            }
            else
            {
                delaySeconds = static_cast<double> (safePhase > laneEnd
                    ? safePhase - laneEnd
                    : safePhase + (1.0f - laneEnd)) * trackSeconds;
                sourceOffsetSeconds = static_cast<double> (laneEnd - laneStart) * trackSeconds;
            }

            scheduleRenderedLaneOrbitLoop (stateIndex, lane.id, delaySeconds, playbackGeneration, reverse, sourceOffsetSeconds);
        }
    }

    void stopRenderedOrbitConnectionTarget (int stateIndex)
    {
        auto& state = machine.state (stateIndex);
        for (auto& lane : state.lanes)
        {
            if (! lane.frozen || lane.frozenAudioPath.isEmpty())
                continue;

            cancelRenderedLaneOrbitLoop (stateIndex, lane.id);
            renderedAudioPlayer.stopLane (lane.id);
            lane.playing = false;
        }
    }

    void pauseOrbitConnectionTarget (int stateIndex)
    {
        auto& state = machine.state (stateIndex);
        for (auto& lane : state.lanes)
        {
            cancelRenderedLaneOrbitLoop (stateIndex, lane.id);
            renderedAudioPlayer.stopLane (lane.id);
            host.stop (lane);
            lane.playing = false;
        }
    }

    void stopOrbitConnectionTarget (int stateIndex)
    {
        pauseOrbitConnectionTarget (stateIndex);
    }

    void scheduleAllTracksFromBarZero (int generation)
    {
        host.stopAll (machine);
        renderedAudioPlayer.start();

        const auto rate = juce::jmax (0.05, rateSlider.getValue());
        for (auto& state : machine.states)
        {
            const auto trackMs = state.secondsPerSection() * 1000.0 / rate;
            const auto hasPlacedAudio = std::any_of (state.lanes.begin(), state.lanes.end(), [] (const Lane& lane)
            {
                return lane.sourceScriptPath.isNotEmpty() || lane.frozenAudioPath.isNotEmpty();
            });

            for (auto& lane : state.lanes)
            {
                if (! shouldRunPlacedLaneOnOrbit (state, lane, hasPlacedAudio))
                    continue;

                const auto delaySeconds = juce::jlimit (0.0, trackMs, trackMs * static_cast<double> (lane.orbitPhase)) / 1000.0;
                const auto delayMs = juce::roundToInt (delaySeconds * 1000.0);
                const auto laneId = lane.id;
                const auto isRenderedLane = lane.frozen && ! lane.freezeStale && lane.frozenAudioPath.isNotEmpty();

                if (isRenderedLane)
                {
                    scheduleRenderedLaneOrbitLoop (state.index, lane.id, delaySeconds, generation);
                    continue;
                }

                if (delayMs <= 1)
                {
                    host.play (lane, getSclangPathOverride());
                    lane.playing = true;
                }
                else
                {
                    juce::Timer::callAfterDelay (delayMs, [safeThis = juce::Component::SafePointer<MainComponent> (this), laneId, generation]
                    {
                        if (safeThis == nullptr || ! safeThis->fsmRunning || safeThis->playbackGeneration != generation)
                            return;

                        if (auto* laneToPlay = safeThis->findLaneById (safeThis->machine, laneId))
                            safeThis->host.play (*laneToPlay, safeThis->getSclangPathOverride());
                    });
                }
            }
        }
    }

    void scheduleRenderedLaneOrbitLoop (int stateIndex,
                                        const juce::String& laneId,
                                        double delaySeconds,
                                        int generation,
                                        bool reverse = false,
                                        double sourceOffsetSeconds = 0.0,
                                        int loopVersion = -1)
    {
        if (! fsmRunning || playbackGeneration != generation)
            return;

        if (stateIndex < 0 || stateIndex >= machine.getStateCount())
            return;

        const auto loopKey = renderedLaneLoopKey (stateIndex, laneId);
        if (loopVersion < 0)
            loopVersion = ++renderedLaneLoopVersions[loopKey];
        else if (renderedLaneLoopVersions[loopKey] != loopVersion)
            return;

        auto& state = machine.state (stateIndex);
        auto* lane = findLaneByIdInState (state, laneId);
        if (lane == nullptr || ! shouldRunLaneOnOrbit (state, *lane, true))
            return;

        const auto rate = juce::jmax (0.05, rateSlider.getValue());
        const auto trackSeconds = state.secondsPerSection() / rate;
        const auto maxDuration = laneRenderDurationSeconds (state, *lane) / rate;

        if (renderedAudioPlayer.scheduleLane (*lane,
                                              juce::jmax (0.0, delaySeconds),
                                              effectiveLaneVolume (state, *lane),
                                              maxDuration,
                                              reverse,
                                              sourceOffsetSeconds))
        {
            lane->playing = true;
            appendLog ("JUCE scheduled rendered lane: " + lane->id + " @ "
                       + machine.state (stateIndex).name + " -> " + lane->frozenAudioPath);
        }
        else
        {
            appendLog ("Could not load rendered audio: " + lane->frozenAudioPath);
            return;
        }

        const auto nextDelaySeconds = juce::jmax (0.01, delaySeconds + trackSeconds - juce::jmax (0.0, sourceOffsetSeconds));
        const auto nextDelayMs = juce::jlimit (1, 120000, juce::roundToInt (nextDelaySeconds * 1000.0));
        juce::Timer::callAfterDelay (nextDelayMs, [safeThis = juce::Component::SafePointer<MainComponent> (this),
                                                   stateIndex,
                                                   laneId,
                                                   generation,
                                                   reverse,
                                                   loopVersion]
        {
            if (safeThis == nullptr || ! safeThis->fsmRunning || safeThis->playbackGeneration != generation)
                return;

            safeThis->scheduleRenderedLaneOrbitLoop (stateIndex, laneId, 0.0, generation, reverse, 0.0, loopVersion);
        });
    }

    std::string renderedLaneLoopKey (int stateIndex, const juce::String& laneId) const
    {
        return std::to_string (stateIndex) + ":" + laneId.toStdString();
    }

    void cancelRenderedLaneOrbitLoop (int stateIndex, const juce::String& laneId)
    {
        ++renderedLaneLoopVersions[renderedLaneLoopKey (stateIndex, laneId)];
    }

    bool shouldRunLaneOnOrbit (const State& state, const Lane& lane, bool trackHasPlacedAudio) const
    {
        if (! shouldPlayLane (state, lane))
            return false;

        const auto isPlacedAudio = lane.sourceScriptPath.isNotEmpty() || lane.frozenAudioPath.isNotEmpty();
        if (trackHasPlacedAudio && ! isPlacedAudio)
            return false;

        return isPlacedAudio && lane.frozen && ! lane.freezeStale && lane.frozenAudioPath.isNotEmpty();
    }

    bool shouldRunPlacedLaneOnOrbit (const State& state, const Lane& lane, bool trackHasPlacedAudio) const
    {
        if (! shouldPlayLane (state, lane))
            return false;

        const auto hasRenderedAudio = lane.frozen && ! lane.freezeStale && lane.frozenAudioPath.isNotEmpty();
        const auto hasLiveSource = lane.sourceScriptPath.isNotEmpty() || lane.script.trim().isNotEmpty();
        const auto isPlacedAudio = hasRenderedAudio || hasLiveSource || lane.frozenAudioPath.isNotEmpty();
        if (trackHasPlacedAudio && ! isPlacedAudio)
            return false;

        return hasRenderedAudio || hasLiveSource;
    }

    void selectInspectorLane (int newIndex)
    {
        clearSelectedRenderedWaveform();
        currentInspectorMachine().selectedLane = newIndex;
        refreshControls();
    }

    bool deleteSelectedLane()
    {
        if (selectedRenderedWaveformState >= 0 && selectedRenderedWaveformLane >= 0)
        {
            const auto stateIndex = selectedRenderedWaveformState;
            const auto laneIndex = selectedRenderedWaveformLane;
            return deleteRenderedWaveformAt (stateIndex, laneIndex);
        }

        auto& inspected = currentInspectorMachine();
        return deleteLaneAt (inspected, inspected.selectedState, inspected.selectedLane);
    }

    void clearSelectedRenderedWaveform()
    {
        selectedRenderedWaveformState = -1;
        selectedRenderedWaveformLane = -1;
        orbitCanvas.clearSelectedRenderedLane();
    }

    bool deleteRenderedWaveformAt (int stateIndex, int laneIndex)
    {
        if (stateIndex < 0 || stateIndex >= machine.getStateCount())
            return false;

        auto& state = machine.state (stateIndex);
        if (laneIndex < 0 || laneIndex >= static_cast<int> (state.lanes.size()))
            return false;

        auto& lane = state.lanes[static_cast<size_t> (laneIndex)];
        if (lane.sourceScriptPath.isEmpty() && lane.frozenAudioPath.isEmpty() && ! lane.frozen)
            return false;

        renderedAudioPlayer.stopLane (lane.id);
        host.stop (lane);
        lane.playing = false;
        lane.frozen = false;
        lane.freezeStale = false;
        lane.freezeInProgress = false;
        lane.frozenAudioPath.clear();
        lane.frozenDurationSeconds = 0.0;
        lane.sourceScriptPath.clear();
        lane.preparedBridge = -1;
        laneMeters.erase (lane.id.toStdString());

        clearSelectedRenderedWaveform();
        statusLabel.setText ("Removed waveform from " + lane.name, juce::dontSendNotification);
        orbitCanvas.invalidateWaveforms();
        markMachineDirty();
        applyAllMixToHost();
        refreshControls();
        return true;
    }

    bool deleteLaneAt (MachineModel& model, int stateIndex, int laneIndex)
    {
        if (stateIndex < 0 || stateIndex >= model.getStateCount())
            return false;

        auto& state = model.state (stateIndex);
        if (state.lanes.size() <= 1)
        {
            statusLabel.setText ("Keep at least one lane", juce::dontSendNotification);
            return false;
        }

        laneIndex = juce::jlimit (0, static_cast<int> (state.lanes.size()) - 1, laneIndex);
        auto& lane = state.lanes[static_cast<size_t> (laneIndex)];
        const auto laneName = lane.name;
        const auto meterKey = lane.id.toStdString();

        renderedAudioPlayer.stopLane (lane.id);
        host.stop (lane);
        laneMeters.erase (meterKey);
        state.lanes.erase (state.lanes.begin() + laneIndex);
        model.selectedState = juce::jlimit (0, model.getStateCount() - 1, stateIndex);
        model.selectedLane = juce::jlimit (0, static_cast<int> (state.lanes.size()) - 1, laneIndex);
        if (&model == &machine && selectedRenderedWaveformState == stateIndex)
        {
            if (selectedRenderedWaveformLane == laneIndex)
                clearSelectedRenderedWaveform();
            else if (selectedRenderedWaveformLane > laneIndex)
                --selectedRenderedWaveformLane;
        }
        statusLabel.setText ("Deleted " + laneName, juce::dontSendNotification);
        orbitCanvas.invalidateWaveforms();
        markMachineDirty();
        applyAllMixToHost();
        refreshControls();
        return true;
    }

    void toggleInspectorLaneEnabled (int newIndex)
    {
        auto& inspected = currentInspectorMachine();
        inspected.selectedLane = newIndex;
        auto& lane = inspected.selectedLaneRef();
        lane.enabled = ! lane.enabled;
        if (! lane.enabled)
        {
            renderedAudioPlayer.stopLane (lane.id);
            host.stop (lane);
        }
        markMachineDirty (UndoGroup::continuous);
        applyAllMixToHost();
        refreshControls();
    }

    void toggleInspectorLaneMute (int newIndex)
    {
        auto& inspected = currentInspectorMachine();
        inspected.selectedLane = newIndex;
        inspected.selectedLaneRef().muted = ! inspected.selectedLaneRef().muted;
        markMachineDirty();
        applyAllMixToHost();
        refreshControls();
    }

    void toggleInspectorLaneSolo (int newIndex)
    {
        auto& inspected = currentInspectorMachine();
        inspected.selectedLane = newIndex;
        inspected.selectedLaneRef().solo = ! inspected.selectedLaneRef().solo;
        markMachineDirty();
        applyAllMixToHost();
        refreshControls();
    }

    void toggleInspectorLaneFreeze (int newIndex)
    {
        auto& inspected = currentInspectorMachine();
        inspected.selectedLane = newIndex;
        auto& lane = inspected.selectedLaneRef();
        if (lane.frozen)
        {
            if (lane.freezeInProgress)
            {
                statusLabel.setText ("Freeze already running", juce::dontSendNotification);
                refreshControls();
                return;
            }

            lane.frozen = false;
            lane.freezeInProgress = false;
            lane.preparedBridge = -1;
            statusLabel.setText ("Live code", juce::dontSendNotification);
            refreshProjectMediaStatus();
        }
        else
        {
            if (! beginFreezeLane (inspected, inspected.selectedState, inspected.selectedLane))
                statusLabel.setText ("Freeze failed", juce::dontSendNotification);
            else
                statusLabel.setText ("Freezing lane", juce::dontSendNotification);
            refreshProjectMediaStatus();
        }
        markMachineDirty();
        refreshControls();
    }

    bool beginFreezeLane (MachineModel& model, int stateIndex, int laneIndex)
    {
        if (stateIndex < 0 || stateIndex >= model.getStateCount())
            return false;

        auto& state = model.state (stateIndex);
        if (laneIndex < 0 || laneIndex >= static_cast<int> (state.lanes.size()))
            return false;

        auto& lane = state.lanes[static_cast<size_t> (laneIndex)];
        if (lane.freezeInProgress)
            return false;

        lane.frozen = true;
        lane.freezeStale = true;
        lane.freezeInProgress = true;
        lane.frozenAudioPath = freezeFileForLane (lane).getFullPathName();
        lane.preparedBridge = -1;

        const auto duration = laneRenderDurationSeconds (state, lane) / juce::jmax (0.05, rateSlider.getValue());
        lane.frozenDurationSeconds = duration;
        if (host.freezeLane (lane, getSclangPathOverride(), duration, juce::File (lane.frozenAudioPath)))
            return true;

        lane.freezeInProgress = false;
        return false;
    }

    double laneRenderDurationSeconds (const State& state, const Lane& lane) const
    {
        const auto trackSeconds = juce::jmax (0.25, state.secondsPerSection());
        const auto phase = juce::jlimit (0.0, 0.9999, static_cast<double> (lane.orbitPhase));
        const auto remainingSeconds = juce::jmax (0.05, trackSeconds * (1.0 - phase));
        const auto beatSeconds = 60.0 / juce::jlimit (20.0, 320.0, state.tempoBpm);
        const auto totalBeats = state.durationUsesSeconds ? trackSeconds / beatSeconds : state.clockBeatsPerSection();
        const auto currentBeat = phase * totalBeats;
        const auto barBeats = juce::jmax (1.0, static_cast<double> (state.beatsPerBar) * (4.0 / static_cast<double> (juce::jlimit (1, 32, state.beatUnit))));

        auto requested = remainingSeconds;
        switch (lane.durationMode)
        {
            case LaneDurationMode::endOfBeat:
                requested = (std::ceil (currentBeat + 0.0001) - currentBeat) * beatSeconds;
                break;

            case LaneDurationMode::endOfBar:
                requested = (std::ceil ((currentBeat + 0.0001) / barBeats) * barBeats - currentBeat) * beatSeconds;
                break;

            case LaneDurationMode::fixedBars:
                requested = juce::jmax (0.01, lane.durationValue) * state.secondsPerBar();
                break;

            case LaneDurationMode::fixedSeconds:
                requested = juce::jmax (0.01, lane.durationValue);
                break;

            case LaneDurationMode::natural:
                requested = remainingSeconds;
                break;
        }

        return juce::jlimit (0.05, remainingSeconds, requested);
    }

    float lanePhaseSpanForState (const State& state, const Lane& lane) const
    {
        const auto trackSeconds = juce::jmax (0.25, state.secondsPerSection());
        return static_cast<float> (juce::jlimit (0.002, 1.0, laneRenderDurationSeconds (state, lane) / trackSeconds));
    }

    int refreezeStaleFrozenLanesInMachine (MachineModel& model)
    {
        auto count = 0;
        for (int stateIndex = 0; stateIndex < model.getStateCount(); ++stateIndex)
        {
            auto& state = model.state (stateIndex);
            for (int laneIndex = 0; laneIndex < static_cast<int> (state.lanes.size()); ++laneIndex)
            {
                auto& lane = state.lanes[static_cast<size_t> (laneIndex)];
                if (lane.frozen && lane.freezeStale && ! lane.freezeInProgress && beginFreezeLane (model, stateIndex, laneIndex))
                    ++count;
            }

            if (auto* child = model.childMachine (stateIndex))
                count += refreezeStaleFrozenLanesInMachine (*child);
        }

        return count;
    }

    int refreezeRenderedLanesInState (MachineModel& model, int stateIndex)
    {
        if (stateIndex < 0 || stateIndex >= model.getStateCount())
            return 0;

        auto& state = model.state (stateIndex);
        auto count = 0;

        for (int laneIndex = 0; laneIndex < static_cast<int> (state.lanes.size()); ++laneIndex)
        {
            auto& lane = state.lanes[static_cast<size_t> (laneIndex)];
            if (! lane.freezeInProgress && lane.frozen && lane.frozenAudioPath.isNotEmpty())
            {
                lane.freezeStale = true;
                lane.preparedBridge = -1;
                if (beginFreezeLane (model, stateIndex, laneIndex))
                    ++count;
            }
        }

        if (count > 0)
            orbitCanvas.invalidateWaveforms();

        return count;
    }

    void refreezeSelectedLane()
    {
        auto& inspected = currentInspectorMachine();
        if (beginFreezeLane (inspected, inspected.selectedState, inspected.selectedLane))
        {
            statusLabel.setText ("Refreezing lane", juce::dontSendNotification);
            markMachineDirty();
        }
        else
        {
            statusLabel.setText ("Refreeze failed", juce::dontSendNotification);
        }

        refreshControls();
    }

    void refreezeStaleFrozenLanes()
    {
        const auto count = refreezeStaleFrozenLanesInMachine (machine);
        if (count > 0)
        {
            statusLabel.setText ("Refreezing " + juce::String (count) + " stale lane" + (count == 1 ? "" : "s"), juce::dontSendNotification);
            markMachineDirty();
            refreshProjectMediaStatus();
        }
        else
        {
            statusLabel.setText ("No stale freezes", juce::dontSendNotification);
        }

        refreshControls();
    }

    int renderAllPlacedLanesInMachine (MachineModel& model)
    {
        auto count = 0;
        for (int stateIndex = 0; stateIndex < model.getStateCount(); ++stateIndex)
        {
            auto& state = model.state (stateIndex);
            for (int laneIndex = 0; laneIndex < static_cast<int> (state.lanes.size()); ++laneIndex)
            {
                auto& lane = state.lanes[static_cast<size_t> (laneIndex)];
                const auto hasSource = lane.sourceScriptPath.isNotEmpty() || lane.script.trim().isNotEmpty();
                if (hasSource && ! lane.freezeInProgress && beginFreezeLane (model, stateIndex, laneIndex))
                    ++count;
            }

            if (auto* child = model.childMachine (stateIndex))
                count += renderAllPlacedLanesInMachine (*child);
        }

        if (count > 0)
            orbitCanvas.invalidateWaveforms();

        return count;
    }

    void renderAllPlacedLanes()
    {
        const auto count = renderAllPlacedLanesInMachine (machine);
        if (count > 0)
        {
            statusLabel.setText ("Rendering " + juce::String (count) + " lane" + (count == 1 ? "" : "s"), juce::dontSendNotification);
            markMachineDirty();
            refreshProjectMediaStatus();
        }
        else
        {
            statusLabel.setText ("No lanes to render", juce::dontSendNotification);
        }

        refreshControls();
    }

    int countStaleFrozenLanes (const MachineModel& model) const
    {
        auto count = 0;
        for (const auto& state : model.states)
        {
            for (const auto& lane : state.lanes)
                if (lane.frozen && lane.freezeStale)
                    ++count;

            if (auto* child = model.childMachine (state.index))
                count += countStaleFrozenLanes (*child);
        }

        return count;
    }

    int countFreezingLanes (const MachineModel& model) const
    {
        auto count = 0;
        for (const auto& state : model.states)
        {
            for (const auto& lane : state.lanes)
                if (lane.freezeInProgress)
                    ++count;

            if (auto* child = model.childMachine (state.index))
                count += countFreezingLanes (*child);
        }

        return count;
    }

    void setInspectorLaneVolume (int newIndex, float volume)
    {
        auto& inspected = currentInspectorMachine();
        inspected.selectedLane = newIndex;
        auto& lane = inspected.selectedLaneRef();
        lane.volume = juce::jlimit (0.0f, 1.0f, volume);
        lane.preparedBridge = -1;
        markMachineDirty();
        applyAllMixToHost();
        refreshControls();
    }

    void setInspectorLaneGain (int newIndex, float gain)
    {
        auto& inspected = currentInspectorMachine();
        inspected.selectedLane = newIndex;
        auto& lane = inspected.selectedLaneRef();
        lane.gain = juce::jlimit (0.0f, 2.0f, gain);
        lane.preparedBridge = -1;
        markMachineDirty (UndoGroup::continuous);
        applyAllMixToHost();
        refreshControls();
    }

    void setInspectorLanePan (int newIndex, float pan)
    {
        auto& inspected = currentInspectorMachine();
        inspected.selectedLane = newIndex;
        auto& lane = inspected.selectedLaneRef();
        lane.pan = juce::jlimit (-1.0f, 1.0f, pan);
        lane.preparedBridge = -1;
        markMachineDirty (UndoGroup::continuous);
        applyAllMixToHost();
        refreshControls();
    }

    juce::String makeUniqueLaneId (MachineModel& model, int stateIndex)
    {
        for (int i = 0; i < 256; ++i)
        {
            const auto candidate = model.makeLaneId (stateIndex, model.getLaneCount (stateIndex) + i);
            if (findLaneById (machine, candidate) == nullptr)
                return candidate;
        }

        return model.makeLaneId (stateIndex, model.getLaneCount (stateIndex)) + "-" + juce::Uuid().toString().substring (0, 8);
    }

    void duplicateSelectedLane()
    {
        auto& inspected = currentInspectorMachine();
        auto& state = inspected.state (inspected.selectedState);

        if (state.lanes.empty())
            return;

        const auto sourceIndex = juce::jlimit (0, static_cast<int> (state.lanes.size()) - 1, inspected.selectedLane);
        auto lane = state.lanes[static_cast<size_t> (sourceIndex)];
        lane.id = makeUniqueLaneId (inspected, inspected.selectedState);
        lane.name = lane.name + " copy";
        lane.playing = false;
        lane.preparedBridge = -1;

        const auto insertIndex = sourceIndex + 1;
        state.lanes.insert (state.lanes.begin() + insertIndex, std::move (lane));
        inspected.selectedLane = insertIndex;
        markMachineDirty();
        refreshControls();
    }

    void chooseSuperColliderFileForOrbit (int stateIndex, float phase)
    {
        const auto start = currentProjectFile.existsAsFile()
            ? currentProjectFile.getParentDirectory()
            : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);

        projectChooser = std::make_unique<juce::FileChooser> ("Place SuperCollider File", start, "*.scd;*.txt");
        auto safeThis = juce::Component::SafePointer<MainComponent> (this);
        projectChooser->launchAsync (juce::FileBrowserComponent::openMode
                                         | juce::FileBrowserComponent::canSelectFiles,
                                     [safeThis, stateIndex, phase] (const juce::FileChooser& chooser)
                                     {
                                         if (safeThis == nullptr)
                                             return;

                                         const auto file = chooser.getResult();
                                         if (file == juce::File())
                                             return;

                                         safeThis->promptSuperColliderLaneDuration (stateIndex, phase, file);
                                     });
    }

    void promptSuperColliderLaneDuration (int stateIndex, float phase, const juce::File& file)
    {
        laneDurationPrompt = std::make_unique<juce::AlertWindow> ("Render duration",
                                                                  "Choose how long this SuperCollider file should play on the track.",
                                                                  juce::AlertWindow::NoIcon);
        laneDurationPrompt->addTextEditor ("amount", "1", "Bars or seconds");
        laneDurationPrompt->addButton ("Beat", 1);
        laneDurationPrompt->addButton ("Bar", 2);
        laneDurationPrompt->addButton ("x bars", 3);
        laneDurationPrompt->addButton ("x seconds", 4);
        laneDurationPrompt->addButton ("Natural", 5);
        laneDurationPrompt->addButton ("Cancel", 0);

        auto safeThis = juce::Component::SafePointer<MainComponent> (this);
        laneDurationPrompt->enterModalState (true, juce::ModalCallbackFunction::create (
            [safeThis, stateIndex, phase, file] (int result)
            {
                if (safeThis == nullptr)
                    return;

                auto amount = 1.0;
                if (safeThis->laneDurationPrompt != nullptr)
                    amount = safeThis->laneDurationPrompt->getTextEditorContents ("amount").getDoubleValue();

                auto mode = LaneDurationMode::natural;
                if (result == 1) mode = LaneDurationMode::endOfBeat;
                else if (result == 2) mode = LaneDurationMode::endOfBar;
                else if (result == 3) mode = LaneDurationMode::fixedBars;
                else if (result == 4) mode = LaneDurationMode::fixedSeconds;
                else if (result != 5)
                {
                    safeThis->laneDurationPrompt = nullptr;
                    return;
                }

                safeThis->laneDurationPrompt = nullptr;
                safeThis->placeSuperColliderFileOnOrbit (stateIndex, phase, file, mode, juce::jmax (0.01, amount));
            }), false);
    }

    void placeSuperColliderFileOnOrbit (int stateIndex, float phase, const juce::File& file, LaneDurationMode durationMode, double durationValue)
    {
        if (stateIndex < 0 || stateIndex >= machine.getStateCount() || ! file.existsAsFile())
        {
            statusLabel.setText ("SC file not found", juce::dontSendNotification);
            return;
        }

        auto script = file.loadFileAsString();
        if (script.trim().isEmpty())
        {
            statusLabel.setText ("SC file was empty", juce::dontSendNotification);
            return;
        }

        auto& state = machine.state (stateIndex);
        Lane lane;
        lane.id = makeUniqueLaneId (machine, stateIndex);
        lane.name = file.getFileNameWithoutExtension();
        lane.script = std::move (script);
        lane.sourceScriptPath = file.getFullPathName();
        lane.orbitPhase = juce::jlimit (0.0f, 0.9999f, phase);
        lane.durationMode = durationMode;
        lane.durationValue = durationValue;
        lane.volume = 1.0f;
        lane.enabled = true;
        lane.frozen = false;
        lane.freezeStale = false;
        lane.freezeInProgress = false;
        lane.preparedBridge = -1;

        state.lanes.push_back (std::move (lane));
        machine.selectedState = stateIndex;
        machine.selectedLane = static_cast<int> (state.lanes.size()) - 1;
        inspectedMachine = &machine;

        if (beginFreezeLane (machine, stateIndex, machine.selectedLane))
            statusLabel.setText ("Rendering " + file.getFileName(), juce::dontSendNotification);
        else
            statusLabel.setText ("Render failed", juce::dontSendNotification);

        markMachineDirty();
        refreshControls();
    }

    void refreshControls()
    {
        updateProjectFileLabel();
        updateInspectorModeButtons();
        refreshStateTabs();
        refreshTrackList();
        rules.setMachine (currentInspectorMachine());

        if (! scriptEditor.hasKeyboardFocus (true) || codeDocument.getAllContent() != currentInspectorMachine().selectedLaneRef().script)
        {
            loadingCodeDocument = true;
            scriptEditor.loadContent (currentInspectorMachine().selectedLaneRef().script);
            loadingCodeDocument = false;
        }
        updateCodeStats();
        trackNameEditor.setText (currentInspectorMachine().selectedLaneRef().name, false);
        const auto& selectedLane = currentInspectorMachine().selectedLaneRef();
        const auto mediaStatus = cachedProjectMediaStatus;
        const auto mediaSummary = mediaStatusSummary (mediaStatus);
        const auto freezingCount = countFreezingLanes (machine);
        const auto freezeSuffix = freezingCount > 0 ? " | " + juce::String (freezingCount) + " freezing" : juce::String();
        if (selectedLane.freezeInProgress)
        {
            freezeStatusLabel.setText ("Freezing selected lane" + freezeSuffix, juce::dontSendNotification);
            freezeStatusLabel.setColour (juce::Label::textColourId, graphColour (currentInspectorMachine().selectedLane).brighter (0.16f));
            refreezeLaneButton.setButtonText ("Freezing");
        }
        else if (! selectedLane.frozen)
        {
            freezeStatusLabel.setText ((mediaSummary.isEmpty() ? "Live code" : "Live code | " + mediaSummary) + freezeSuffix, juce::dontSendNotification);
            freezeStatusLabel.setColour (juce::Label::textColourId, mediaStatus.needsAttention() ? graphColour (currentInspectorMachine().selectedLane, 4).brighter (0.18f) : mutedInk());
            refreezeLaneButton.setButtonText ("Freeze");
        }
        else if (selectedLane.freezeStale)
        {
            freezeStatusLabel.setText ((mediaSummary.isEmpty() ? "Freeze stale: render again" : "Freeze stale: " + mediaSummary) + freezeSuffix, juce::dontSendNotification);
            freezeStatusLabel.setColour (juce::Label::textColourId, graphColour (currentInspectorMachine().selectedLane, 4).brighter (0.20f));
            refreezeLaneButton.setButtonText ("Refreeze");
        }
        else
        {
            freezeStatusLabel.setText ((mediaSummary.isEmpty() ? "Frozen audio ready" : "Frozen audio | " + mediaSummary) + freezeSuffix, juce::dontSendNotification);
            freezeStatusLabel.setColour (juce::Label::textColourId, mediaStatus.needsAttention() ? graphColour (currentInspectorMachine().selectedLane, 4).brighter (0.18f)
                                                                                                  : graphColour (currentInspectorMachine().selectedLane, 2).brighter (0.12f));
            refreezeLaneButton.setButtonText ("Refreeze");
        }
        refreezeLaneButton.setEnabled (! selectedLane.freezeInProgress);
        refreezeStaleButton.setEnabled ((mediaStatus.stale > 0 || mediaStatus.missing > 0) && freezingCount == 0);
        renderAllButton.setEnabled (freezingCount == 0);
        breadcrumbLabel.setText (makeBreadcrumb(), juce::dontSendNotification);
        stateSummaryLabel.setText (makeStateSummary(), juce::dontSendNotification);
        const auto& inspectedState = currentInspectorMachine().state (currentInspectorMachine().selectedState);
        stateTempoEditor.setText (juce::String (inspectedState.tempoBpm, 1), false);
        stateMeterBeatsEditor.setText (juce::String (inspectedState.beatsPerBar), false);
        stateMeterUnitEditor.setText (juce::String (inspectedState.beatUnit), false);
        stateDurationModeButton.setButtonText (inspectedState.durationUsesSeconds ? "Secs" : "Bars");
        stateDurationBarsEditor.setText (juce::String (inspectedState.arrangementBars), false);
        stateDurationBeatsEditor.setText (juce::String (inspectedState.arrangementBeats), false);
        stateDurationSecondsEditor.setText (juce::String (inspectedState.durationSeconds, 2), false);
        stateDurationBarsEditor.setEnabled (! inspectedState.durationUsesSeconds);
        stateDurationBeatsEditor.setEnabled (! inspectedState.durationUsesSeconds);
        stateDurationSecondsEditor.setEnabled (inspectedState.durationUsesSeconds);
        if (auto* child = selectedNestedMachine())
        {
            nestedModeBox.setEnabled (true);
            nestedDivisionMinus.setEnabled (true);
            nestedDivisionEditor.setEnabled (true);
            nestedDivisionPlus.setEnabled (true);
            nestedModeBox.setSelectedItemIndex (static_cast<int> (child->timingMode), juce::dontSendNotification);
            nestedDivisionEditor.setText (juce::String (child->parentDivision), false);
        }
        else
        {
            nestedModeBox.setEnabled (false);
            nestedDivisionMinus.setEnabled (false);
            nestedDivisionEditor.setEnabled (false);
            nestedDivisionPlus.setEnabled (false);
            nestedModeBox.setSelectedItemIndex (0, juce::dontSendNotification);
            nestedDivisionEditor.setText ("-", false);
        }
        topStateCountEditor.setText (juce::String (machine.getStateCount()), false);
        const auto arrangementVisible = arrangementViewMode > 0;
        arrangementStrip.setVisible (arrangementVisible);
        graph.setVisible (false);
        orbitCanvas.setVisible (arrangementViewMode != 2);
        arrangementStrip.setMachine (machine, rateSlider.getValue(), arrangementViewMode == 2, exportInProgress, exportElapsedSeconds, exportTotalSeconds);
        if (orbitFocusedTrack >= machine.getStateCount())
            orbitFocusedTrack = -1;
        orbitCanvas.setShapeEditMode (orbitShapeEditMode);
        orbitCanvas.setMachine (machine, rateSlider.getValue(), fsmRunning, orbitFocusedTrack);
        updateArrangementButtonText();
        arrangementViewButton.setColour (juce::TextButton::buttonColourId,
                                         arrangementVisible ? rowFill().interpolatedWith (graphColour (machine.selectedState + 2), arrangementViewMode == 2 ? 0.30f : 0.20f)
                                                            : panelFill().brighter (0.04f));
        arrangementViewButton.setColour (juce::TextButton::textColourOffId,
                                         arrangementVisible ? graphColour (machine.selectedState + 2).brighter (0.12f)
                                                            : mutedInk());
        const auto selectedLanePlaying = currentInspectorMachine().selectedLaneRef().playing;
        playButton.setButtonText (selectedLanePlaying ? "Stop" : "Audition");
        playButton.setColour (juce::TextButton::buttonColourId,
                              selectedLanePlaying ? rowFill().interpolatedWith (graphColour (currentInspectorMachine().selectedLane, 4), 0.24f)
                                                  : rowFill().interpolatedWith (graphColour (currentInspectorMachine().selectedLane), 0.16f));
        runButton.setEnabled (! exportInProgress);
        stepButton.setEnabled (! exportInProgress);
        playButton.setEnabled (! exportInProgress);
        stopAllButton.setEnabled (! exportInProgress || exportCancelRequested);
        runButton.setColour (juce::TextButton::buttonColourId, fsmRunning ? rowFill().interpolatedWith (graphColour (machine.selectedState), 0.24f)
                                                                          : rowFill().interpolatedWith (graphColour (machine.selectedState), 0.10f));
        renderAllButton.setColour (juce::TextButton::buttonColourId, rowFill().interpolatedWith (accentB(), 0.14f));
        renderAllButton.setColour (juce::TextButton::textColourOffId, accentB().brighter (0.06f));
        shapeEditButton.setToggleState (orbitShapeEditMode, juce::dontSendNotification);
        shapeEditButton.setColour (juce::TextButton::buttonColourId, orbitShapeEditMode ? rowFill().interpolatedWith (accentB(), 0.22f)
                                                                                        : rowFill().interpolatedWith (accentB(), 0.08f));
        shapeEditButton.setColour (juce::TextButton::textColourOffId, orbitShapeEditMode ? accentB().brighter (0.08f) : mutedInk());
        stepButton.setColour (juce::TextButton::buttonColourId, rowFill().interpolatedWith (graphColour (machine.selectedState + 1), 0.10f));
        stopAllButton.setColour (juce::TextButton::buttonColourId, rowFill().interpolatedWith (graphColour (machine.selectedState + 4), 0.10f));
        moveLaneUpButton.setEnabled (currentInspectorMachine().selectedLane > 0);
        moveLaneDownButton.setEnabled (currentInspectorMachine().selectedLane < currentInspectorMachine().getLaneCount (currentInspectorMachine().selectedState) - 1);
        duplicateLaneButton.setEnabled (currentInspectorMachine().getLaneCount (currentInspectorMachine().selectedState) > 0);
        const auto hasChild = currentInspectorMachine().hasChildMachine (currentInspectorMachine().selectedState);
        addChildMachineButton.setEnabled (! hasChild);
        removeChildMachineButton.setEnabled (hasChild);
        navigator.setMachines (machine, activeMachine, inspectedMachine);
        graph.repaint();
        orbitCanvas.repaint();
        rules.repaint();
        graph.setInspectedMachine (&currentInspectorMachine());
        updateTransitionPreview();
    }

    void refreshVisualTheme()
    {
        scriptEditor.setColour (juce::CodeEditorComponent::highlightColourId, graphColour (1).withAlpha (0.24f));
        updateInspectorModeButtons();
        refreshStateTabs();
        refreshTrackList();
        navigator.repaint();
        graph.repaint();
        orbitCanvas.repaint();
        rules.repaint();
        repaint();
    }

    void refreshStateTabs()
    {
        juce::StringArray names;
        for (int i = 0; i < currentMachine().getStateCount(); ++i)
            names.add (currentMachine().state (i).name);

        stateTabs.setItems (names, currentMachine().selectedState);
    }

    juce::String currentProjectDisplayName() const
    {
        if (currentProjectFile.existsAsFile())
            return currentProjectFile.getFileName();

        return autosaveFile().getFileName();
    }

    void updateProjectFileLabel()
    {
        auto name = currentProjectDisplayName();
        if (dirtyProject)
            name += " *";

        projectFileLabel.setText (name, juce::dontSendNotification);
    }

    void refreshTrackList()
    {
        auto& inspected = currentInspectorMachine();
        auto& s = inspected.state (inspected.selectedState);
        trackList.setState (s, inspected.selectedLane);
        mixer.setState (s, inspected.selectedLane, fsmRunning);
    }

    OfLookAndFeel ofLookAndFeel;
    MachineModel machine;
    MachineModel* activeMachine = &machine;
    MachineModel* inspectedMachine = &machine;
    std::vector<MachineModel*> machineStack;
    SuperColliderHost host;
    GraphComponent graph;
    OrbitTrackCanvas orbitCanvas;
    juce::TextButton graphFitButton;
    juce::TextButton graphLayoutButton;
    juce::TextButton arrangementViewButton;
    RuleListComponent rules;
    PaneDivider graphBottomDivider { PaneDivider::Orientation::horizontal };
    PaneDivider rulesTracksDivider;
    PaneDivider tracksCodeDivider;
    PaneDivider rightInspectorDivider { PaneDivider::Orientation::horizontal };

    juce::Label title;
    juce::Label projectFileLabel;
    ClickableLabel statusLabel;
    juce::TextButton loadProjectButton;
    juce::TextButton saveProjectButton;
    juce::TextButton undoButton;
    juce::TextButton redoButton;
    juce::TextButton logButton;
    juce::TextButton panicButton;
    juce::Label topStateCountLabel;
    juce::TextButton topStateCountMinus;
    juce::TextEditor topStateCountEditor;
    juce::TextButton topStateCountPlus;
    juce::Label masterGainLabel;
    juce::Slider masterGainSlider;
    juce::TextButton runButton;
    juce::TextButton stepButton;
    juce::TextButton stopAllButton;
    juce::TextButton renderAllButton;
    juce::Slider rateSlider;
    PillBar stateTabs;
    ArrangementStripComponent arrangementStrip;
    FsmNavigatorComponent navigator;
    juce::Label stateInfoTitle;
    juce::Label nestedSectionTitle;
    juce::Label trackSectionTitle;
    juce::Label breadcrumbLabel;
    juce::Label stateSummaryLabel;
    juce::Label stateTempoLabel;
    juce::TextEditor stateTempoEditor;
    juce::Label stateMeterLabel;
    juce::TextEditor stateMeterBeatsEditor;
    juce::Label stateMeterSlashLabel;
    juce::TextEditor stateMeterUnitEditor;
    juce::TextButton stateDurationModeButton;
    juce::TextEditor stateDurationBarsEditor;
    juce::TextEditor stateDurationBeatsEditor;
    juce::TextEditor stateDurationSecondsEditor;
    juce::Label nestedTimingLabel;
    juce::ComboBox nestedModeBox;
    juce::Label nestedDivisionLabel;
    juce::TextButton nestedDivisionMinus;
    juce::TextEditor nestedDivisionEditor;
    juce::TextButton nestedDivisionPlus;
    juce::TextButton tracksModeButton;
    juce::TextButton mixerModeButton;
    juce::Label trackPaneTitle;
    juce::TextEditor trackNameEditor;
    juce::TextButton shapeEditButton;
    juce::TextButton resetShapeButton;
    juce::Label freezeStatusLabel;
    juce::TextButton refreezeLaneButton;
    juce::TextButton refreezeStaleButton;
    TrackListComponent trackList;
    MixerComponent mixer;
    juce::Label codePaneTitle;
    juce::Label codeStatsLabel;
    juce::Label codeCheckLabel;
    juce::TextButton checkCodeButton;
    juce::TextEditor codeFontSizeEditor;
    juce::TextButton tidyCodeButton;
    juce::TextButton expandCodeButton;
    SuperColliderTokeniser scTokeniser;
    juce::CodeDocument codeDocument;
    SuperColliderCodeEditor scriptEditor;
    juce::TextButton addLaneButton;
    juce::TextButton removeLaneButton;
    juce::TextButton duplicateLaneButton;
    juce::TextButton moveLaneUpButton;
    juce::TextButton moveLaneDownButton;
    juce::TextButton addChildMachineButton;
    juce::TextButton removeChildMachineButton;
    juce::TextButton playButton;
    WelcomePanel welcomePanel;
    juce::TextEditor logView;
    juce::String scLog;
    bool logDirty = false;
    double lastLogFlushMs = 0.0;
    bool logVisible = false;
    bool codeExpanded = false;
    int headerCompactLevel = 0;
    int arrangementViewMode = 0;
    bool orbitShapeEditMode = false;
    int orbitFocusedTrack = -1;
    int selectedRenderedWaveformState = -1;
    int selectedRenderedWaveformLane = -1;
    bool fsmRunning = false;
    int playbackGeneration = 0;
    std::unordered_map<std::string, int> renderedLaneLoopVersions;
    double orbitConnectionTransportStartMs = 0.0;
    std::vector<float> previousOrbitConnectionPhases;
    bool machinePrepared = false;
    bool exportInProgress = false;
    bool exportCancelRequested = false;
    double exportElapsedSeconds = 0.0;
    double exportTotalSeconds = 0.0;
    juce::String exportOutputPath;
    InspectorMode inspectorMode = InspectorMode::tracks;
    int rulesPaneWidth = 500;
    int tracksPaneWidth = 210;
    int bottomPaneHeight = 250;
    int dividerDragStartRulesWidth = 500;
    int dividerDragStartTracksWidth = 210;
    int dividerDragStartBottomHeight = 250;
    int rightStatePaneHeight = 300;
    int dividerDragStartRightStateHeight = 300;
    bool rulesPaneUserSized = false;
    bool tracksPaneUserSized = false;
    bool bottomPaneUserSized = false;
    bool rightInspectorUserSized = false;
    std::atomic<bool> audioJobRunning { false };
    std::atomic<int> audioPrepareRevision { 0 };
    std::atomic<bool> resetAudioBeforePrepare { false };
    bool prepareQueued = false;
    bool prepareQueuedStartAfter = false;
    std::atomic<bool> transportShouldRun { false };
    std::atomic<int> transportIntervalMs { 2000 };
    std::atomic<int> transportCallbackGeneration { 0 };
    double scheduledTransitionDelaySeconds = 0.0;
    float codeFontSize = 14.0f;
    std::mutex transportMutex;
    std::condition_variable transportCv;
    std::thread transportThread;
    bool loadingCodeDocument = false;
    juce::String pendingCheckId;
    std::unordered_map<std::string, LaneMeterState> laneMeters;
    std::unique_ptr<juce::FileChooser> projectChooser;
    std::unique_ptr<juce::AlertWindow> laneDurationPrompt;
    juce::File currentProjectFile;
    juce::File loadingProjectDirectory;
    ProjectMediaStatus cachedProjectMediaStatus;
    juce::String lastProjectMediaStatus = "Project ready";
    juce::StringArray recentProjects;
    RenderedAudioPlayer renderedAudioPlayer;
    juce::AudioDeviceManager audioDeviceManager;
    std::unique_ptr<SettingsWindow> settingsWindow;
    std::unique_ptr<AudioExportWindow> exportWindow;
    bool dirtyProject = false;
    bool loadingProjectInternally = false;
    bool suppressUndoCapture = false;
    bool colourblindSafeMode = false;
    SuperColliderAudioSettings scAudioSettings;
    AudioExportSettings exportSettings;
    juce::String lastProjectSnapshot;
    std::vector<juce::String> undoSnapshots;
    std::vector<juce::String> redoSnapshots;
    UndoGroup lastUndoGroup = UndoGroup::structural;
    double lastUndoSnapshotMs = 0.0;
    double lastDirtyMs = 0.0;
    double lastAutosaveTimerMs = 0.0;
    double visualNextStateMs = 0.0;
    double lastSchedulerStateMs = 0.0;
    double scheduledTransitionTargetMs = 0.0;
    int scheduledVisualFromState = -1;
    int scheduledVisualNextState = -1;
    bool deferredStateRefreshPending = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

class OfApplication final : public juce::JUCEApplication,
                                private juce::MenuBarModel
{
public:
    const juce::String getApplicationName() override { return "of::"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise (const juce::String&) override
    {
        mainWindow = std::make_unique<MainWindow> (getApplicationName());

       #if JUCE_MAC
        juce::MenuBarModel::setMacMainMenu (this);
       #endif
    }

    void shutdown() override
    {
       #if JUCE_MAC
        juce::MenuBarModel::setMacMainMenu (nullptr);
       #endif

        mainWindow = nullptr;
    }

    void systemRequestedQuit() override
    {
        quit();
    }

private:
    enum MenuItemIds
    {
        newProjectItem = 1,
        loadProjectItem,
        saveProjectItem,
        saveProjectAsItem,
        exportAudioItem,
        cancelExportItem,
        welcomeItem,
        settingsItem,
        aboutItem
    };

    juce::StringArray getMenuBarNames() override
    {
        return { "File" };
    }

    juce::PopupMenu getMenuForIndex (int menuIndex, const juce::String& menuName) override
    {
        juce::ignoreUnused (menuIndex);

        juce::PopupMenu menu;

        if (menuName == "File")
        {
            menu.addItem (newProjectItem, "New");
            menu.addItem (loadProjectItem, "Load...");
            menu.addSeparator();
            menu.addItem (saveProjectItem, "Save");
            menu.addItem (saveProjectAsItem, "Save As...");
            menu.addSeparator();
            menu.addItem (exportAudioItem, "Export Audio...");
            menu.addItem (cancelExportItem, "Cancel Export");
            menu.addSeparator();
            menu.addItem (welcomeItem, "Welcome");
            menu.addItem (settingsItem, "Settings...");
            menu.addSeparator();
            menu.addItem (aboutItem, "About of::");
        }

        return menu;
    }

    void menuItemSelected (int menuItemID, int topLevelMenuIndex) override
    {
        juce::ignoreUnused (topLevelMenuIndex);

        if (mainWindow == nullptr)
            return;

        auto* main = mainWindow->getMainComponent();

        if (main == nullptr)
            return;

        switch (menuItemID)
        {
            case newProjectItem:     main->newProject(); break;
            case loadProjectItem:    main->loadProject(); break;
            case saveProjectItem:    main->saveCurrentProject(); break;
            case saveProjectAsItem:  main->saveProjectAs(); break;
            case exportAudioItem:    main->exportAudio(); break;
            case cancelExportItem:   main->cancelAudioExport(); break;
            case welcomeItem:        main->showWelcomePanel(); break;
            case settingsItem:       main->showSettings(); break;
            case aboutItem:          main->showAbout(); break;
            default: break;
        }
    }

    class MainWindow final : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (juce::String name)
            : DocumentWindow (std::move (name), backgroundTop(), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);
            setResizable (true, true);
            if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
                setBounds (display->userArea);
            else
                centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

        MainComponent* getMainComponent() const
        {
            return dynamic_cast<MainComponent*> (getContentComponent());
        }
    };

    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (OfApplication)
