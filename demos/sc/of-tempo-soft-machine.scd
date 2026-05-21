(
{
    |gate = 1, fade = 0.08, vol = 1|

    var bpm = 124;
    var tempo = (~ofTempoHz ? ~markovTempoHz ? (bpm / 60)).max(0.05);
    var tick = Impulse.kr(tempo * 4);
    var beat = Impulse.kr(tempo);
    var step = PulseCount.kr(tick) % 16;
    var open = EnvGen.kr(Env.asr(fade, 1, fade), gate, doneAction: 2);
    var root = Demand.kr(beat, 0, Dseq([38, 38, 45, 38, 50, 45, 43, 45].midicps, inf));
    var mask = Select.kr(step, [1, 0, 0.25, 0, 0.8, 0, 0.2, 0.35, 1, 0, 0.45, 0, 0.7, 0.25, 0, 0.5]);
    var env = Decay2.kr(tick, 0.003, (0.12 / tempo).clip(0.025, 0.22)) * mask;
    var bassEnv = Decay2.kr(beat, 0.01, (0.42 / tempo).clip(0.08, 0.52));
    var bass = SinOsc.ar(root * [1, 1.002]) * bassEnv * 0.45;
    var pulse = VarSaw.ar(root * [2, 2.005], 0, 0.18, 0.22).sum;
    var filter = RLPF.ar(pulse, 460 + (env * 3200) + (LFNoise1.kr(tempo / 8).range(0, 900)), 0.16);
    var hat = HPF.ar(WhiteNoise.ar(0.12), 5200) * env;
    var sig = (bass + (filter * env) + hat) * open;

    Limiter.ar(Splay.ar(sig, 0.55), 0.86) * vol * 0.72;
}.play;
)
