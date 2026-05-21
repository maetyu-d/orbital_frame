(
{
    |gate = 1, fade = 0.005, vol = 1|

    var amp = EnvGen.kr(Env.perc(0.001, 0.62, curve: -6), gate, doneAction: 2);
    var pitch = EnvGen.kr(Env([86, 42, 31], [0.04, 0.42], [-8, -4]), gate);
    var body = SinOsc.ar(pitch, 0, amp);
    var thump = SinOsc.ar(29) * EnvGen.kr(Env.perc(0.018, 0.52, curve: -5), gate) * 0.38;
    var tap = HPF.ar(WhiteNoise.ar(0.20), 2600) * EnvGen.kr(Env.perc(0.001, 0.018), gate);
    var sig = (body * 0.72) + thump + (tap * 0.12);

    Limiter.ar((LeakDC.ar(sig) ! 2) * vol, 0.9);
}.play;
)
