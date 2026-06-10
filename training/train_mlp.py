#!/usr/bin/env python3
"""
Treina MLP 624->128->64->N sobre MFCCs de features/*.npy.
Aplica data augmentation e exporta firmware/main/kws/weights.h com pesos int8.

Uso: python train_mlp.py
"""
import sys
import numpy as np
from pathlib import Path
from collections import defaultdict

LR        = 1e-3
EPOCHS    = 300
H1, H2    = 128, 64
SEED      = 42
TEST_FRAC = 0.20


# ── Funções de ativação ──────────────────────────────────────────────────────

def relu(x: np.ndarray) -> np.ndarray:
    return np.maximum(0.0, x)


def softmax(x: np.ndarray) -> np.ndarray:
    x = x - x.max(axis=1, keepdims=True)
    e = np.exp(x)
    return e / e.sum(axis=1, keepdims=True)


def cross_entropy(probs: np.ndarray, y: np.ndarray) -> float:
    n = len(y)
    return float(-np.log(probs[np.arange(n), y] + 1e-9).mean())


# ── MLP com Adam ─────────────────────────────────────────────────────────────

class MLP:
    def __init__(self, n_in: int, h1: int, h2: int, n_out: int, seed: int = 42):
        rng = np.random.default_rng(seed)
        he  = lambda m: np.sqrt(2.0 / m)
        self.W1 = (rng.standard_normal((h1, n_in)) * he(n_in)).astype(np.float32)
        self.b1 = np.zeros(h1, dtype=np.float32)
        self.W2 = (rng.standard_normal((h2, h1)) * he(h1)).astype(np.float32)
        self.b2 = np.zeros(h2, dtype=np.float32)
        self.Wo = (rng.standard_normal((n_out, h2)) * he(h2)).astype(np.float32)
        self.bo = np.zeros(n_out, dtype=np.float32)
        self._params = [self.W1, self.b1, self.W2, self.b2, self.Wo, self.bo]
        self._m = [np.zeros_like(p) for p in self._params]
        self._v = [np.zeros_like(p) for p in self._params]
        self._t = 0

    def forward(self, X: np.ndarray) -> np.ndarray:
        self._X   = X
        self._z1  = X @ self.W1.T + self.b1
        self._a1  = relu(self._z1)
        self._z2  = self._a1 @ self.W2.T + self.b2
        self._a2  = relu(self._z2)
        self._zo  = self._a2 @ self.Wo.T + self.bo
        self._out = softmax(self._zo)
        return self._out

    def backward(self, y: np.ndarray, lr: float, weight_decay: float = 5e-4,
                 beta1: float = 0.9, beta2: float = 0.999, eps: float = 1e-8):
        n = len(y)
        self._t += 1

        dzo = self._out.copy()
        dzo[np.arange(n), y] -= 1.0
        dzo /= n

        dWo = dzo.T @ self._a2
        dbo = dzo.sum(axis=0)
        da2 = dzo @ self.Wo

        dz2 = da2 * (self._z2 > 0)
        dW2 = dz2.T @ self._a1
        db2 = dz2.sum(axis=0)
        da1 = dz2 @ self.W2

        dz1 = da1 * (self._z1 > 0)
        dW1 = dz1.T @ self._X
        db1 = dz1.sum(axis=0)

        grads = [dW1, db1, dW2, db2, dWo, dbo]
        bc1 = 1.0 - beta1 ** self._t
        bc2 = 1.0 - beta2 ** self._t
        for i, (p, g) in enumerate(zip(self._params, grads)):
            self._m[i] = beta1 * self._m[i] + (1.0 - beta1) * g
            self._v[i] = beta2 * self._v[i] + (1.0 - beta2) * (g * g)
            p -= lr * (self._m[i] / bc1) / (np.sqrt(self._v[i] / bc2) + eps)
            if p.ndim > 1:  # AdamW: weight decay só em matrizes, não em biases
                p -= lr * weight_decay * p

    def predict(self, X: np.ndarray) -> np.ndarray:
        z1 = relu(X @ self.W1.T + self.b1)
        z2 = relu(z1 @ self.W2.T + self.b2)
        zo = z2 @ self.Wo.T + self.bo
        return softmax(zo)


# ── Data loading ─────────────────────────────────────────────────────────────

def load_features(features_dir: Path):
    files = sorted(features_dir.glob('*.npy'))
    if not files:
        print(f'ERRO: nenhum .npy em {features_dir}', file=sys.stderr)
        sys.exit(1)
    classes = sorted(p.stem for p in files)
    X, y = [], []
    for idx, cls in enumerate(classes):
        data = np.load(str(features_dir / f'{cls}.npy'))  # (N, N_FRAMES, N_COEFS)
        for sample in data:
            X.append(sample.flatten().astype(np.float32))
            y.append(idx)
    return np.array(X, dtype=np.float32), np.array(y, dtype=np.int32), classes


def stratified_split(X, y, test_frac: float, seed: int):
    rng = np.random.default_rng(seed)
    by_class = defaultdict(list)
    for i, lbl in enumerate(y):
        by_class[int(lbl)].append(i)
    train_idx, test_idx = [], []
    for lbl in sorted(by_class):
        idxs = rng.permutation(by_class[lbl]).tolist()
        n_test = max(1, round(len(idxs) * test_frac))
        test_idx.extend(idxs[:n_test])
        train_idx.extend(idxs[n_test:])
    return (X[train_idx], y[train_idx]), (X[test_idx], y[test_idx])


def augment(X: np.ndarray, y: np.ndarray, seed: int):
    rng = np.random.default_rng(seed)
    # Ruído gaussiano SNR ~15dB
    sig_power = np.mean(X ** 2, axis=1, keepdims=True).clip(1e-9)
    std       = np.sqrt(sig_power / (10 ** (15.0 / 10))).astype(np.float32)
    noise     = (rng.standard_normal(X.shape) * std).astype(np.float32)
    X_noise   = X + noise
    # Variação de volume ±6dB
    db    = rng.uniform(-6.0, 6.0, size=(len(X), 1)).astype(np.float32)
    gain  = (10 ** (db / 20.0)).astype(np.float32)
    X_vol = X * gain
    return (
        np.vstack([X, X_noise, X_vol]).astype(np.float32),
        np.tile(y, 3).astype(np.int32),
    )


# ── Quantização int8 ─────────────────────────────────────────────────────────

def quantize(w: np.ndarray):
    scale = float(np.max(np.abs(w))) / 127.0
    if scale < 1e-9:
        scale = 1e-9
    w_q = np.round(w / scale).clip(-127, 127).astype(np.int8)
    return w_q, scale


def forward_quantized(X: np.ndarray, s_input: float,
                      W1_q, s_W1, B1_q, s_B1,
                      W2_q, s_W2, B2_q, s_B2,
                      Wo_q, s_Wo, Bo_q, s_Bo,
                      s_act1: float, s_act2: float) -> np.ndarray:
    """Replica exata do mlp.c: mesmas operações de quantização/dequant."""
    x_q = np.clip(np.round(X / s_input), -127, 127).astype(np.int8)
    acc1 = W1_q.astype(np.int32) @ x_q.T.astype(np.int32)
    h1   = acc1.T.astype(np.float32) * (s_W1 * s_input) + B1_q.astype(np.float32) * s_B1
    h1   = np.maximum(0.0, h1)
    h1_q = np.clip(np.round(h1 / s_act1), -127, 127).astype(np.int8)
    acc2 = W2_q.astype(np.int32) @ h1_q.T.astype(np.int32)
    h2   = acc2.T.astype(np.float32) * (s_W2 * s_act1) + B2_q.astype(np.float32) * s_B2
    h2   = np.maximum(0.0, h2)
    h2_q = np.clip(np.round(h2 / s_act2), -127, 127).astype(np.int8)
    acco   = Wo_q.astype(np.int32) @ h2_q.T.astype(np.int32)
    logits = acco.T.astype(np.float32) * (s_Wo * s_act2) + Bo_q.astype(np.float32) * s_Bo
    logits -= logits.max(axis=1, keepdims=True)
    e = np.exp(logits)
    return e / e.sum(axis=1, keepdims=True)


# ── Exportação weights.h ─────────────────────────────────────────────────────

def _array1d_c(name: str, arr: np.ndarray) -> str:
    vals = ','.join(str(int(v)) for v in arr)
    return f'static const int8_t {name}[{len(arr)}] = {{{vals}}};\n'


def _array2d_c(name: str, arr: np.ndarray) -> str:
    rows = [','.join(str(int(v)) for v in row) for row in arr]
    body = ',\n    '.join('{' + r + '}' for r in rows)
    return (f'static const int8_t {name}[{arr.shape[0]}][{arr.shape[1]}] = {{\n'
            f'    {body}\n}};\n')


def export_weights(path: Path, classes: list, s_input: float,
                   W1_q, s_W1, B1_q, s_B1,
                   W2_q, s_W2, B2_q, s_B2,
                   Wo_q, s_Wo, Bo_q, s_Bo,
                   s_act1: float, s_act2: float):
    n_cls = len(classes)
    h1, n_in = W1_q.shape
    h2       = W2_q.shape[0]

    lines = [
        '/* AUTO-GERADO por train_mlp.py — NÃO EDITAR MANUALMENTE */\n',
        '#pragma once\n',
        '#include <stdint.h>\n',
        '\n',
        f'#define MLP_N_CLASSES {n_cls}\n',
        f'#define MLP_N_INPUT   {n_in}\n',
        f'#define MLP_N_H1      {h1}\n',
        f'#define MLP_N_H2      {h2}\n',
        '\n',
        'static const char * const MLP_CLASS_NAMES[MLP_N_CLASSES] = {\n',
    ]
    for cls in classes:
        lines.append(f'    "{cls}",\n')
    lines += [
        '};\n\n',
        f'static const float MLP_SCALE_INPUT = {s_input:.10e}f;\n',
        f'static const float MLP_SCALE_W1   = {s_W1:.10e}f;\n',
        f'static const float MLP_SCALE_B1   = {s_B1:.10e}f;\n',
        f'static const float MLP_SCALE_ACT1 = {s_act1:.10e}f;\n',
        f'static const float MLP_SCALE_W2   = {s_W2:.10e}f;\n',
        f'static const float MLP_SCALE_B2   = {s_B2:.10e}f;\n',
        f'static const float MLP_SCALE_ACT2 = {s_act2:.10e}f;\n',
        f'static const float MLP_SCALE_WOUT = {s_Wo:.10e}f;\n',
        f'static const float MLP_SCALE_BOUT = {s_Bo:.10e}f;\n',
        '\n',
        _array2d_c('MLP_W1', W1_q), '\n',
        _array1d_c('MLP_B1', B1_q), '\n',
        _array2d_c('MLP_W2', W2_q), '\n',
        _array1d_c('MLP_B2', B2_q), '\n',
        _array2d_c('MLP_WOUT', Wo_q), '\n',
        _array1d_c('MLP_BOUT', Bo_q),
    ]
    path.write_text(''.join(lines), encoding='utf-8')
    size_kb = path.stat().st_size / 1024
    print(f'\n{path} gerado  ({size_kb:.0f} KB texto)')
    binary_kb = (W1_q.nbytes + B1_q.nbytes + W2_q.nbytes +
                 B2_q.nbytes + Wo_q.nbytes + Bo_q.nbytes) / 1024
    print(f'Tamanho binário dos arrays (Flash): {binary_kb:.1f} KB')


# ── Main ─────────────────────────────────────────────────────────────────────

def main():
    root    = Path(__file__).parent
    fw_kws  = root.parent / 'firmware' / 'main' / 'kws'

    X, y, classes = load_features(root / 'features')
    n_cls = len(classes)
    n_in  = X.shape[1]
    print(f'Classes ({n_cls}): {classes}')
    print(f'Total amostras: {len(X)}  dim: {n_in}')

    (X_train, y_train), (X_test, y_test) = stratified_split(X, y, TEST_FRAC, SEED)
    print(f'Holdout: {len(X_test)} amostras ({100*len(X_test)/len(X):.0f}%)')

    X_aug, y_aug = augment(X_train, y_train, seed=SEED)
    ratio = len(X_aug) / len(X_train)
    print(f'Augmentation: {len(X_train)} → {len(X_aug)} ({ratio:.1f}×)')
    assert ratio >= 3.0, 'Augmentation deve gerar ≥ 3× o original'

    # Shuffle augmented dataset
    rng  = np.random.default_rng(SEED)
    perm = rng.permutation(len(X_aug))
    X_aug, y_aug = X_aug[perm], y_aug[perm]

    mlp = MLP(n_in, H1, H2, n_cls, seed=SEED)
    print(f'\nTreino — {EPOCHS} épocas, LR={LR}, batch completo')
    for ep in range(EPOCHS):
        probs = mlp.forward(X_aug)
        loss  = cross_entropy(probs, y_aug)
        mlp.backward(y_aug, lr=LR)
        if (ep + 1) % 40 == 0:
            acc = float((probs.argmax(axis=1) == y_aug).mean())
            print(f'  época {ep+1:3d}: loss={loss:.4f}  train_acc={acc:.3f}')

    # Avaliação no holdout
    probs_test = mlp.predict(X_test)
    preds_test = probs_test.argmax(axis=1)
    global_acc = float((preds_test == y_test).mean())
    print(f'\n── Acurácia holdout 20% ──────────────────────────')
    print(f'Global: {global_acc:.3f}  ({(preds_test == y_test).sum()}/{len(y_test)})')
    for i, cls in enumerate(classes):
        mask  = y_test == i
        n_i   = int(mask.sum())
        if n_i == 0:
            continue
        acc_i = float((preds_test[mask] == i).mean())
        print(f'  {cls:<12s}: {acc_i:.3f}  ({int(acc_i*n_i)}/{n_i})')

    # Quantização
    s_input    = float(np.max(np.abs(X_train))) / 127.0
    W1_q, s_W1 = quantize(mlp.W1)
    B1_q, s_B1 = quantize(mlp.b1)
    W2_q, s_W2 = quantize(mlp.W2)
    B2_q, s_B2 = quantize(mlp.b2)
    Wo_q, s_Wo = quantize(mlp.Wo)
    Bo_q, s_Bo = quantize(mlp.bo)

    # Escalas de ativação baseadas no range real das ativações no treino
    h1_train = np.maximum(0.0, X_aug @ mlp.W1.T + mlp.b1)
    h2_train = np.maximum(0.0, h1_train @ mlp.W2.T + mlp.b2)
    s_act1 = float(np.max(np.abs(h1_train))) / 127.0
    s_act2 = float(np.max(np.abs(h2_train))) / 127.0

    # Avaliação do modelo quantizado (valida que a quantização não degradou)
    probs_q = forward_quantized(X_test, s_input,
                                W1_q, s_W1, B1_q, s_B1,
                                W2_q, s_W2, B2_q, s_B2,
                                Wo_q, s_Wo, Bo_q, s_Bo,
                                s_act1, s_act2)
    preds_q = probs_q.argmax(axis=1)
    acc_q = float((preds_q == y_test).mean())
    print(f'\n── Acurácia quantizada (int8) ────────────────────')
    print(f'Global: {acc_q:.3f}  ({(preds_q == y_test).sum()}/{len(y_test)})')
    print(f'MLP_SCALE_INPUT = {s_input:.6e}  (range treino: [{-127*s_input:.2f}, {127*s_input:.2f}])')
    print(f'MLP_SCALE_ACT1  = {s_act1:.6e}  (range h1: [0, {127*s_act1:.2f}])')
    print(f'MLP_SCALE_ACT2  = {s_act2:.6e}  (range h2: [0, {127*s_act2:.2f}])')

    export_weights(fw_kws / 'weights.h', classes, s_input,
                   W1_q, s_W1, B1_q, s_B1,
                   W2_q, s_W2, B2_q, s_B2,
                   Wo_q, s_Wo, Bo_q, s_Bo,
                   s_act1, s_act2)


if __name__ == '__main__':
    main()
