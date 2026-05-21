(
{
    |gate = 1, fade = 0.08, vol = 1|

    var bpm = 116;
    var tempo = (~ofTempoHz ? ~markovTempoHz ? (bpm / 60)).max(0.05);
    var sixteenth = Impulse.kr(tempo * 4);
    var beat = Impulse.kr(tempo);
    var step = PulseCount.kr(sixteenth) % 16;
    var open = EnvGen.kr(Env.asr(fade, 1, fade), gate, doneAction: 2);
    var note = Demand.kr(beat, 0, Dseq([31, 31, 38, 31, 43, 38, 36, 38, 31, 43, 46, 43, 38, 36, 34, 36].midicps, inf));
    var pattern = Select.kr(step, [1, 0, 0, 0.34, 0.75, 0, 0.22, 0, 1, 0, 0.45, 0, 0.7, 0.28, 0, 0.38]);
    var snap = Decay2.kr(sixteenth, 0.003, (0.11 / tempo).clip(0.025, 0.2)) * pattern;
    var body = Decay2.kr(beat, 0.008, (0.5 / tempo).clip(0.1, 0.65));
    var bass = SinOsc.ar(note * [1, 1.001]) * body * 0.58;
    var edge = RLPF.ar(Saw.ar(note * [1, 2.002], 0.12).sum, 220 + (snap * 2800), 0.2) * snap;
    var ghost = BPF.ar(PinkNoise.ar(0.08), note * 12, 0.08) * Decay2.kr(Impulse.kr(tempo / 2), 0.01, (0.25 / tempo).clip(0.07, 0.45));
    var sig = (bass + edge + ghost) * open;

    Limiter.ar(Splay.ar(sig, 0.42), 0.88) * vol * 0.72;
}.play;
)
