"""
Gera firmware/main/kws/templates.h a partir dos arquivos .npy em features/.
Uso: python generate_templates.py --words ligar garbage desligar vermelho ...

Classificação automática:
  ligar / desligar / garbage  → KWS_TRIGGERS[]
  demais palavras             → KWS_COLORS[]
"""
import argparse
import numpy as np
from pathlib import Path
from datetime import datetime
from firmware_mfcc import N_FRAMES, N_COEFS

TRIGGER_WORDS   = {"ligar", "desligar", "garbage"}
COLOR_WORDS_ORDER = ["vermelho", "verde", "azul", "amarelo", "ciano",
                     "magenta", "laranja", "roxo", "branco"]
TEMPLATE_COUNT  = 10

def select_templates(features: np.ndarray, n: int) -> np.ndarray:
    if len(features) <= n:
        return features
    dists = []
    for i, f in enumerate(features):
        others = np.delete(features, i, axis=0)
        d = np.mean([np.linalg.norm(f - o) for o in others])
        dists.append(d)
    idx = np.argsort(dists)[:n]
    n_discarded = len(features) - len(idx)
    print(f'  {len(features)} amostras → {len(idx)} templates, {n_discarded} outliers descartados')
    return features[idx]

def array_to_c(arr: np.ndarray, name: str) -> str:
    flat = arr.flatten()
    vals = ', '.join(f'{v:.6f}f' for v in flat)
    return f'static const float {name}[] = {{{vals}}};\n'

def emit_word(lines: list, word: str, features_dir: Path):
    npy = features_dir / f'{word}.npy'
    if not npy.exists():
        print(f'AVISO: {npy} não encontrado — {word} terá 0 templates.')
        lines.append(f'static const float * const kws_{word}_templates[] = {{}};')
        lines.append(f'static const int kws_{word}_n_templates = 0;')
        lines.append('')
        return 0

    features = np.load(str(npy))
    selected = select_templates(features, TEMPLATE_COUNT)
    n = len(selected)

    lines.append(f'/* ── Palavra: "{word}" — {n} templates ── */')
    for i, tmpl in enumerate(selected):
        lines.append(array_to_c(tmpl, f'kws_{word}_tmpl_{i:02d}'))

    lines.append(f'static const float * const kws_{word}_templates[] = {{')
    for i in range(n):
        lines.append(f'    kws_{word}_tmpl_{i:02d},')
    lines.append('};')
    lines.append(f'static const int kws_{word}_n_templates = {n};')
    lines.append('')
    return n

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--words', nargs='+', required=True)
    args = parser.parse_args()

    provided = set(args.words)
    features_dir = Path('features')
    out_path = Path('../firmware/main/kws/templates.h')

    trigger_words = [w for w in ["ligar", "desligar", "garbage"] if w in provided or True]
    # Always include all triggers; emit 0 templates if not provided
    trigger_list  = ["ligar", "desligar", "garbage"]

    # Colors: maintain canonical order, include those provided + all canonical ones
    color_list = COLOR_WORDS_ORDER

    lines = [
        '/* AUTO-GERADO por generate_templates.py — NÃO EDITAR MANUALMENTE */',
        f'/* Gerado em: {datetime.now().isoformat()} */',
        '#pragma once',
        '#include <stdint.h>',
        '',
        f'#define KWS_N_FRAMES  {N_FRAMES}',
        f'#define KWS_N_MFCC   {N_COEFS}',
        '#define KWS_N_COEFS  (KWS_N_FRAMES * KWS_N_MFCC)',
        '',
        'typedef struct {',
        '    const char          *name;',
        '    const float * const *templates;',
        '    int                  n_templates;',
        '} kws_word_t;',
        '',
    ]

    lines.append('/* ════ TRIGGERS ════ */')
    for word in trigger_list:
        emit_word(lines, word, features_dir)

    lines.append(f'#define KWS_N_TRIGGERS {len(trigger_list)}')
    lines.append('static const kws_word_t KWS_TRIGGERS[] = {')
    for word in trigger_list:
        lines.append(f'    {{ "{word}", kws_{word}_templates, kws_{word}_n_templates }},')
    lines.append('};')
    lines.append('')

    lines.append('/* ════ COLORS ════ */')
    for word in color_list:
        emit_word(lines, word, features_dir)

    lines.append(f'#define KWS_N_COLORS {len(color_list)}')
    lines.append('static const kws_word_t KWS_COLORS[] = {')
    for word in color_list:
        lines.append(f'    {{ "{word}", kws_{word}_templates, kws_{word}_n_templates }},')
    lines.append('};')

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text('\n'.join(lines) + '\n')
    print(f'Gerado: {out_path}')
    print(f'Triggers: {trigger_list}')
    print(f'Colors: {color_list}')

if __name__ == '__main__':
    main()
