(
{
    |gate = 1, fade = 0.01, vol = 1|

    var env = EnvGen.kr(Env.perc(0.001, 1.25, curve: -7), gate, doneAction: 2);
    var tap = EnvGen.kr(Env.perc(0.001, 0.08, curve: -9), gate);
    var pitch = XLine.kr(1960, 1180, 0.55);
    var fm = SinOsc.ar(pitch + (SinOsc.ar(pitch * 2.01) * 180 * env));
    var air = BPF.ar(WhiteNoise.ar(0.18), [6100, 9100], 0.08).sum * tap;
    var strike = HPF.ar(WhiteNoise.ar(0.08), 3200) * tap;
    var body = Ringz.ar(strike, [1180, 1770, 2710, 3980], [1.2, 0.9, 0.7, 0.45]).sum * 0.18;
    var sig = ((fm * 0.28) + body + air) * env;

    Limiter.ar(Splay.ar(sig, 0.35), 0.85) * vol;
}.play;
)
