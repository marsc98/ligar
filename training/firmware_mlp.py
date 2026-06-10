#!/usr/bin/env python3
"""
Valida paridade Python ↔ C para o forward pass MLP do firmware.
Parseia weights.h, replica exatamente a inferência de mlp.c, imprime probabilidades.

Uso: python firmware_mlp.py <wav_path>
"""
import sys
import re
import numpy as np
from pathlib import Path


def _parse_weights_h(weights_h: Path) -> dict:
    text = weights_h.read_text(encoding='utf-8')
    result = {}

    # Classes
    m = re.search(r'MLP_CLASS_NAMES\[.*?\]\s*=\s*\{(.*?)\};', text, re.DOTALL)
    if not m:
        raise ValueError('MLP_CLASS_NAMES não encontrado em weights.h')
    result['classes'] = re.findall(r'"(\w+)"', m.group(1))

    # Defines
    for key in ('MLP_N_CLASSES', 'MLP_N_INPUT', 'MLP_N_H1', 'MLP_N_H2'):
        m = re.search(rf'#define\s+{key}\s+(\d+)', text)
        if not m:
            raise ValueError(f'{key} não encontrado em weights.h')
        result[key] = int(m.group(1))

    # Escalas float
    for key in ('MLP_SCALE_INPUT', 'MLP_SCALE_W1', 'MLP_SCALE_B1', 'MLP_SCALE_W2',
                'MLP_SCALE_B2', 'MLP_SCALE_WOUT', 'MLP_SCALE_BOUT'):
        m = re.search(rf'{key}\s*=\s*([-+0-9.e]+)f;', text)
        if not m:
            raise ValueError(f'{key} não encontrado em weights.h')
        result[key] = float(m.group(1))

    def parse_array(name):
        m = re.search(rf'{name}\[.*?\]\s*=\s*\{{(.*?)\}};', text, re.DOTALL)
        if not m:
            raise ValueError(f'{name} não encontrado em weights.h')
        return np.array(list(map(int, re.findall(r'-?\d+', m.group(1)))), dtype=np.int8)

    n_in  = result['MLP_N_INPUT']
    h1    = result['MLP_N_H1']
    h2    = result['MLP_N_H2']
    n_cls = result['MLP_N_CLASSES']

    result['W1']   = parse_array('MLP_W1').reshape(h1, n_in)
    result['B1']   = parse_array('MLP_B1')
    result['W2']   = parse_array('MLP_W2').reshape(h2, h1)
    result['B2']   = parse_array('MLP_B2')
    result['WOUT'] = parse_array('MLP_WOUT').reshape(n_cls, h2)
    result['BOUT'] = parse_array('MLP_BOUT')
    return result


def mlp_infer_python(mfcc: np.ndarray, w: dict) -> np.ndarray:
    """Replica exatamente mlp.c: quantização int8 → matmul → dequant → softmax."""
    s_input = w['MLP_SCALE_INPUT']
    s_W1 = w['MLP_SCALE_W1']
    s_W2 = w['MLP_SCALE_W2']
    s_Wo = w['MLP_SCALE_WOUT']

    x_q = np.clip(np.round(mfcc / s_input), -127, 127).astype(np.int8)

    acc1 = w['W1'].astype(np.int32) @ x_q.astype(np.int32)
    h1   = acc1.astype(np.float32) * (s_W1 * s_input) + w['B1'].astype(np.float32) * w['MLP_SCALE_B1']
    h1   = np.maximum(0.0, h1)

    h1_q = np.clip(np.round(h1 / s_W2), -127, 127).astype(np.int8)

    acc2 = w['W2'].astype(np.int32) @ h1_q.astype(np.int32)
    h2   = acc2.astype(np.float32) * (s_W2 * s_W2) + w['B2'].astype(np.float32) * w['MLP_SCALE_B2']
    h2   = np.maximum(0.0, h2)

    h2_q = np.clip(np.round(h2 / s_Wo), -127, 127).astype(np.int8)

    acco   = w['WOUT'].astype(np.int32) @ h2_q.astype(np.int32)
    logits = acco.astype(np.float32) * (s_Wo * s_Wo) + w['BOUT'].astype(np.float32) * w['MLP_SCALE_BOUT']

    logits -= logits.max()
    e = np.exp(logits)
    return (e / e.sum()).astype(np.float32)


def extract_mfcc(wav_path: Path) -> np.ndarray:
    try:
        from firmware_mfcc import extract as fw_mfcc, N_SAMPLES, N_FRAMES
        import librosa
    except ImportError as e:
        print(f'Dependência ausente: {e}', file=sys.stderr)
        sys.exit(1)

    SAMPLE_RATE = 16000
    y_wav, _ = librosa.load(str(wav_path), sr=SAMPLE_RATE, mono=True)
    y_int16  = (y_wav * 32767).clip(-32768, 32767).astype(np.int16)

    abs_y = np.abs(y_int16).astype(np.float32)
    peak  = abs_y.max()
    if peak == 0:
        offset = len(y_int16)
    else:
        voiced = np.where(abs_y > peak * 0.05)[0]
        offset = int(voiced[-1]) if len(voiced) > 0 else len(y_int16)

    end    = min(len(y_int16), offset + SAMPLE_RATE // 20)
    start  = max(0, end - N_SAMPLES)
    y_int16 = y_int16[start:end]
    if len(y_int16) < N_SAMPLES:
        y_int16 = np.pad(y_int16, (N_SAMPLES - len(y_int16), 0))

    return fw_mfcc(y_int16, N_FRAMES).flatten().astype(np.float32)


def main():
    if len(sys.argv) != 2:
        print('Uso: python firmware_mlp.py <wav_path>', file=sys.stderr)
        sys.exit(1)

    wav_path = Path(sys.argv[1])
    if not wav_path.exists():
        print(f'Arquivo não encontrado: {wav_path}', file=sys.stderr)
        sys.exit(1)

    root      = Path(__file__).parent
    weights_h = root.parent / 'firmware' / 'main' / 'kws' / 'weights.h'
    if not weights_h.exists():
        print(f'weights.h não encontrado: {weights_h}', file=sys.stderr)
        print('Execute train_mlp.py primeiro.', file=sys.stderr)
        sys.exit(1)

    mfcc    = extract_mfcc(wav_path)
    w       = _parse_weights_h(weights_h)
    probs   = mlp_infer_python(mfcc, w)
    classes = w['classes']

    print(f'\nProbabilidades — {wav_path.name}')
    for cls, p in zip(classes, probs):
        print(f'  {cls:<12s}: {p:.4f}')
    best = int(probs.argmax())
    print(f'\nPrevisão: {classes[best]}  (p={probs[best]:.4f})')


if __name__ == '__main__':
    main()
