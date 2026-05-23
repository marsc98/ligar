import type { VisualizerMode } from '../types'

interface StreamVisualizerProps {
  canvasRef: React.RefObject<HTMLCanvasElement | null>
  mode: VisualizerMode
  onModeChange: (m: VisualizerMode) => void
}

const MODES: VisualizerMode[] = ['waveform', 'fft', 'both']
const MODE_LABELS: Record<VisualizerMode, string> = { waveform: 'Onda', fft: 'FFT', both: 'Ambos' }

export function StreamVisualizer({ canvasRef, mode, onModeChange }: StreamVisualizerProps) {
  return (
    <div style={{ marginBottom: 12 }}>
      <div style={{ display: 'flex', gap: 4, marginBottom: 8 }}>
        {MODES.map(m => (
          <button
            key={m}
            onClick={() => onModeChange(m)}
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
        height={80}
        style={{ width: '100%', height: 80, borderRadius: 6, background: '#0f172a', display: 'block' }}
      />
    </div>
  )
}
