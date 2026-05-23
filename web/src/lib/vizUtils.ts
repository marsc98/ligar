import { computeMagnitudes } from './fft'

export function drawWaveform(
  ctx: CanvasRenderingContext2D,
  samples: Float32Array,
  chunkSize: number,
  w: number,
  h: number,
  offsetY: number,
) {
  const mid = offsetY + h / 2

  ctx.strokeStyle = '#94a3b8'
  ctx.lineWidth = 1
  ctx.beginPath()
  for (let x = 0; x < w; x++) {
    const i = Math.floor((x / w) * samples.length)
    const y = mid - samples[i] * (h / 2)
    if (x === 0) ctx.moveTo(x, y)
    else ctx.lineTo(x, y)
  }
  ctx.stroke()

  ctx.strokeStyle = '#334155'
  ctx.lineWidth = 1
  const totalChunks = Math.floor(samples.length / chunkSize)
  for (let c = 1; c < totalChunks; c++) {
    const x = Math.round((c * chunkSize / samples.length) * w)
    ctx.beginPath()
    ctx.moveTo(x, offsetY)
    ctx.lineTo(x, offsetY + h)
    ctx.stroke()
  }
}

export function drawFFT(
  ctx: CanvasRenderingContext2D,
  samples: Float32Array,
  w: number,
  h: number,
  offsetY: number,
) {
  const mags = computeMagnitudes(samples)
  const barW = w / mags.length
  ctx.fillStyle = '#3b82f6'
  for (let i = 0; i < mags.length; i++) {
    const barH = mags[i] * h
    ctx.fillRect(i * barW, offsetY + h - barH, barW, barH)
  }
}
