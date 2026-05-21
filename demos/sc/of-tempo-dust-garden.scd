(
{
    |gate = 1, fade = 0.12, vol = 1|

    var bpm = 92;
    var tempo = (~ofTempoHz ? ~markovTempoHz ? (bpm / 60)).max(0.05);
    var eighth = Impulse.kr(tempo * 2);
    var bar = Impulse.kr(tempo / 4);
    var step = PulseCount.kr(eighth) % 16;
    var open = EnvGen.kr(Env.asr(fade, 1, fade), gate, doneAction: 2);
    var accent = Select.kr(step, [1, 0.1, 0.3, 0.16, 0.6, 0.12, 0.38, 0.18, 0.9, 0.08, 0.34, 0.22, 0.72, 0.15, 0.42, 0.25]);
    var root = Demand.kr(bar, 0, Dseq([45, 45, 52, 50, 43, 48, 55, 52].midicps, inf));
    var grainEnv = Decay2.kr(eighth, 0.005, (0.2 / tempo).clip(0.04, 0.32)) * accent;
    var dust = Dust2.ar((tempo * 18).clip(4, 90)) * 0.22;
    var seeds = Ringz.ar(dust, root * [3, 4.5, 6, 7.5], [0.18, 0.12, 0.1, 0.08]).sum;
    var breath = RLPF.ar(PinkNoise.ar(0.28), root * 9 + (grainEnv * 4800), 0.12) * grainEnv;
    var pulse = SinOsc.ar(root * [0.5, 1.005]) * Decay2.kr(bar, 0.02, (1.2 / tempo).clip(0.25, 2.0)) * 0.22;
    var sig = (seeds + breath + pulse) * open;

    Limiter.ar(Splay.ar(sig, 0.82), 0.84) * vol * 0.66;
}.play;
)
