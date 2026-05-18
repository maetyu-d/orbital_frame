# SuperCollider Lane Catalogue

This catalogue collects lane-ready SuperCollider script patterns for `wf::`.
Each script is designed to be pasted into a lane and run by the app.

## Lane Contract

Use this shape for most lanes:

```supercollider
(
{ |gate=1, fade=0.12, vol=1|
    var active = EnvGen.kr(Env.asr(fade, 1, fade), gate);
    var sig = Silent.ar(2);
    sig * active * vol;
}.play;
)
```

Notes:

- `gate` is controlled by `wf::` when states start and stop.
- `fade` is supplied by the app for state-change smoothing.
- `vol` is the lane volume from the UI.
- Use `~wfTempoHz` for tempo-synced material.
- Keep the final signal stereo.
- Put a limiter near the end for aggressive or stacked material.
- For long drones, use `Lag.kr(gate, seconds)` or a long ASR envelope so tails are not cut off.

## Drones And Spectral Fields

### Radigue Root Body

Slow beating partials with a filtered body. Good as a primary drone lane.

```supercollider
(
{ |gate=1, fade=0.12, vol=1|
    var active = Lag.kr(gate, 14);
    var root = 42.midicps;
    var drift = LFNoise1.kr([0.006, 0.009, 0.013, 0.017]).range(-0.18, 0.18);
    var partials = SinOsc.ar((root * [0.5, 1, 1.5, 2.01]) + drift, 0, [0.30, 0.22, 0.11, 0.055]);
    var body = Splay.ar(partials, 0.26);
    var shade = LPF.ar(body, LFNoise1.kr(0.018).range(420, 1250));
    Limiter.ar(LeakDC.ar(HPF.ar(shade, 22)), 0.20) * active * vol * 0.28;
}.play;
)
```

### Close Beating Tone

Very slow phase movement. Useful for nested micro-states inside drones.

```supercollider
(
{ |gate=1, fade=0.12, vol=1|
    var active = Lag.kr(gate, 18);
    var root = 49.midicps;
    var offsets = [0, 0.07, -0.11, 0.19] + LFNoise1.kr(0.01 ! 4).range(-0.025, 0.025);
    var tones = SinOsc.ar(root + offsets, 0, [0.18, 0.14, 0.12, 0.09]);
    var slow = SinOsc.kr([0.021, 0.034]).range(0.35, 1.0);
    var sig = Splay.ar(tones, 0.34) * slow;
    sig = BLowPass4.ar(sig, LFNoise1.kr(0.014).range(520, 1550), 0.62);
    Limiter.ar(LeakDC.ar(HPF.ar(sig, 30)), 0.16) * active * vol * 0.24;
}.play;
)
```

### Harmonic Swell

A larger, more tonal drone for chorus-like sections.

```supercollider
(
{ |gate=1, fade=0.12, vol=1|
    var active = Lag.kr(gate, 24);
    var root = 47.midicps;
    var chord = root * [0.5, 1, 1.25, 1.5, 2, 2.5];
    var wander = LFNoise1.kr([0.004, 0.006, 0.008, 0.011, 0.013, 0.017]).range(0.997, 1.003);
    var amps = [0.20, 0.16, 0.11, 0.09, 0.055, 0.035]
        * SinOsc.kr([0.015, 0.019, 0.023, 0.029, 0.034, 0.041]).range(0.52, 1.0);
    var bank = SinOsc.ar(chord * wander, 0, amps);
    var sig = Splay.ar(bank, 0.52);
    sig = RLPF.ar(sig, LFNoise1.kr(0.010).range(640, 1650), 0.38);
    sig = sig + (CombC.ar(sig, 0.9, [0.47, 0.61], 2.8) * 0.045);
    Limiter.ar(LeakDC.ar(HPF.ar(LPF.ar(sig, 2400), 28)), 0.18) * active * vol * 0.30;
}.play;
)
```

### Air Band

Quiet high-frequency pressure without harshness.

```supercollider
(
{ |gate=1, fade=0.12, vol=1|
    var active = Lag.kr(gate, 16);
    var centre = LFNoise1.kr(0.012).range(240, 900);
    var air = BPF.ar(PinkNoise.ar(0.10 ! 2), centre, 0.22);
    var tone = SinOsc.ar([132, 134] * LFNoise1.kr(0.006).range(0.998, 1.002), 0, 0.018);
    var sig = LPF.ar(air + tone, 2100);
    Limiter.ar(LeakDC.ar(HPF.ar(sig, 70)), 0.12) * active * vol * 0.20;
}.play;
)
```

## Arpeggios And Melodic Machines

### Berlin Pulse

Classic straight pulse, good for a main arp lane.

```supercollider
(
{ |gate=1, fade=0.12, vol=1|
    var active = EnvGen.kr(Env.asr(fade, 1, fade), gate);
    var trig = Impulse.kr((~wfTempoHz ? 1) * 4, 0);
    var seq = Dseq([48, 55, 60, 67, 72, 67, 60, 55].midicps, inf);
    var freq = Demand.kr(trig, 0, seq);
    var env = EnvGen.kr(Env.perc(0.006, 0.18, curve: -4), trig);
    var sig = VarSaw.ar(freq * [0.997, 1.003], 0, 0.42, 0.22).sum * env;
    sig = RLPF.ar(sig, Decay2.kr(trig, 0.01, 0.16).range(900, 2800), 0.28);
    Pan2.ar(Limiter.ar(LeakDC.ar(sig), 0.30), SinOsc.kr(0.05).range(-0.35, 0.35)) * active * vol * 0.18;
}.play;
)
```

### Counter Arp

Works well against the Berlin pulse in another lane.

```supercollider
(
{ |gate=1, fade=0.12, vol=1|
    var active = EnvGen.kr(Env.asr(fade, 1, fade), gate);
    var trig = Impulse.kr((~wfTempoHz ? 1) * 3, 0);
    var seq = Dseq([72, 76, 79, 83, 81, 79, 76, 74].midicps, inf);
    var freq = Demand.kr(trig, 0, seq);
    var env = EnvGen.kr(Env.perc(0.004, 0.24, curve: -5), trig);
    var sig = SinOsc.ar(freq * [1, 1.002]) + Pulse.ar(freq * 2, 0.38, 0.08);
    sig = Splay.ar(sig * env, 0.42);
    sig = CombC.ar(sig, 0.35, [0.125, 0.188], 1.7) * 0.20 + sig;
    Limiter.ar(LeakDC.ar(HPF.ar(sig, 120)), 0.22) * active * vol * 0.14;
}.play;
)
```

### Glass Hook

A bright melodic layer. Keep its lane volume modest.

```supercollider
(
{ |gate=1, fade=0.12, vol=1|
    var active = EnvGen.kr(Env.asr(fade, 1, fade), gate);
    var trig = Impulse.kr((~wfTempoHz ? 1) * 2, 0);
    var seq = Dseq([76, 79, 83, 86, 84, 83, 79, 76].midicps, inf);
    var freq = Demand.kr(trig, 0, seq);
    var env = EnvGen.kr(Env.perc(0.002, 0.42, curve: -6), trig);
    var sig = SinOsc.ar(freq * [1, 2.01, 3.005], 0, [0.20, 0.08, 0.035]).sum * env;
    sig = HPF.ar(sig, 240);
    sig = FreeVerb2.ar(sig, DelayC.ar(sig, 0.03, 0.017), 0.24, 0.72, 0.28);
    Limiter.ar(LeakDC.ar(sig), 0.18) * active * vol * 0.16;
}.play;
)
```

## Bass And Low Movement

### Warm Step Bass

Tempo-locked but smooth enough not to thump on state changes.

```supercollider
(
{ |gate=1, fade=0.12, vol=1|
    var active = EnvGen.kr(Env.asr(fade, 1, fade), gate);
    var trig = Impulse.kr((~wfTempoHz ? 1), 0);
    var seq = Dseq([36, 36, 43, 34, 36, 48, 41, 43].midicps, inf);
    var freq = Lag.kr(Demand.kr(trig, 0, seq), 0.025);
    var env = EnvGen.kr(Env.perc(0.012, 0.34, curve: -3), trig);
    var sig = SinOsc.ar(freq, 0, 0.32) + Pulse.ar(freq * 0.5, 0.45, 0.18);
    sig = RLPF.ar(sig * env, Decay2.kr(trig, 0.02, 0.22).range(120, 900), 0.30);
    Pan2.ar(Limiter.ar(LeakDC.ar(HPF.ar(sig, 28)), 0.24), 0) * active * vol * 0.24;
}.play;
)
```

### Sub Drone

Use under a drone state or a long breakdown.

```supercollider
(
{ |gate=1, fade=0.12, vol=1|
    var active = Lag.kr(gate, 12);
    var root = 30.midicps;
    var bend = LFNoise1.kr(0.006).range(-0.10, 0.10);
    var sig = SinOsc.ar((root * [0.5, 1.0]) + bend, 0, [0.18, 0.11]);
    sig = LPF.ar(Splay.ar(sig, 0.10), LFNoise1.kr(0.010).range(150, 420));
    sig = Compander.ar(sig, sig, 0.12, 1, 0.65, 0.02, 0.18);
    Limiter.ar(LeakDC.ar(HPF.ar(sig, 24)), 0.15) * active * vol * 0.20;
}.play;
)
```

## Drums And Pulses

### Clean Four-On-The-Floor Kit

Simple, reliable, and not too aggressive.

```supercollider
(
{ |gate=1, fade=0.12, vol=1|
    var active = EnvGen.kr(Env.asr(fade, 1, fade), gate);
    var clock = Impulse.kr((~wfTempoHz ? 1) * 4, 0);
    var step = PulseCount.kr(clock) % 16;
    var kickTrig = clock * ((step % 4) == 0);
    var snareTrig = clock * ((step == 4) + (step == 12));
    var hatTrig = clock * ((step % 2) == 0);
    var kick = SinOsc.ar(48 + EnvGen.kr(Env.perc(0.001, 0.045, 32, -6), kickTrig))
        * EnvGen.kr(Env.perc(0.003, 0.14, curve: -5), kickTrig);
    var snare = BPF.ar(WhiteNoise.ar(0.24), 1800, 0.55)
        * EnvGen.kr(Env.perc(0.002, 0.11), snareTrig);
    var hat = HPF.ar(WhiteNoise.ar(0.08), 6400)
        * EnvGen.kr(Env.perc(0.001, 0.035), hatTrig);
    var sig = (kick * 0.70) + (snare * 0.34) + (hat * 0.12);
    Pan2.ar(Limiter.ar(LeakDC.ar(HPF.ar(sig, 30)), 0.28), 0) * active * vol * 0.24;
}.play;
)
```

### Fractured Click Grid

For glitch sections. It is deliberately quiet.

```supercollider
(
{ |gate=1, fade=0.12, vol=1|
    var active = EnvGen.kr(Env.asr(fade, 1, fade), gate);
    var clock = Impulse.kr((~wfTempoHz ? 1) * 8, 0);
    var pat = Dseq([1,0,1,0, 0,1,0,1, 1,1,0,0, 1,0,1,1], inf);
    var trig = clock * Demand.kr(clock, 0, pat);
    var freq = Demand.kr(trig, 0, Dseq([1200, 900, 1800, 640, 1500, 2100], inf));
    var env = EnvGen.kr(Env.perc(0.001, 0.035, curve: -7), trig);
    var sig = SinOsc.ar(freq * [1, 1.006], 0, 0.08) * env;
    sig = RHPF.ar(sig, Decay2.kr(trig, 0.004, 0.04).range(900, 5200), 0.34);
    Limiter.ar(LeakDC.ar(Splay.ar(sig, 0.64)), 0.13) * active * vol * 0.10;
}.play;
)
```

## Chords And Pads

### Soft PWM Chords

Good as a harmonic support lane, especially with state volumes below 0.6.

```supercollider
(
{ |gate=1, fade=0.12, vol=1|
    var active = EnvGen.kr(Env.asr(fade, 1, fade), gate);
    var root = 48.midicps;
    var chord = root * [1, 1.25, 1.5, 2];
    var width = SinOsc.kr([0.07, 0.09, 0.11, 0.13]).range(0.36, 0.58);
    var sig = Pulse.ar(chord, width, [0.08, 0.06, 0.05, 0.035]);
    sig = Splay.ar(sig, 0.48);
    sig = RLPF.ar(sig, LFNoise1.kr(0.08).range(650, 1800), 0.32);
    sig = FreeVerb2.ar(sig[0], sig[1], 0.24, 0.72, 0.32);
    Limiter.ar(LeakDC.ar(HPF.ar(sig, 80)), 0.22) * active * vol * 0.18;
}.play;
)
```

### Slow Chord Wash

Long, soft state bed.

```supercollider
(
{ |gate=1, fade=0.12, vol=1|
    var active = EnvGen.kr(Env.asr(fade.max(1.5), 1, fade.max(2.0)), gate);
    var root = 41.midicps;
    var freqs = root * [1, 1.2, 1.5, 2, 2.4];
    var sig = SinOsc.ar(freqs * LFNoise1.kr(0.03 ! 5).range(0.997, 1.003), 0, [0.11, 0.08, 0.06, 0.04, 0.03]);
    sig = Splay.ar(sig, 0.55);
    sig = LPF.ar(sig, LFNoise1.kr(0.025).range(700, 1800));
    sig = sig + (CombC.ar(sig, 1.2, [0.37, 0.53], 4.2) * 0.08);
    Limiter.ar(LeakDC.ar(HPF.ar(sig, 45)), 0.18) * active * vol * 0.20;
}.play;
)
```

## Utility Lanes

### Silent Placeholder

Useful when building structure before sound.

```supercollider
(
{ |gate=1, fade=0.12, vol=1|
    Silent.ar(2);
}.play;
)
```

### Meter Test Tone

Quiet tone for checking routing and meters.

```supercollider
(
{ |gate=1, fade=0.12, vol=1|
    var active = EnvGen.kr(Env.asr(fade, 1, fade), gate);
    var sig = SinOsc.ar(220 * [1, 1.005], 0, 0.08);
    sig * active * vol * 0.20;
}.play;
)
```
