(
{
    |gate = 1, fade = 0.08, vol = 1|

    var bpm = 96;
    var tempo = (~ofTempoHz ? ~markovTempoHz ? (bpm / 60)).max(0.05);
    var sixteenth = Impulse.kr(tempo * 4);
    var quarter = Impulse.kr(tempo);
    var bar = Impulse.kr(tempo / 4);
    var step = PulseCount.kr(sixteenth) % 16;
    var accent = Select.kr(step, [1, 0.22, 0.42, 0.18, 0.72, 0.20, 0.38, 0.24, 1, 0.18, 0.55, 0.22, 0.78, 0.24, 0.44, 0.28]);
    var env = Decay2.kr(sixteenth, 0.004, (0.11 / tempo).clip(0.035, 0.22)) * accent;
    var open = EnvGen.kr(Env.asr(fade, 1, fade), gate, doneAction: 2);
    var root = Demand.kr(quarter, 0, Dseq([36, 36, 43, 36, 48, 43, 41, 43].midicps, inf));
    var bass = SinOsc.ar(root * [1, 1.003]) * Decay2.kr(quarter, 0.01, (0.36 / tempo).clip(0.10, 0.55));
    var pluck = RLPF.ar(PinkNoise.ar(0.32), 700 + (env * 2600) + (Decay2.kr(bar, 0.02, 0.75) * 1200), 0.18) * env;
    var shimmer = SinOsc.ar((72.midicps * [1, 1.5, 2.01])).sum * 0.035 * LFTri.kr(tempo / 4).range(0.15, 1);
    var sig = ((bass * 0.42) + pluck + shimmer) * open;

    Limiter.ar(Splay.ar(sig, 0.72), 0.82) * vol * 0.55;
}.play;
)
