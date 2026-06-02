"""
Extrai MFCCs de todos os WAVs em samples/ e salva como .npy.
Usa firmware_mfcc.py — pipeline idêntico ao firmware C (mfcc.c).
Uso: python extract_features.py --word ligar
"""
import argparse
import sys
import numpy as np
import librosa
from pathlib import Path
from firmware_mfcc import extract as fw_mfcc, N_SAMPLES, N_FRAMES, N_COEFS

SAMPLE_RATE = 16000


def extract_mfcc(wav_path: Path) -> np.ndarray:
    y, sr = librosa.load(str(wav_path), sr=SAMPLE_RATE, mono=True)
    y_int16 = (y * 32767).clip(-32768, 32767).astype(np.int16)

    abs_y = np.abs(y_int16).astype(np.float32)
    peak  = abs_y.max()

    if peak == 0:
        offset = len(y_int16)
    else:
        threshold = peak * 0.05
        voiced_mask = abs_y > threshold
        voiced_indices = np.where(voiced_mask)[0]
        offset = int(voiced_indices[-1]) if len(voiced_indices) > 0 else len(y_int16)

    # 50ms de contexto pós-palavra para corresponder ao firmware
    end   = min(len(y_int16), offset + SAMPLE_RATE // 20)
    start = max(0, end - N_SAMPLES)

    y_int16 = y_int16[start:end]
    if len(y_int16) < N_SAMPLES:
        # Pad no INÍCIO (silêncio pré-palavra)
        y_int16 = np.pad(y_int16, (N_SAMPLES - len(y_int16), 0))

    mfcc = fw_mfcc(y_int16, N_FRAMES)
    assert mfcc.shape == (N_FRAMES, N_COEFS), f"Shape inesperado: {mfcc.shape}"
    return mfcc


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--word', required=True, help='Nome da palavra (ex: ligar)')
    args = parser.parse_args()

    samples_dir = Path('samples')
    out_dir = Path('features')
    out_dir.mkdir(exist_ok=True)

    wavs = sorted(samples_dir.glob(f'{args.word}_*.wav'))
    if not wavs:
        print(f'Nenhum WAV encontrado para "{args.word}" em {samples_dir}', file=sys.stderr)
        sys.exit(1)

    features = []
    for wav in wavs:
        try:
            mfcc = extract_mfcc(wav)
            features.append(mfcc)
            print(f'OK  {wav.name}  shape={mfcc.shape}')
        except Exception as e:
            print(f'ERR {wav.name}: {e}')

    if not features:
        print('Nenhuma amostra processada com sucesso.', file=sys.stderr)
        sys.exit(1)

    out = out_dir / f'{args.word}.npy'
    arr = np.stack(features)
    np.save(str(out), arr)
    print(f'\n{len(features)} amostras salvas em {out}')
    print(f'Shape final: {arr.shape}  (amostras, frames, coefs)')


if __name__ == '__main__':
    main()
