(
{
    |gate = 1, fade = 0.12, vol = 1|

    var bpm = ((~ofTempoHz ? ~markovTempoHz ? (72 / 60)) * 60).clip(30, 200);
    var tempo = (bpm / 60).max(0.05);
    var root = NamedControl.kr(\root, 50);
    var shiftTrig = Impulse.kr(tempo / 6);
    var stepIndex = (PulseCount.kr(shiftTrig) - 1).max(0);
    var shift = Select.kr(stepIndex % 6, [0, 5, 3, -2, 7, 3]);
    var notes = (root + shift + [0, 3, 7, 10, 14]).clip(24, 108).midicps;
    var env = EnvGen.kr(Env.asr(fade.max(0.03), 1, fade.max(0.06)), gate, doneAction: 2);
    var sig = Formant.ar(notes, notes * [1.6, 1.35, 1.42, 1.28, 1.55], notes * 0.22, 0.045).sum;
    sig = LPF.ar(sig, SinOsc.kr(tempo / 18).range(900, 3400));
    sig = FreeVerb2.ar(sig, DelayC.ar(sig, 0.04, 0.019), 0.35, 0.72, 0.35);

    Limiter.ar(LeakDC.ar(sig) * env * vol, 0.76);
}.play;
)
