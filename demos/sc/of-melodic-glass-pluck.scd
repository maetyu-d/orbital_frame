(
{
    |gate = 1, fade = 0.005, vol = 1|

    var bpm = ((~ofTempoHz ? ~markovTempoHz ? (108 / 60)) * 60).clip(30, 260);
    var tempo = (bpm / 60).max(0.05);
    var root = NamedControl.kr(\root, 60);
    var rateMul = NamedControl.kr(\rateMul, 1.5);
    var trig = Impulse.kr(tempo * rateMul);
    var stepIndex = (PulseCount.kr(trig) - 1).max(0);
    var degree = Select.kr(stepIndex % 12, [0, 7, 10, 14, 12, 7, 5, 3, 0, 12, 15, 19]);
    var octave = Select.kr(stepIndex % 6, [0, 0, 12, 0, -12, 0]);
    var note = (root + degree + octave).clip(24, 108).midicps;
    var exc = PinkNoise.ar(0.22) * Decay2.kr(trig, 0.002, 0.05);
    var tone = Ringz.ar(exc, note * [1, 2.01, 3.01], [0.34, 0.19, 0.11]).sum;
    var env = EnvGen.kr(Env.asr(fade.max(0.002), 1, fade.max(0.01)), gate, doneAction: 2);
    var sig = HPF.ar(tone, 160) * env;
    sig = CombC.ar(sig, 0.32, [0.19, 0.23], 1.8) * 0.18 + sig;

    Limiter.ar(Splay.ar(sig, 0.55) * vol, 0.72);
}.play;
)
