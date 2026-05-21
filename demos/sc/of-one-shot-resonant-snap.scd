(
{
    |gate = 1, fade = 0.01, vol = 1|

    var hit = EnvGen.kr(Env.perc(0.001, 0.075, curve: -10), gate);
    var tail = EnvGen.kr(Env.perc(0.001, 0.48, curve: -6), gate, doneAction: 2);
    var exciter = HPF.ar(WhiteNoise.ar(0.55), 1800) * hit;
    var rings = Ringz.ar(exciter, [430, 740, 1310, 2240], [0.16, 0.24, 0.18, 0.10]).sum;
    var low = SinOsc.ar(XLine.kr(230, 115, 0.16)) * tail * 0.18;
    var sig = ((rings * 0.42) + low) * (hit + tail);

    Limiter.ar(Splay.ar(sig, 0.18), 0.86) * vol;
}.play;
)
