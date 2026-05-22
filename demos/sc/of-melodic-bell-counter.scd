(
{
    |gate = 1, fade = 0.006, vol = 1|

    var bpm = ((~ofTempoHz ? ~markovTempoHz ? (120 / 60)) * 60).clip(30, 260);
    var tempo = (bpm / 60).max(0.05);
    var root = NamedControl.kr(\root, 72);
    var density = NamedControl.kr(\density, 0.75);
    var trig = Impulse.kr(tempo * density);
    var stepIndex = (PulseCount.kr(trig) - 1).max(0);
    var degree = Select.kr(stepIndex % 10, [12, 10, 7, 15, 14, 10, 19, 17, 14, 12]);
    var note = (root + degree).clip(24, 112).midicps;
    var env = Decay2.kr(trig, 0.004, 0.42);
    var sig = SinOsc.ar(note * [1, 2.003, 2.997]) * [0.52, 0.22, 0.12];
    sig = sig.sum * env;
    sig = HPF.ar(sig, 420);
    sig = sig + (AllpassC.ar(sig, 0.22, [0.071, 0.097], 1.6).sum * 0.18);
    sig = sig * EnvGen.kr(Env.asr(fade.max(0.002), 1, fade.max(0.01)), gate, doneAction: 2);

    Limiter.ar(Pan2.ar(sig, SinOsc.kr(tempo / 9).range(-0.55, 0.55)) * vol, 0.72);
}.play;
)
