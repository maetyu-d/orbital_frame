(
{
    |gate = 1, fade = 0.01, vol = 1|

    var env = EnvGen.kr(Env.perc(0.006, 1.15, curve: -4), gate, doneAction: 2);
    var sweep = XLine.kr(8200, 520, 1.05);
    var noise = PinkNoise.ar(0.7);
    var filtered = RHPF.ar(RLPF.ar(noise, sweep, 0.18), sweep * 0.28, 0.22);
    var shimmer = BPF.ar(WhiteNoise.ar(0.22), [3600, 5400, 7400], 0.06).sum;
    var pan = SinOsc.kr(0.35).range(-0.45, 0.45);
    var sig = (filtered + (shimmer * 0.28)) * env;

    Limiter.ar(Pan2.ar(sig, pan), 0.78) * vol;
}.play;
)
