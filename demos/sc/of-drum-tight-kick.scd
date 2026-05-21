(
{
    |gate = 1, fade = 0.005, vol = 1|

    var bodyEnv = EnvGen.kr(Env.perc(0.001, 0.34, curve: -7), gate, doneAction: 2);
    var clickEnv = EnvGen.kr(Env.perc(0.001, 0.014, curve: -9), gate);
    var pitch = EnvGen.kr(Env([112, 54, 39], [0.022, 0.18], [-9, -5]), gate);
    var body = SinOsc.ar(pitch, 0, bodyEnv);
    var click = BPF.ar(WhiteNoise.ar(0.55), 4200, 0.34) * clickEnv;
    var sig = (body * 0.86) + (click * 0.18);

    Limiter.ar((sig ! 2) * vol, 0.92);
}.play;
)
