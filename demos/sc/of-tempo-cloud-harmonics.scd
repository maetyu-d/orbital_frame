(
{
    |gate = 1, fade = 0.2, vol = 1|

    var bpm = 72;
    var tempo = (~ofTempoHz ? ~markovTempoHz ? (bpm / 60)).max(0.05);
    var bar = Impulse.kr(tempo / 4);
    var halfBar = Impulse.kr(tempo / 8);
    var open = EnvGen.kr(Env.asr(fade, 1, fade), gate, doneAction: 2);
    var root = Demand.kr(halfBar, 0, Dseq([41, 48, 53, 55, 46, 50, 57, 55].midicps, inf));
    var rise = Decay2.kr(bar, 0.12, (3.2 / tempo).clip(1.0, 6.0));
    var drift = LFNoise1.kr([tempo / 12, tempo / 10, tempo / 9, tempo / 7]).range(-0.018, 0.018);
    var partials = SinOsc.ar(root * ([1, 1.5, 2, 2.5] + drift), 0, [0.18, 0.12, 0.08, 0.055]).sum;
    var veil = RLPF.ar(PinkNoise.ar(0.18), 900 + (rise * 2800) + LFNoise1.kr(tempo / 16).range(0, 1200), 0.22);
    var shimmer = BPF.ar(WhiteNoise.ar(0.05), root * [8, 12, 16], 0.04).sum * rise;
    var wash = CombC.ar(partials + veil + shimmer, 2.0, (1.5 / tempo).clip(0.1, 1.6), 5.5);
    var sig = ((partials * 0.42) + (veil * 0.22) + (shimmer * 0.18) + (wash * 0.38)) * open;

    Limiter.ar(Splay.ar(sig, 0.88), 0.82) * vol * 0.62;
}.play;
)
