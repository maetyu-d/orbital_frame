(
{
    |gate = 1, fade = 0.005, vol = 1|

    var hit = EnvGen.kr(Env.perc(0.001, 0.07, curve: -9), gate, doneAction: 2);
    var impulse = PinkNoise.ar(0.42) * EnvGen.kr(Env.perc(0.001, 0.008, curve: -10), gate);
    var wood = Ringz.ar(impulse, [760, 1440, 2360], [0.055, 0.040, 0.025]).sum;
    var tick = BPF.ar(WhiteNoise.ar(0.20), 3800, 0.45) * hit;
    var sig = (wood * 0.68) + tick;

    Limiter.ar(Pan2.ar(sig, 0.04) * vol, 0.82);
}.play;
)
