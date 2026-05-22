(
{
    |gate = 1, fade = 0.012, vol = 1|

    var bpm = ((~ofTempoHz ? ~markovTempoHz ? (126 / 60)) * 60).clip(30, 260);
    var tempo = (bpm / 60).max(0.05);
    var root = NamedControl.kr(\root, 62);
    var trig = Impulse.kr(tempo * 2);
    var stepIndex = (PulseCount.kr(trig) - 1).max(0);
    var seq = Select.kr(stepIndex % 12, [0, 3, 7, 10, 12, 15, 10, 7, 5, 9, 12, 17]);
    var shift = Select.kr((stepIndex / 8).floor % 4, [0, 0, -5, 2]);
    var note = (root + seq + shift).clip(24, 96).midicps;
    var hit = Decay2.kr(trig, 0.003, 0.15);
    var open = EnvGen.kr(Env.asr(fade.max(0.002), 1, fade.max(0.01)), gate, doneAction: 2);
    var sig = Pulse.ar(note * [1, 1.005], 0.42, 0.095) + SinOsc.ar(note * 2, 0, 0.045);
    sig = RLPF.ar(sig.tanh, (850 + hit * 3600).clip(120, 9000), 0.34) * hit;
    sig = LeakDC.ar(HPF.ar(sig, 45)) * open;

    Limiter.ar(Splay.ar(sig, 0.42) * vol, 0.72);
}.play;
)
