(
{
    |gate = 1, fade = 0.08, vol = 1|

    var bpm = ((~ofTempoHz ? ~markovTempoHz ? (84 / 60)) * 60).clip(30, 220);
    var tempo = (bpm / 60).max(0.05);
    var root = NamedControl.kr(\root, 55);
    var stepTrig = Impulse.kr(tempo / 4);
    var stepIndex = (PulseCount.kr(stepTrig) - 1).max(0);
    var step = Select.kr(stepIndex % 8, [0, -2, 3, 5, 7, 5, 3, -2]);
    var chord = (root + step + [0, 7, 12, 16, 19]).clip(24, 108);
    var drift = LFNoise1.kr([0.08, 0.11, 0.13, 0.17, 0.19]).range(-0.045, 0.045);
    var env = EnvGen.kr(Env.asr(fade.max(0.02), 1, fade.max(0.04)), gate, doneAction: 2);
    var sig = VarSaw.ar((chord + drift).midicps, 0, LFNoise1.kr(0.07 ! 5).range(0.28, 0.58), 0.08).sum;
    sig = RLPF.ar(sig, SinOsc.kr(tempo / 16).range(720, 2600), 0.42);
    sig = sig + (PitchShift.ar(sig, 0.18, [1.5, 2.0], 0.01, 0.06).sum * 0.12);

    Limiter.ar(Splay.ar(sig * env, 0.64) * vol, 0.76);
}.play;
)
