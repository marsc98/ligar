import { useEffect, useRef, useState } from 'react'
import type { VisualizerMode } from '../types'
import { drawWaveform, drawFFT } from '../lib/vizUtils'

interface AudioVisualizerProps {
  blob: Blob
  chunkSize?: number
  audioRef: React.RefObject<HTMLAudioElement | null>
  isPlaying: boolean
}

const MODES: VisualizerMode[] = ['waveform', 'fft', 'both']
const MODE_LABELS: Record<VisualizerMode, string> = { waveform: 'Onda', fft: 'FFT', both: 'Ambos' }

export function AudioVisualizer({ blob, chunkSize = 512, audioRef, isPlaying }: AudioVisualizerProps) {
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const [mode, setMode] = useState<VisualizerMode>('waveform')
  const [samples, setSamples] = useState<Float32Array | null>(null)
  const [error, setError] = useState(false)
  const rafRef = useRef<number>(0)

  useEffect(() => {
    let ctx: AudioContext | null = new AudioContext()
    blob.arrayBuffer()
      .then(buf => ctx!.decodeAudioData(buf))
      .then(decoded => {
        setSamples(decoded.getChannelData(0).slice())
        ctx!.close()
        ctx = null
      })
      .catch(() => {
        setError(true)
        ctx?.close()
        ctx = null
      })
    return () => { ctx?.close() }
  }, [blob])

  useEffect(() => {
    if (!samples) return
    const canvas = canvasRef.current
    if (!canvas) return
    const ctx = canvas.getContext('2d')
    if (!ctx) return

    const w = canvas.width
    const h = canvas.height

    const render = () => {
      ctx.clearRect(0, 0, w, h)

      if (mode === 'waveform' || mode === 'both') {
        const topH = mode === 'both' ? h / 2 : h
        drawWaveform(ctx, samples, chunkSize, w, topH, 0)
      }
      if (mode === 'fft' || mode === 'both') {
        const offsetY = mode === 'both' ? h / 2 : 0
        const fftH = mode === 'both' ? h / 2 : h
        drawFFT(ctx, samples, w, fftH, offsetY)
      }

      if (isPlaying) {
        const audio = audioRef.current
        if (audio && audio.duration) {
          const x = (audio.currentTime / audio.duration) * w
          ctx.strokeStyle = '#f8fafc'
          ctx.lineWidth = 1.5
          ctx.beginPath()
          ctx.moveTo(x, 0)
          ctx.lineTo(x, h)
          ctx.stroke()
        }
        rafRef.current = requestAnimationFrame(render)
      }
    }

    render()
    return () => cancelAnimationFrame(rafRef.current)
  }, [samples, mode, isPlaying, chunkSize, audioRef])

  const handleClick = (e: React.MouseEvent<HTMLCanvasElement>) => {
    const audio = audioRef.current
    if (!audio || !audio.duration) return
    const rect = (e.target as HTMLCanvasElement).getBoundingClientRect()
    audio.currentTime = ((e.clientX - rect.left) / rect.width) * audio.duration
  }

  if (error) {
    return <div style={{ fontSize: 13, color: '#94a3b8', padding: '8px 0' }}>Não foi possível carregar o áudio</div>
  }

  return (
    <div style={{ marginTop: 12 }}>
      <div style={{ display: 'flex', gap: 4, marginBottom: 8 }}>
        {MODES.map(m => (
          <button
            key={m}
            onClick={() => setMode(m)}
            style={{
              padding: '2px 10px',
              borderRadius: 12,
              border: '1px solid #334155',
              background: mode === m ? '#3b82f6' : '#1e293b',
              color: mode === m ? '#fff' : '#94a3b8',
              fontSize: 12,
              cursor: 'pointer',
            }}
          >
            {MODE_LABELS[m]}
          </button>
        ))}
      </div>
      <canvas
        ref={canvasRef}
        width={800}
        height={120}
        onClick={handleClick}
        style={{ width: '100%', height: 120, cursor: 'pointer', borderRadius: 6, background: '#0f172a', display: 'block' }}
      />
    </div>
  )
}
