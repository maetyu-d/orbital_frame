(
{
    |gate = 1, fade = 0.005, vol = 1|

    var env = EnvGen.kr(Env.perc(0.001, 0.38, curve: -5), gate, doneAction: 2);
    var strike = EnvGen.kr(Env.perc(0.001, 0.025, curve: -8), gate);
    var metal = Ringz.ar(HPF.ar(WhiteNoise.ar(0.34), 5200), [6100, 8100, 10300, 13200], [0.12, 0.09, 0.075, 0.05]).sum;
    var hiss = HPF.ar(WhiteNoise.ar(0.24), 7400);
    var sig = ((metal * 0.24) + hiss) * env + (hiss * strike * 0.22);

    Limiter.ar(Splay.ar(sig, 0.32) * vol, 0.74);
}.play;
)
