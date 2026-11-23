#!/usr/bin/env python3
"""Tape capture test file for Unique Vintage Processor (MORE COLOUR sampling).

48 kHz, 24-bit, mono. 0 VU = 1 kHz sine at -18 dBFS RMS (-15 dBFS peak).
Sections: silence, 1 kHz calibration tone, sync markers, 15 exponential sweeps
(-30..+12 VU, 3 dB steps, 10 Hz-22 kHz, 12 s each), pink noise at -20/-10/0/+3 VU,
3150 Hz flutter tone, sync markers, silence. Writes the WAV and a JSON cue list.
"""
import json, math, struct, sys
import numpy as np

FS = 48000
REF_PEAK = 10 ** (-18 / 20) * math.sqrt(2.0)   # 0 VU sine peak
REF_RMS = 10 ** (-18 / 20)                      # 0 VU sine RMS


def vu_peak(vu):
    return REF_PEAK * 10 ** (vu / 20)


def silence(sec):
    return np.zeros(int(round(sec * FS)))


def fade(x, fin, fout):
    n_in, n_out = int(fin * FS), int(fout * FS)
    if n_in:
        x[:n_in] *= 0.5 - 0.5 * np.cos(np.linspace(0, np.pi, n_in))
    if n_out:
        x[-n_out:] *= 0.5 + 0.5 * np.cos(np.linspace(0, np.pi, n_out))
    return x


def tone(freq, sec, vu, fio=0.02):
    t = np.arange(int(round(sec * FS))) / FS
    return fade(vu_peak(vu) * np.sin(2 * np.pi * freq * t), fio, fio)


def sync_sweep_params(f1, f2, approx_sec):
    """Synchronized swept sine (Novak et al.): f1*L is a whole number, so every harmonic's response
    is an exact time-shifted copy with the right phase - needed to build harmonic kernels."""
    L = round(f1 * approx_sec / math.log(f2 / f1)) / f1
    return L, L * math.log(f2 / f1)


def sweep(f1, f2, approx_sec, vu):
    L, sec = sync_sweep_params(f1, f2, approx_sec)
    t = np.arange(int(round(sec * FS))) / FS
    x = vu_peak(vu) * np.sin(2 * np.pi * f1 * L * np.exp(t / L))
    return fade(x, 0.10, 0.005)


def marker_burst(vu=-6.0):
    """Three short log chirps (200 Hz-12 kHz, 20 ms) at 0, 0.5 and 1.25 s: an unmistakable pattern."""
    n = int(0.020 * FS)
    t = np.arange(n) / FS
    L = 0.020 / math.log(12000 / 200)
    c = np.sin(2 * np.pi * 200 * L * (np.exp(t / L) - 1.0)) * np.hanning(n) * vu_peak(vu)
    out = silence(1.25 + 0.020)
    for start in (0.0, 0.5, 1.25):
        i = int(start * FS)
        out[i:i + n] += c
    return out


def pink(sec, vu_rms, seed):
    rng = np.random.default_rng(seed)
    n = int(round(sec * FS))
    spec = np.fft.rfft(rng.standard_normal(n))
    f = np.fft.rfftfreq(n, 1 / FS)
    shape = np.zeros_like(f)
    band = (f >= 20) & (f <= 20000)
    shape[band] = 1 / np.sqrt(f[band])
    x = np.fft.irfft(spec * shape, n)
    r = REF_RMS * 10 ** (vu_rms / 20)
    x *= r / np.sqrt(np.mean(x ** 2))
    x = 3.5 * r * np.tanh(x / (3.5 * r))           # keep the rare noise peaks inside 11 dB of the RMS
    x *= r / np.sqrt(np.mean(x ** 2))
    return fade(x, 0.05, 0.05)


def write_wav24(path, x):
    pcm = np.clip(np.round(x * 8388607.0), -8388608, 8388607).astype('<i4')
    raw = pcm.view(np.uint8).reshape(-1, 4)[:, :3].tobytes()
    with open(path, 'wb') as fh:
        fh.write(b'RIFF' + struct.pack('<I', 36 + len(raw)) + b'WAVE')
        fh.write(b'fmt ' + struct.pack('<IHHIIHH', 16, 1, 1, FS, FS * 3, 3, 24))
        fh.write(b'data' + struct.pack('<I', len(raw)) + raw)


def main(out_wav, out_json):
    parts, cues = [], []
    pos = 0.0

    def add(x, label=None, level=None, kind=None):
        nonlocal pos
        if label:
            cues.append({'start_s': round(pos, 3), 'length_s': round(len(x) / FS, 3), 'section': label, 'level': level, 'kind': kind})
        parts.append(x)
        pos += len(x) / FS

    add(silence(2.0), 'Silence (noise floor)', None, 'silence')
    add(tone(1000, 30.0, 0.0), '1 kHz calibration tone', '0 VU (-18 dBFS RMS)', 'cal')
    add(silence(2.0))
    add(marker_burst(), 'Sync markers (3 chirps)', '-6 VU', 'sync')
    add(silence(2.0))
    levels = list(range(-30, 13, 3))
    for vu in levels:
        add(sweep(10.0, 22000.0, 12.0, vu), f'Sweep 10 Hz-22 kHz', f'{vu:+d} VU', 'sweep')
        add(silence(3.0))
    for i, vu in enumerate((-20, -10, 0, 3)):
        add(pink(8.0, vu, 100 + i), 'Pink noise', f'{vu:+d} VU (RMS)', 'noise')
        add(silence(2.0))
    add(tone(3150, 20.0, -10.0), '3150 Hz flutter tone', '-10 VU', 'flutter')
    add(silence(2.0))
    add(marker_burst(), 'Sync markers (3 chirps)', '-6 VU', 'sync')
    add(silence(3.0), 'Silence (end)', None, 'silence')

    x = np.concatenate(parts)
    peak = float(np.max(np.abs(x)))
    assert peak < 0.99, peak
    write_wav24(out_wav, x)
    L, sec = sync_sweep_params(10.0, 22000.0, 12.0)
    meta = {'sample_rate': FS, 'bits': 24, 'channels': 1, 'duration_s': round(len(x) / FS, 3),
            'peak_dbfs': round(20 * math.log10(peak), 2), 'zero_vu': '1 kHz sine at -18 dBFS RMS',
            'sweep': {'type': 'synchronized exponential', 'f1': 10.0, 'f2': 22000.0, 'L': L, 'length_s': round(sec, 4),
                      'levels_vu': levels, 'gap_s': 3.0},
            'cues': cues}
    with open(out_json, 'w') as fh:
        json.dump(meta, fh, indent=1)
    print(json.dumps({k: meta[k] for k in ('duration_s', 'peak_dbfs')}), len(cues), 'cues')


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
