export function computeMagnitudes(samples: Float32Array, fftSize?: number): Float32Array {
  const maxSize = 2048

  let size = 1
  const defaultLimit = Math.min(samples.length, maxSize)
  while (size * 2 <= defaultLimit) size *= 2

  if (fftSize !== undefined) {
    let clamped = 1
    const clampLimit = Math.min(fftSize, samples.length, maxSize)
    while (clamped * 2 <= clampLimit) clamped *= 2
    size = clamped
  }

  const re = new Float32Array(size)
  const im = new Float32Array(size)

  for (let i = 0; i < size; i++) {
    const w = 0.5 * (1 - Math.cos((2 * Math.PI * i) / (size - 1)))
    re[i] = (i < samples.length ? samples[i] : 0) * w
  }

  const bits = Math.log2(size)
  for (let i = 0; i < size; i++) {
    let rev = 0
    let n = i
    for (let b = 0; b < bits; b++) {
      rev = (rev << 1) | (n & 1)
      n >>= 1
    }
    if (rev > i) {
      let tmp = re[i]; re[i] = re[rev]; re[rev] = tmp
      tmp = im[i]; im[i] = im[rev]; im[rev] = tmp
    }
  }

  for (let len = 2; len <= size; len *= 2) {
    const half = len >>> 1
    const ang = (-2 * Math.PI) / len
    const wRe = Math.cos(ang)
    const wIm = Math.sin(ang)
    for (let i = 0; i < size; i += len) {
      let tRe = 1, tIm = 0
      for (let j = 0; j < half; j++) {
        const uRe = re[i + j]
        const uIm = im[i + j]
        const vRe = re[i + j + half] * tRe - im[i + j + half] * tIm
        const vIm = re[i + j + half] * tIm + im[i + j + half] * tRe
        re[i + j] = uRe + vRe
        im[i + j] = uIm + vIm
        re[i + j + half] = uRe - vRe
        im[i + j + half] = uIm - vIm
        const nTRe = tRe * wRe - tIm * wIm
        tIm = tRe * wIm + tIm * wRe
        tRe = nTRe
      }
    }
  }

  const half = size >>> 1
  const mags = new Float32Array(half)
  let max = 0
  for (let i = 0; i < half; i++) {
    mags[i] = Math.sqrt(re[i] * re[i] + im[i] * im[i])
    if (mags[i] > max) max = mags[i]
  }
  if (max > 0) {
    for (let i = 0; i < half; i++) mags[i] /= max
  }

  return mags
}
