"""
MFCC idêntico ao firmware (mfcc.c).
Pre-emphasis 0.97, Hann, FFT 512, 26 mels 300-8000 Hz, 13 coefs, normalização /32768.
"""
import numpy as np

SAMPLE_RATE = 16000
DURATION_S  = 0.5
N_SAMPLES   = int(SAMPLE_RATE * DURATION_S)   # 8000
FRAME_LEN   = 400
HOP         = 160
N_FFT       = 512
N_MELS      = 26
N_COEFS     = 13
N_FRAMES    = (N_SAMPLES - FRAME_LEN) // HOP + 1  # 48
FMIN        = 300.0
FMAX        = 8000.0


def _hz_to_mel(hz):
    return 2595.0 * np.log10(1.0 + hz / 700.0)


def _mel_to_hz(mel):
    return 700.0 * (10.0 ** (mel / 2595.0) - 1.0)


def _build_mel_fb():
    n_bins  = N_FFT // 2 + 1
    mel_min = _hz_to_mel(FMIN)
    mel_max = _hz_to_mel(FMAX)
    pts     = _mel_to_hz(np.linspace(mel_min, mel_max, N_MELS + 2))
    bin_pts = pts / (SAMPLE_RATE / 2.0) * (n_bins - 1)

    fb = np.zeros((N_MELS, n_bins), dtype=np.float32)
    for m in range(N_MELS):
        left, center, right = bin_pts[m], bin_pts[m + 1], bin_pts[m + 2]
        for k in range(n_bins):
            if left < center and left <= k <= center:
                fb[m, k] = (k - left) / (center - left)
            elif center < right and center < k <= right:
                fb[m, k] = (right - k) / (right - center)
    return fb


def _build_dct():
    dct = np.zeros((N_COEFS, N_MELS), dtype=np.float32)
    for c in range(N_COEFS):
        norm = np.sqrt(1.0 / N_MELS) if c == 0 else np.sqrt(2.0 / N_MELS)
        for m in range(N_MELS):
            dct[c, m] = norm * np.cos(np.pi * c * (m + 0.5) / N_MELS)
    return dct


_hann   = (0.5 * (1.0 - np.cos(2.0 * np.pi * np.arange(FRAME_LEN) / (FRAME_LEN - 1)))).astype(np.float32)
_mel_fb = _build_mel_fb()
_dct    = _build_dct()


def extract(samples: np.ndarray, n_frames: int) -> np.ndarray:
    """
    samples: int16 ou float já normalizado (será convertido para float /32768).
    n_frames: número de frames esperado — deve ser (len(samples) - FRAME_LEN) // HOP + 1.
    Retorna ndarray shape (n_frames, N_COEFS) float32.
    """
    if samples.dtype != np.float32:
        s = samples.astype(np.float32)
    else:
        s = samples.copy()

    # normaliza igual ao firmware (int16 / 32768)
    if np.abs(s).max() > 1.0:
        s = s / 32768.0

    out = np.zeros((n_frames, N_COEFS), dtype=np.float32)

    for frame in range(n_frames):
        off = frame * HOP

        # pre-emphasis por frame (igual ao firmware)
        windowed = np.empty(FRAME_LEN, dtype=np.float32)
        windowed[0] = s[off] * _hann[0]
        for i in range(1, FRAME_LEN):
            windowed[i] = (s[off + i] - 0.97 * s[off + i - 1]) * _hann[i]

        # FFT real → power spectrum
        re = np.fft.rfft(windowed, n=N_FFT)
        power = (re.real ** 2 + re.imag ** 2).astype(np.float32)

        # mel filterbank
        mel_e = np.log(_mel_fb @ power + 1e-9)

        # DCT
        out[frame] = _dct @ mel_e

    mean = out.mean(axis=0)
    std  = out.std(axis=0) + 1e-8
    out  = (out - mean) / std
    return out
