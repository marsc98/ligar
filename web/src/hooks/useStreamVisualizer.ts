import { useRef, useState } from 'react'
import type { VisualizerMode } from '../types'
import { drawWaveform, drawFFT } from '../lib/vizUtils'

const RING_SIZE = 4096
const CHUNK_SIZE = 512

interface UseStreamVisualizerReturn {
  canvasRef: React.RefObject<HTMLCanvasElement | null>
  mode: VisualizerMode
  setMode: (m: VisualizerMode) => void
  pushChunk: (samples: Int16Array) => void
  start: () => void
  stop: () => void
}

export function useStreamVisualizer(): UseStreamVisualizerReturn {
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const [mode, setModeState] = useState<VisualizerMode>('waveform')
  const modeRef = useRef<VisualizerMode>('waveform')
  const bufferRef = useRef(new Float32Array(RING_SIZE))
  const writePosRef = useRef(0)
  const rafRef = useRef<number>(0)

  const pushChunk = (samples: Int16Array) => {
    const buf = bufferRef.current
    for (let i = 0; i < samples.length; i++) {
      buf[writePosRef.current] = samples[i] / 32768
      writePosRef.current = (writePosRef.current + 1) % RING_SIZE
    }
  }

  const start = () => {
    cancelAnimationFrame(rafRef.current)
    const render = () => {
      const canvas = canvasRef.current
      if (!canvas) {
        rafRef.current = requestAnimationFrame(render)
        return
      }
      const ctx = canvas.getContext('2d')
      if (!ctx) {
        rafRef.current = requestAnimationFrame(render)
        return
      }

      const w = canvas.width
      const h = canvas.height
      ctx.clearRect(0, 0, w, h)

      const pos = writePosRef.current
      const buf = bufferRef.current
      const ordered = new Float32Array(RING_SIZE)
      for (let i = 0; i < RING_SIZE; i++) {
        ordered[i] = buf[(pos + i) % RING_SIZE]
      }

      const m = modeRef.current
      if (m === 'waveform' || m === 'both') {
        const topH = m === 'both' ? h / 2 : h
        drawWaveform(ctx, ordered, CHUNK_SIZE, w, topH, 0)
      }
      if (m === 'fft' || m === 'both') {
        const offsetY = m === 'both' ? h / 2 : 0
        const fftH = m === 'both' ? h / 2 : h
        drawFFT(ctx, ordered, w, fftH, offsetY)
      }

      rafRef.current = requestAnimationFrame(render)
    }
    rafRef.current = requestAnimationFrame(render)
  }

  const stop = () => {
    cancelAnimationFrame(rafRef.current)
    rafRef.current = 0
    bufferRef.current = new Float32Array(RING_SIZE)
    writePosRef.current = 0
  }

  const setMode = (m: VisualizerMode) => {
    modeRef.current = m
    setModeState(m)
  }

  return { canvasRef, mode, setMode, pushChunk, start, stop }
}
