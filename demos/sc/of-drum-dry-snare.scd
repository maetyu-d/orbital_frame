(
{
    |gate = 1, fade = 0.005, vol = 1|

    var snap = EnvGen.kr(Env.perc(0.001, 0.045, curve: -9), gate);
    var tail = EnvGen.kr(Env.perc(0.003, 0.18, curve: -5), gate, doneAction: 2);
    var noise = BPF.ar(WhiteNoise.ar(0.82), 1850, 0.62) * tail;
    var body = SinOsc.ar(182, 0, 0.30) * EnvGen.kr(Env.perc(0.001, 0.10, curve: -7), gate);
    var crack = HPF.ar(WhiteNoise.ar(0.55), 5200) * snap;
    var sig = (noise * 0.54) + body + (crack * 0.24);

    Limiter.ar(Splay.ar(sig, 0.12) * vol, 0.86);
}.play;
)
