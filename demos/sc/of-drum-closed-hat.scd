(
{
    |gate = 1, fade = 0.005, vol = 1|

    var env = EnvGen.kr(Env.perc(0.001, 0.055, curve: -8), gate, doneAction: 2);
    var metal = Ringz.ar(HPF.ar(WhiteNoise.ar(0.38), 6500), [7400, 9700, 12200], [0.018, 0.013, 0.010]).sum;
    var sand = HPF.ar(WhiteNoise.ar(0.32), 7600);
    var sig = (metal * 0.28 + sand) * env;

    Limiter.ar(Splay.ar(sig, 0.18) * vol, 0.72);
}.play;
)
