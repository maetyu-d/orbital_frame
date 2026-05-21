(
{
    |gate = 1, fade = 0.005, vol = 1|

    var burst = EnvGen.kr(Env([0, 1, 0.25, 1, 0.18, 1, 0], [0.001, 0.018, 0.010, 0.022, 0.016, 0.11], [-4, -7]), gate, doneAction: 2);
    var air = BPF.ar(WhiteNoise.ar(0.85), 1550, 0.72) + HPF.ar(WhiteNoise.ar(0.18), 5200);
    var room = CombC.ar(air * burst, 0.055, [0.019, 0.026], 0.18).sum * 0.18;
    var sig = (air * burst * 0.50) + room;

    Limiter.ar(Splay.ar(sig, 0.36) * vol, 0.84);
}.play;
)
