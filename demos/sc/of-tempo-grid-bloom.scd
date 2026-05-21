(
{
    |gate = 1, fade = 0.08, vol = 1|

    var bpm = 108;
    var tempo = (~ofTempoHz ? ~markovTempoHz ? (bpm / 60)).max(0.05);
    var sixteenth = Impulse.kr(tempo * 4);
    var quarter = Impulse.kr(tempo);
    var bar = Impulse.kr(tempo / 4);
    var step = PulseCount.kr(sixteenth) % 16;
    var open = EnvGen.kr(Env.asr(fade, 1, fade), gate, doneAction: 2);
    var accent = Select.kr(step, [1, 0.18, 0.34, 0.22, 0.72, 0.2, 0.46, 0.18, 0.9, 0.2, 0.42, 0.24, 0.82, 0.22, 0.5, 0.3]);
    var note = Demand.kr(quarter, 0, Dseq([48, 55, 60, 64, 67, 64, 60, 55].midicps, inf));
    var env = Decay2.kr(sixteenth, 0.004, (0.18 / tempo).clip(0.035, 0.28)) * accent;
    var bloom = Decay2.kr(bar, 0.03, (1.4 / tempo).clip(0.35, 2.2));
    var pluck = RLPF.ar(PinkNoise.ar(0.22), note * [3.0, 4.01] + (env * 2400), 0.18).sum * env;
    var tone = SinOsc.ar(note * [1, 1.5, 2.01], 0, [0.22, 0.07, 0.045]).sum * bloom;
    var echo = CombC.ar(pluck + tone, 1.0, (0.75 / tempo).clip(0.04, 0.8), 2.8);
    var sig = (pluck + (tone * 0.35) + (echo * 0.28)) * open;

    Limiter.ar(Splay.ar(sig, 0.76), 0.84) * vol * 0.62;
}.play;
)
