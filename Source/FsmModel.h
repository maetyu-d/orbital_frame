#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "DemoScripts.h"

#include <algorithm>
#include <memory>
#include <vector>

inline constexpr int maxStateCount = 12;

enum class NestedTimingMode
{
    followParent,
    freeRun,
    oneShot,
    latch
};

inline juce::String nestedTimingModeName (NestedTimingMode mode)
{
    switch (mode)
    {
        case NestedTimingMode::followParent: return "Follow";
        case NestedTimingMode::freeRun: return "Free-run";
        case NestedTimingMode::oneShot: return "One-shot";
        case NestedTimingMode::latch: return "Latch";
    }

    return "Follow";
}

struct Lane
{
    Lane() = default;

    Lane (juce::String idToUse, juce::String nameToUse, juce::String scriptToUse)
        : id (std::move (idToUse)), name (std::move (nameToUse)), script (std::move (scriptToUse))
    {
    }

    juce::String id;
    juce::String name;
    juce::String script;
    float volume = 1.0f;
    float gain = 1.0f;
    float pan = 0.0f;
    bool enabled = true;
    bool muted = false;
    bool solo = false;
    bool frozen = false;
    bool freezeStale = false;
    bool freezeInProgress = false;
    juce::String frozenAudioPath;
    bool playing = false;
    int preparedBridge = -1;
};

struct LaneSnapshot
{
    juce::String id;
    juce::String name;
    juce::String script;
    float volume = 1.0f;
    float gain = 1.0f;
    float pan = 0.0f;
    bool frozen = false;
    bool freezeStale = false;
    juce::String frozenAudioPath;
};

struct State
{
    int index = 0;
    juce::String name;
    std::vector<Lane> lanes;
    double tempoBpm = 120.0;
    int beatsPerBar = 4;
    int beatUnit = 4;
    int arrangementBars = 1;

    double secondsPerBar() const
    {
        const auto bpm = juce::jlimit (20.0, 320.0, tempoBpm);
        const auto beats = juce::jlimit (1, 32, beatsPerBar);
        const auto unit = juce::jlimit (1, 32, beatUnit);
        return (60.0 / bpm) * static_cast<double> (beats) * (4.0 / static_cast<double> (unit));
    }

    double clockBeatsPerSection() const
    {
        const auto beats = juce::jlimit (1, 32, beatsPerBar);
        const auto unit = juce::jlimit (1, 32, beatUnit);
        const auto bars = juce::jlimit (1, 64, arrangementBars);
        return static_cast<double> (beats) * (4.0 / static_cast<double> (unit)) * static_cast<double> (bars);
    }

    double secondsPerSection() const
    {
        return secondsPerBar() * static_cast<double> (juce::jlimit (1, 64, arrangementBars));
    }
};

struct Rule
{
    int from = 0;
    int to = 0;
    float weight = 1.0f;
};

class MachineModel
{
public:
    explicit MachineModel (juce::String machineIdToUse = "root", juce::String lanePrefixToUse = "")
        : machineId (std::move (machineIdToUse)), lanePrefix (std::move (lanePrefixToUse))
    {
        setStateCount (5);
        regenerateRingRules();
        if (machineId == "root" && lanePrefix.isEmpty())
            configureRootDemo();
        else
            configureDefaultChildDemo();
    }

    ~MachineModel() = default;
    MachineModel (MachineModel&&) noexcept = default;
    MachineModel& operator= (MachineModel&&) noexcept = default;
    MachineModel (const MachineModel&) = delete;
    MachineModel& operator= (const MachineModel&) = delete;

    void setStateCount (int newCount)
    {
        newCount = juce::jlimit (1, maxStateCount, newCount);
        const auto oldSize = static_cast<int> (states.size());
        states.resize (static_cast<size_t> (newCount));
        childMachines.resize (static_cast<size_t> (newCount));

        for (int i = oldSize; i < newCount; ++i)
        {
            states[static_cast<size_t> (i)].index = i;
            states[static_cast<size_t> (i)].name = "State " + juce::String (i + 1);
            states[static_cast<size_t> (i)].lanes.push_back (
                { makeLaneId (i, 0), "Lane 1", MarkovDemo::defaultScriptFor (i, 0) });
        }

        for (int i = 0; i < newCount; ++i)
            states[static_cast<size_t> (i)].index = i;

        rules.erase (std::remove_if (rules.begin(), rules.end(), [newCount] (const Rule& rule)
        {
            return rule.from >= newCount || rule.to >= newCount;
        }), rules.end());

        selectedState = juce::jlimit (0, newCount - 1, selectedState);
        selectedLane = juce::jlimit (0, getLaneCount (selectedState) - 1, selectedLane);
        entryState = juce::jlimit (0, newCount - 1, entryState);
    }

    int getStateCount() const { return static_cast<int> (states.size()); }
    int getLaneCount (int stateIndex) const { return static_cast<int> (states[static_cast<size_t> (stateIndex)].lanes.size()); }

    State& state (int index) { return states[static_cast<size_t> (index)]; }
    const State& state (int index) const { return states[static_cast<size_t> (index)]; }

    Lane& selectedLaneRef()
    {
        return states[static_cast<size_t> (selectedState)].lanes[static_cast<size_t> (selectedLane)];
    }

    void addLaneToSelectedState()
    {
        auto& s = state (selectedState);
        const auto laneIndex = static_cast<int> (s.lanes.size());
        s.lanes.push_back ({ makeLaneId (selectedState, laneIndex),
                             "Lane " + juce::String (laneIndex + 1),
                             MarkovDemo::defaultScriptFor (selectedState, laneIndex) });
        selectedLane = laneIndex;
    }

    void removeSelectedLane()
    {
        auto& s = state (selectedState);
        if (s.lanes.size() <= 1)
            return;

        s.lanes.erase (s.lanes.begin() + selectedLane);
        selectedLane = juce::jlimit (0, static_cast<int> (s.lanes.size()) - 1, selectedLane);
    }

    void moveSelectedLane (int offset)
    {
        auto& s = state (selectedState);
        const auto count = static_cast<int> (s.lanes.size());
        const auto target = juce::jlimit (0, count - 1, selectedLane + offset);
        if (target == selectedLane)
            return;

        std::swap (s.lanes[static_cast<size_t> (selectedLane)], s.lanes[static_cast<size_t> (target)]);
        selectedLane = target;
    }

    void regenerateRingRules()
    {
        rules.clear();
        const auto count = getStateCount();
        for (int i = 0; i < count; ++i)
            rules.push_back ({ i, (i + 1) % count, 1.0f });
    }

    juce::String makeLaneId (int stateIndex, int laneIndex) const
    {
        return lanePrefix + "s" + juce::String (stateIndex) + "-l" + juce::String (laneIndex);
    }

    bool hasChildMachine (int stateIndex) const
    {
        return childMachines[static_cast<size_t> (stateIndex)] != nullptr;
    }

    MachineModel* childMachine (int stateIndex)
    {
        return childMachines[static_cast<size_t> (stateIndex)].get();
    }

    const MachineModel* childMachine (int stateIndex) const
    {
        return childMachines[static_cast<size_t> (stateIndex)].get();
    }

    MachineModel& addChildToSelectedState()
    {
        auto childId = machineId + "_state" + juce::String (selectedState) + "_child";
        auto childPrefix = lanePrefix + "n" + juce::String (selectedState) + "-";
        childMachines[static_cast<size_t> (selectedState)] = std::make_unique<MachineModel> (childId, childPrefix);
        return *childMachines[static_cast<size_t> (selectedState)];
    }

    void removeChildFromSelectedState()
    {
        childMachines[static_cast<size_t> (selectedState)] = nullptr;
    }

    void configureRootDemo()
    {
        setStateCount (10);
        childMachines.clear();
        childMachines.resize (states.size());

        setStateDemo (0, "Breath", { { "Ground sine", "radiguecore" }, { "Low beating", "radiguebeating" }, { "Room air", "radigueair" } });
        setStateDemo (1, "First partial", { { "Fundamental", "radiguecore" }, { "Upper beat", "radiguebeating" }, { "Narrow air", "radigueair" } });
        setStateDemo (2, "Interference", { { "Beating pair", "radiguebeating" }, { "Formant veil", "radigueformant" }, { "Undertone", "radiguelow" }, { "Air thread", "radigueair" } });
        setStateDemo (3, "Long veil", { { "Veil body", "radiguecore" }, { "Slow formant", "radigueformant" }, { "Dust band", "radigueair" } });
        setStateDemo (4, "Narrowing", { { "Close tone", "radiguebeating" }, { "Filter line", "radigueformant" }, { "Low cloud", "radiguelow" } });
        setStateDemo (5, "Bloom", { { "Bloom core", "radiguecore" }, { "Wide harmonic", "radigueharmonic" }, { "Bloom beat", "radiguebeating" }, { "Bloom air", "radigueair" }, { "Soft formant", "radigueformant" } });
        setStateDemo (6, "Still centre", { { "Centre tone", "radiguecore" }, { "Harmonic field", "radigueharmonic" }, { "Sub breath", "radiguelow" }, { "Almost air", "radigueair" } });
        setStateDemo (7, "Low cloud", { { "Deep partial", "radiguelow" }, { "Cloud beating", "radiguebeating" }, { "Muted formant", "radigueformant" } });
        setStateDemo (8, "Bright thread", { { "Thin harmonic", "radigueformant" }, { "Thread beat", "radiguebeating" }, { "High air", "radigueair" } });
        setStateDemo (9, "Return", { { "Return core", "radiguecore" }, { "Return low", "radiguelow" }, { "Returning air", "radigueair" } });

        setStateTiming (0, 42.0, 4, 4);
        setStateTiming (1, 44.0, 4, 4);
        setStateTiming (2, 40.0, 5, 4);
        setStateTiming (3, 38.0, 4, 4);
        setStateTiming (4, 46.0, 3, 4);
        setStateTiming (5, 42.0, 6, 4);
        setStateTiming (6, 36.0, 4, 4);
        setStateTiming (7, 39.0, 5, 4);
        setStateTiming (8, 48.0, 4, 4);
        setStateTiming (9, 41.0, 4, 4);

        setStateArrangementBars (0, 4);
        setStateArrangementBars (1, 4);
        setStateArrangementBars (2, 5);
        setStateArrangementBars (3, 6);
        setStateArrangementBars (4, 4);
        setStateArrangementBars (5, 6);
        setStateArrangementBars (6, 5);
        setStateArrangementBars (7, 5);
        setStateArrangementBars (8, 4);
        setStateArrangementBars (9, 6);

        rules = {
            { 0, 0, 5.0f }, { 0, 1, 1.0f },
            { 1, 1, 4.0f }, { 1, 2, 1.0f },
            { 2, 2, 6.0f }, { 2, 3, 1.0f }, { 2, 5, 0.35f },
            { 3, 3, 7.0f }, { 3, 4, 1.0f },
            { 4, 4, 4.0f }, { 4, 5, 1.0f },
            { 5, 5, 8.0f }, { 5, 6, 1.0f }, { 5, 8, 0.45f },
            { 6, 6, 7.0f }, { 6, 7, 1.0f },
            { 7, 7, 5.0f }, { 7, 8, 1.0f },
            { 8, 8, 4.0f }, { 8, 9, 1.0f },
            { 9, 9, 6.0f }, { 9, 0, 1.0f }
        };

        childMachines[2] = std::make_unique<MachineModel> (machineId + "_interference_child", lanePrefix + "interference-");
        auto& interference = *childMachines[2];
        interference.setStateCount (5);
        interference.timingMode = NestedTimingMode::followParent;
        interference.parentDivision = 4;
        interference.setStateDemo (0, "Left drift", { { "Left pair", "radiguebeating" }, { "Left air", "radigueair" } });
        interference.setStateDemo (1, "Right drift", { { "Right pair", "radiguebeating" }, { "Right formant", "radigueformant" } });
        interference.setStateDemo (2, "Low fold", { { "Fold low", "radiguelow" }, { "Fold core", "radiguecore" } });
        interference.setStateDemo (3, "Still band", { { "Band formant", "radigueformant" }, { "Band air", "radigueair" } });
        interference.setStateDemo (4, "Return beat", { { "Return pair", "radiguebeating" } });
        interference.rules = { { 0, 0, 3.0f }, { 0, 1, 1.0f }, { 1, 1, 3.0f }, { 1, 2, 0.8f }, { 1, 3, 0.5f }, { 2, 4, 1.0f }, { 3, 4, 1.0f }, { 4, 0, 1.0f } };
        interference.setAllLaneVolumes (0.36f);

        interference.childMachines[1] = std::make_unique<MachineModel> (interference.machineId + "_slow_beads", interference.lanePrefix + "beads-");
        auto& beads = *interference.childMachines[1];
        beads.setStateCount (3);
        beads.timingMode = NestedTimingMode::freeRun;
        beads.parentDivision = 6;
        beads.setStateDemo (0, "Bead A", { { "Bead tone", "radiguebeating" } });
        beads.setStateDemo (1, "Bead B", { { "Bead air", "radigueair" } });
        beads.setStateDemo (2, "Bead C", { { "Bead formant", "radigueformant" } });
        beads.rules = { { 0, 0, 4.0f }, { 0, 1, 1.0f }, { 1, 1, 4.0f }, { 1, 2, 1.0f }, { 2, 0, 1.0f } };
        beads.setAllLaneVolumes (0.26f);

        childMachines[5] = std::make_unique<MachineModel> (machineId + "_bloom_child", lanePrefix + "bloom-");
        auto& bloom = *childMachines[5];
        bloom.setStateCount (4);
        bloom.timingMode = NestedTimingMode::followParent;
        bloom.parentDivision = 5;
        bloom.setStateDemo (0, "Opening", { { "Opening core", "radiguecore" }, { "Opening air", "radigueair" } });
        bloom.setStateDemo (1, "Widen", { { "Widen pair", "radiguebeating" }, { "Widen formant", "radigueformant" } });
        bloom.setStateDemo (2, "Held light", { { "Held core", "radiguecore" }, { "Held harmonic", "radigueharmonic" }, { "Held air", "radigueair" } });
        bloom.setStateDemo (3, "Settle", { { "Settle low", "radiguelow" }, { "Settle beat", "radiguebeating" } });
        bloom.rules = { { 0, 0, 4.0f }, { 0, 1, 1.0f }, { 1, 1, 4.0f }, { 1, 2, 1.0f }, { 2, 2, 5.0f }, { 2, 3, 1.0f }, { 3, 0, 1.0f } };
        bloom.setAllLaneVolumes (0.34f);

        childMachines[8] = std::make_unique<MachineModel> (machineId + "_thread_child", lanePrefix + "thread-");
        auto& thread = *childMachines[8];
        thread.setStateCount (3);
        thread.timingMode = NestedTimingMode::freeRun;
        thread.parentDivision = 3;
        thread.setStateDemo (0, "Harmonic", { { "Harmonic formant", "radigueformant" } });
        thread.setStateDemo (1, "Dust", { { "Dust air", "radigueair" } });
        thread.setStateDemo (2, "Beat", { { "Fine beat", "radiguebeating" } });
        thread.rules = { { 0, 0, 5.0f }, { 0, 1, 1.0f }, { 1, 1, 4.0f }, { 1, 2, 1.0f }, { 2, 0, 1.0f } };
        thread.setAllLaneVolumes (0.28f);

        selectedState = 0;
        selectedLane = 0;
    }

    void configureDefaultChildDemo()
    {
        setStateCount (4);
        childMachines.clear();
        childMachines.resize (states.size());
        setStateDemo (0, "Cell A", { { "Accent", "arp" } });
        setStateDemo (1, "Cell B", { { "Colour", "shimmer" } });
        setStateDemo (2, "Cell C", { { "Answer", "phrase" } });
        setStateDemo (3, "Cell D", { { "Bed", "texture" } });
        rules = { { 0, 1, 1.0f }, { 1, 2, 0.8f }, { 1, 3, 0.35f }, { 2, 0, 1.0f }, { 3, 0, 1.0f } };
    }

    void configureGrooveChild (MachineModel& child)
    {
        child.setStateCount (4);
        child.timingMode = NestedTimingMode::freeRun;
        child.parentDivision = 2;
        child.setStateDemo (0, "Motif A", { { "Tiny lead", "lead" }, { "Air", "texture" } });
        child.setStateDemo (1, "Motif B", { { "Tiny answer", "counter" }, { "Small chords", "chords" } });
        child.setStateDemo (2, "Motif C", { { "Bell lead", "lead" }, { "Upper reply", "shimmer" } });
        child.setStateDemo (3, "Motif D", { { "Turn line", "lead" } });
        child.rules = { { 0, 0, 4.0f }, { 0, 1, 1.0f }, { 1, 1, 4.0f }, { 1, 2, 1.0f }, { 2, 2, 4.0f }, { 2, 3, 1.0f }, { 3, 0, 1.0f } };

        child.childMachines[1] = std::make_unique<MachineModel> (child.machineId + "_figure_b_child", child.lanePrefix + "figB-");
        configureMicroArpChild (*child.childMachines[1], "Figure B Cells", NestedTimingMode::followParent, 2);

        child.childMachines[2] = std::make_unique<MachineModel> (child.machineId + "_figure_c_child", child.lanePrefix + "figC-");
        configureMicroAnswerChild (*child.childMachines[2], "Figure C Cells", NestedTimingMode::freeRun, 3);
    }

    void configureBloomChild (MachineModel& child)
    {
        child.setStateCount (3);
        child.timingMode = NestedTimingMode::followParent;
        child.parentDivision = 4;
        child.setStateDemo (0, "Hook fifth", { { "Fifth lead", "lead" } });
        child.setStateDemo (1, "High answer", { { "Glass answer", "shimmer" }, { "Air", "texture" } });
        child.setStateDemo (2, "Fold down", { { "Fold melody", "phrase" } });
        child.rules = { { 0, 1, 1.0f }, { 1, 1, 0.4f }, { 1, 2, 1.0f }, { 2, 0, 1.0f } };

        child.childMachines[1] = std::make_unique<MachineModel> (child.machineId + "_motes_child", child.lanePrefix + "motes-");
        configureMicroArpChild (*child.childMachines[1], "Mote Cells", NestedTimingMode::freeRun, 4);
    }

    void configureFractureChild (MachineModel& child)
    {
        child.setStateCount (5);
        child.timingMode = NestedTimingMode::followParent;
        child.parentDivision = 4;
        child.setStateDemo (0, "Question", { { "Question lead", "lead" } });
        child.setStateDemo (1, "Lift", { { "Lift lead", "lead" }, { "Edge shimmer", "shimmer" } });
        child.setStateDemo (2, "Drop", { { "Drop melody", "phrase" }, { "Drop answer", "counter" } });
        child.setStateDemo (3, "Suspension", { { "Suspended lead", "lead" }, { "Chord shade", "chords" } });
        child.setStateDemo (4, "Exit", { { "Exit lead", "lead" } });
        child.rules = { { 0, 1, 1.0f }, { 1, 2, 0.75f }, { 1, 3, 0.8f }, { 2, 4, 1.0f }, { 3, 4, 1.0f }, { 4, 0, 1.0f } };

        child.childMachines[1] = std::make_unique<MachineModel> (child.machineId + "_lift_child", child.lanePrefix + "lift-");
        configureMicroArpChild (*child.childMachines[1], "Lift Cells", NestedTimingMode::oneShot, 1);

        child.childMachines[3] = std::make_unique<MachineModel> (child.machineId + "_suspension_child", child.lanePrefix + "susp-");
        configureMicroAnswerChild (*child.childMachines[3], "Suspension Cells", NestedTimingMode::followParent, 2);

        child.entryState = 0;
        child.selectedState = 0;
        child.selectedLane = 0;
    }

    void configureMicroArpChild (MachineModel& child, const juce::String& name, NestedTimingMode mode, int division)
    {
        juce::ignoreUnused (name);
        child.setStateCount (3);
        child.timingMode = mode;
        child.parentDivision = division;
        child.entryState = 0;
        child.setStateDemo (0, "Spark", { { "Tiny hook", "lead" } });
        child.setStateDemo (1, "Fold", { { "Glass hook", "shimmer" } });
        child.setStateDemo (2, "Return", { { "Hook return", "phrase" } });
        child.rules = { { 0, 0, 2.5f }, { 0, 1, 1.0f }, { 1, 1, 1.8f }, { 1, 2, 1.0f }, { 2, 0, 1.0f } };
        child.setAllLaneVolumes (0.42f);
    }

    void configureMicroAnswerChild (MachineModel& child, const juce::String& name, NestedTimingMode mode, int division)
    {
        juce::ignoreUnused (name);
        child.setStateCount (4);
        child.timingMode = mode;
        child.parentDivision = division;
        child.entryState = 0;
        child.setStateDemo (0, "Answer A", { { "Answer hook", "lead" } });
        child.setStateDemo (1, "Answer B", { { "Small melody", "phrase" } });
        child.setStateDemo (2, "Answer C", { { "Glass reply", "shimmer" } });
        child.setStateDemo (3, "Rest", { { "Thin air", "texture" } });
        child.rules = { { 0, 1, 1.0f }, { 1, 2, 0.8f }, { 1, 3, 0.35f }, { 2, 0, 1.0f }, { 3, 0, 1.0f } };
        child.setAllLaneVolumes (0.36f);
    }

    void setAllLaneVolumes (float volume)
    {
        const auto clipped = juce::jlimit (0.0f, 1.0f, volume);
        for (auto& stateToScale : states)
            for (auto& lane : stateToScale.lanes)
                lane.volume = clipped;
    }

    void setStateDemo (int stateIndex, std::initializer_list<std::pair<const char*, const char*>> laneDefs)
    {
        auto& s = state (stateIndex);
        s.name = s.name.isEmpty() ? "State " + juce::String (stateIndex + 1) : s.name;
        s.lanes.clear();
        int laneIndex = 0;
        for (const auto& lane : laneDefs)
        {
            auto role = juce::String (lane.second);
            Lane demoLane { makeLaneId (stateIndex, laneIndex),
                            lane.first,
                            MarkovDemo::scriptForRole (role, stateIndex, laneIndex) };
            demoLane.volume = MarkovDemo::volumeForRole (role);
            s.lanes.push_back (std::move (demoLane));
            ++laneIndex;
        }

        if (s.lanes.empty())
            s.lanes.push_back ({ makeLaneId (stateIndex, 0), "Lane 1", MarkovDemo::defaultScriptFor (stateIndex, 0) });
    }

    void setStateDemo (int stateIndex, const juce::String& name, std::initializer_list<std::pair<const char*, const char*>> laneDefs)
    {
        state (stateIndex).name = name;
        setStateDemo (stateIndex, laneDefs);
    }

    void setStateTiming (int stateIndex, double bpm, int beats, int unit)
    {
        auto& s = state (stateIndex);
        s.tempoBpm = juce::jlimit (20.0, 320.0, bpm);
        s.beatsPerBar = juce::jlimit (1, 32, beats);
        s.beatUnit = juce::jlimit (1, 32, unit);
    }

    void setStateArrangementBars (int stateIndex, int bars)
    {
        state (stateIndex).arrangementBars = juce::jlimit (1, 64, bars);
    }

    std::vector<State> states;
    std::vector<std::unique_ptr<MachineModel>> childMachines;
    std::vector<Rule> rules;
    juce::String machineId;
    juce::String lanePrefix;
    NestedTimingMode timingMode = NestedTimingMode::followParent;
    int parentDivision = 1;
    int parentTickCounter = 0;
    bool oneShotComplete = false;
    bool latchedActive = false;
    int selectedState = 0;
    int selectedLane = 0;
    int entryState = 0;
    int stepsSinceEntry = 0;
};
