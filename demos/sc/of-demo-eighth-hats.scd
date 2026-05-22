(
{
    |gate = 1, fade = 0.005, vol = 1|

    var bpm = (~ofTempoHz ? ~markovTempoHz ? (120 / 60)).max(0.05) * 60;
    var tempo = bpm / 60;
    var eighth = Impulse.kr(tempo * 2);
    var accent = PulseDivider.kr(eighth, 4);
    var tickEnv = Decay2.kr(eighth, 0.001, 0.035);
    var accentEnv = Decay2.kr(accent, 0.001, 0.075);
    var shimmer = HPF.ar(WhiteNoise.ar(0.34), 7200);
    var metal = Ringz.ar(shimmer, [8200, 10400, 12800], [0.014, 0.011, 0.009]).sum * 0.16;
    var closed = (shimmer * 0.55 + metal) * tickEnv;
    var lift = HPF.ar(PinkNoise.ar(0.16), 9000) * accentEnv * 0.42;
    var body = closed + lift;
    var env = EnvGen.kr(Env.asr(fade.max(0.001), 1, fade.max(0.001)), gate, doneAction: 2);

    Limiter.ar(Splay.ar(body, 0.22) * env * vol, 0.72);
}.play;
)
