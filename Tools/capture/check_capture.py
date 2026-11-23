# Sanity check: run the test file through a known nonlinearity and pull the harmonic responses
# back out of the sweeps (Farina's method), using only the sync markers to find them.
import json, math, numpy as np, wave
FS = 48000
m = json.load(open('cues.json'))
w = wave.open('UVP Tape Capture 48k.wav'); raw = w.readframes(w.getnframes())
b = np.frombuffer(raw, np.uint8).reshape(-1, 3)
x = (b[:, 0].astype(np.int32) | (b[:, 1].astype(np.int32) << 8) | (b[:, 2].astype(np.int8).astype(np.int32) << 16)) / 8388608.0
# "machine": 0.37 s of latency, soft saturation, gentle top-end loss
d = int(0.37 * FS)
y = np.concatenate([np.zeros(d), np.tanh(2.2 * x) / 2.2])
k = np.exp(-np.arange(64) / 3.0); k /= k.sum(); y = np.convolve(y, k)[:len(y)]
# find the first sync burst by correlating with the marker chirp pattern
mk = [c for c in m['cues'] if c['kind'] == 'sync'][0]
s0 = int(mk['start_s'] * FS); ref = x[s0:s0 + int(1.3 * FS)]
corr = np.abs(np.fft.irfft(np.fft.rfft(y[:FS * 60], 2 ** 22) * np.conj(np.fft.rfft(ref, 2 ** 22)), 2 ** 22))
lag = int(np.argmax(corr)) - s0
print('detected latency %.4f s (true 0.3700)' % (lag / FS))
f1, f2 = 10.0, 22000.0
L = m['sweep']['L']; T = m['sweep']['length_s']; n = int(round(T * FS)); t = np.arange(n) / FS
for c in [c for c in m['cues'] if c['kind'] == 'sweep'][::3]:
    i0 = int(c['start_s'] * FS) + lag
    seg = y[i0 - 3 * FS: i0 + n + 2 * FS]
    N = 2 ** int(math.ceil(math.log2(len(seg) + n)))
    h = np.fft.irfft(np.fft.rfft(seg, N) * np.fft.rfft(inv, N), N)
    lin = int(np.argmax(np.abs(h)))
    e = lambda pos: np.sum(h[max(0, pos - 400):pos + 400] ** 2)
    e1 = e(lin)
    harm = []
    for kk in (2, 3, 5):
        dt = int(round(L * math.log(kk) * FS))
        harm.append(10 * math.log10(e(lin - dt) / e1 + 1e-30))
    print('%-7s  H2 %6.1f dB  H3 %6.1f dB  H5 %6.1f dB' % (c['level'], *harm))
