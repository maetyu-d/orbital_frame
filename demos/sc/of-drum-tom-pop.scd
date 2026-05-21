(
{
    |gate = 1, fade = 0.005, vol = 1|

    var amp = EnvGen.kr(Env.perc(0.001, 0.30, curve: -6), gate, doneAction: 2);
    var pitch = EnvGen.kr(Env([245, 152, 118], [0.026, 0.18], [-7, -4]), gate);
    var tone = SinOsc.ar(pitch, 0, amp);
    var skin = BPF.ar(WhiteNoise.ar(0.28), 2100, 0.48) * EnvGen.kr(Env.perc(0.001, 0.035), gate);
    var sig = (tone * 0.74) + (skin * 0.20);

    Limiter.ar(Pan2.ar(LeakDC.ar(sig), -0.03) * vol, 0.86);
}.play;
)
