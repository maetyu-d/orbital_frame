(
{
    |gate = 1, fade = 0.01, vol = 1|

    var bpm = ((~ofTempoHz ? ~markovTempoHz ? (96 / 60)) * 60).clip(30, 240);
    var tempo = (bpm / 60).max(0.05);
    var root = NamedControl.kr(\root, 43);
    var trig = Impulse.kr(tempo / 2);
    var stepIndex = (PulseCount.kr(trig) - 1).max(0);
    var degree = Select.kr(stepIndex % 8, [0, 0, 7, 3, 5, 2, 10, 7]);
    var octave = Select.kr((stepIndex / 4).floor % 4, [0, -12, 0, 12]);
    var note = (root + degree + octave).clip(20, 96);
    var env = EnvGen.kr(Env.asr(fade.max(0.002), 1, fade.max(0.01)), gate, doneAction: 2);
    var pluck = Decay2.kr(trig, 0.006, 0.18);
    var sig = (Saw.ar(note.midicps * [0.5, 1.0]) * 0.18) + (SinOsc.ar(note.midicps * 0.5) * 0.42);
    sig = LPF.ar(sig, 220 + (pluck * 1200));
    sig = LeakDC.ar(sig) * (0.42 + (pluck * 0.58)) * env;

    Limiter.ar(Splay.ar(sig, 0.18) * vol, 0.82);
}.play;
)
