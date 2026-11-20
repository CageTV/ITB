"""ITB Birds audio: Birds/audio_src/*.mp3 -> game-ready mono 16-bit WAV in Birds/assets/sound/fx/itb/birds/ (Skyrim plays WAV from Data/sound/fx).
Trims silence, fades in and out (no clicks), normalises each clip to the same peak and prints what each clip is like."""
import os

import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, 'audio_src')
DST = os.path.join(HERE, 'assets', 'sound', 'fx', 'itb', 'birds')
RATE = 44100
PEAK_DB = -3.0           # every clip peaks here; the volume in the game comes from the sound descriptor
SILENCE_DB = -48.0       # below this at the start / end is cut
PAD = 0.04               # seconds of room kept around the sound


def resample(x, sr, to):
    if sr == to:
        return x
    n = int(round(len(x) * to / sr))
    return np.interp(np.linspace(0, len(x) - 1, n), np.arange(len(x)), x).astype(np.float32)


def process(path):
    x, sr = sf.read(path, always_2d=True, dtype='float32')
    x = x.mean(axis=1)                           # mono: Skyrim positions mono sounds in 3D
    x = resample(x, sr, RATE)
    env = np.abs(x)
    thr = 10 ** (SILENCE_DB / 20)
    idx = np.where(env > thr)[0]
    if len(idx) == 0:
        return None, 0, 0
    a = max(0, idx[0] - int(PAD * RATE))
    b = min(len(x), idx[-1] + int(PAD * RATE))
    x = x[a:b].copy()
    fi, fo = int(0.01 * RATE), int(0.06 * RATE)
    x[:fi] *= np.linspace(0, 1, fi)
    x[-fo:] *= np.linspace(1, 0, fo)
    peak = np.abs(x).max()
    x *= (10 ** (PEAK_DB / 20)) / max(peak, 1e-6)
    return x, sr, peak


def describe(x):
    rms = 20 * np.log10(np.sqrt((x ** 2).mean()) + 1e-9)
    spec = np.abs(np.fft.rfft(x * np.hanning(len(x))))
    freqs = np.fft.rfftfreq(len(x), 1 / RATE)
    centroid = (spec * freqs).sum() / max(spec.sum(), 1e-9)
    # how much of the clip is "active" (above -35 dB of its peak in 50 ms blocks)
    blk = int(0.05 * RATE)
    n = len(x) // blk
    act = np.mean([np.abs(x[i * blk:(i + 1) * blk]).max() > 10 ** (-35 / 20) * np.abs(x).max() for i in range(n)]) if n else 1.0
    return rms, centroid, act


if __name__ == '__main__':
    os.makedirs(DST, exist_ok=True)
    print('%-12s %6s %6s %8s %9s %8s' % ('clip', 'src_hz', 'secs', 'rms_dB', 'centroid', 'active'))
    for f in sorted(os.listdir(SRC)):
        if not f.endswith('.mp3'):
            continue
        name = f[:-4]
        x, sr, peak = process(os.path.join(SRC, f))
        if x is None:
            print('%-12s SILENT, skipped' % name)
            continue
        sf.write(os.path.join(DST, name + '.wav'), x, RATE, subtype='PCM_16')
        rms, cen, act = describe(x)
        print('%-12s %6d %6.2f %8.1f %8.0fHz %7.0f%%' % (name, sr, len(x) / RATE, rms, cen, act * 100))
