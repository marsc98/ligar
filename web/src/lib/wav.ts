const SAMPLE_RATE     = 16000
const BITS_PER_SAMPLE = 16
const CHANNELS        = 1

export function buildWavHeader(numSamples: number): ArrayBuffer {
  const dataBytes = numSamples * CHANNELS * (BITS_PER_SAMPLE / 8)
  const buf  = new ArrayBuffer(44)
  const view = new DataView(buf)
  const enc  = new TextEncoder()
  const w4   = (off: number, s: string) =>
    enc.encode(s).forEach((b, i) => view.setUint8(off + i, b))

  w4(0,  'RIFF')
  view.setUint32(4,  36 + dataBytes, true)
  w4(8,  'WAVE')
  w4(12, 'fmt ')
  view.setUint32(16, 16, true)
  view.setUint16(20, 1,  true)
  view.setUint16(22, CHANNELS, true)
  view.setUint32(24, SAMPLE_RATE, true)
  view.setUint32(28, SAMPLE_RATE * CHANNELS * (BITS_PER_SAMPLE / 8), true)
  view.setUint16(32, CHANNELS * (BITS_PER_SAMPLE / 8), true)
  view.setUint16(34, BITS_PER_SAMPLE, true)
  w4(36, 'data')
  view.setUint32(40, dataBytes, true)
  return buf
}

export function assemblePcmToWav(chunks: ArrayBuffer[], numSamples: number): Blob {
  return new Blob([buildWavHeader(numSamples), ...chunks], { type: 'audio/wav' })
}
