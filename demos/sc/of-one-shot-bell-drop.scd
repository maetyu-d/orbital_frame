(
{
    |gate = 1, fade = 0.01, vol = 1|

    var env = EnvGen.kr(Env.perc(0.002, 1.85, curve: -5), gate, doneAction: 2);
    var strike = EnvGen.kr(Env.perc(0.001, 0.035, curve: -9), gate);
    var base = XLine.kr(880, 392, 1.35);
    var partials = SinOsc.ar(base * [1, 1.498, 2.01, 2.72, 3.96], 0, [0.38, 0.22, 0.18, 0.12, 0.08]).sum;
    var chime = Ringz.ar(WhiteNoise.ar(0.06) * strike, [1760, 2635, 3520, 4978], [1.6, 1.2, 0.9, 0.6]).sum * 0.16;
    var sig = ((partials * env) + chime) * 0.58;

    Limiter.ar(Splay.ar(sig, 0.62), 0.82) * vol;
}.play;
)
