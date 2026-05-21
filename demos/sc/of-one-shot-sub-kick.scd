(
{
    |gate = 1, fade = 0.01, vol = 1|

    var amp = EnvGen.kr(Env.perc(0.001, 0.72, curve: -6), gate, doneAction: 2);
    var click = EnvGen.kr(Env.perc(0.001, 0.018, curve: -8), gate);
    var pitch = EnvGen.kr(Env([92, 48, 34], [0.035, 0.42], [-8, -4]), gate);
    var body = SinOsc.ar(pitch, 0, amp);
    var knock = BPF.ar(WhiteNoise.ar, 2450, 0.42) * click * 0.22;
    var sub = SinOsc.ar(32) * EnvGen.kr(Env.perc(0.018, 0.62, curve: -5), gate) * 0.35;
    var sig = (body * 0.8) + sub + knock;

    Limiter.ar((sig ! 2) * vol, 0.9);
}.play;
)
