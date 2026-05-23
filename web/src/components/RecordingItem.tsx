import type { Recording } from '../types'
import { AudioVisualizer } from './AudioVisualizer'

interface RecordingItemProps {
  recording: Recording
  isPlaying: boolean
  transcribing: boolean
  isExpanded: boolean
  onPlay(recording: Recording): void
  onDelete(id: string): void
  onDownload(recording: Recording): void
  onToggleViz(): void
  audioRef: React.RefObject<HTMLAudioElement | null>
}

function formatDuration(seconds: number): string {
  const m = Math.floor(seconds / 60)
  const s = Math.floor(seconds % 60)
  return `${m}:${s.toString().padStart(2, '0')}`
}

export function RecordingItem({ recording, isPlaying, transcribing, isExpanded, onPlay, onDelete, onDownload, onToggleViz, audioRef }: RecordingItemProps) {
  return (
    <div style={{
      borderRadius: 8,
      background: '#1e293b',
      border: '1px solid #334155',
    }}>
      <div style={{ display: 'flex', alignItems: 'center', gap: 12, padding: '12px 16px' }}>
        <div style={{ flex: 1, minWidth: 0 }}>
          <div style={{ fontSize: 14, fontWeight: 500, color: '#f1f5f9', overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
            {recording.name}
          </div>
          <div style={{ fontSize: 12, color: '#94a3b8', marginTop: 2 }}>
            {new Date(recording.timestamp).toLocaleString('pt-BR')} · {formatDuration(recording.duration)} · {(recording.size / 1024).toFixed(0)} KB
          </div>
          {transcribing && (
            <div style={{ fontSize: 12, color: '#94a3b8', marginTop: 4, fontStyle: 'italic' }}>
              Transcrevendo...
            </div>
          )}
          {!transcribing && recording.transcription && (
            <div style={{ fontSize: 13, color: '#cbd5e1', marginTop: 6 }}>
              {recording.transcription}
            </div>
          )}
        </div>
        <button
          onClick={() => onPlay(recording)}
          title={isPlaying ? 'Pausar' : 'Reproduzir'}
          style={iconBtnStyle(isPlaying ? '#22c55e' : '#3b82f6')}
        >
          {isPlaying ? '⏸' : '▶'}
        </button>
        <button
          onClick={onToggleViz}
          title="Visualizar"
          style={iconBtnStyle(isExpanded ? '#f59e0b' : '#64748b')}
        >
          📊
        </button>
        <button
          onClick={() => onDownload(recording)}
          title="Download"
          style={iconBtnStyle('#6366f1')}
        >
          ⬇
        </button>
        <button
          onClick={() => onDelete(recording.id)}
          title="Excluir"
          style={iconBtnStyle('#ef4444')}
        >
          ✕
        </button>
      </div>
      {isExpanded && (
        <div style={{ padding: '0 16px 12px' }}>
          <AudioVisualizer blob={recording.blob} audioRef={audioRef} isPlaying={isPlaying} />
        </div>
      )}
    </div>
  )
}

function iconBtnStyle(color: string): React.CSSProperties {
  return {
    width: 32,
    height: 32,
    borderRadius: 6,
    border: 'none',
    background: color + '22',
    color,
    cursor: 'pointer',
    fontSize: 14,
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center',
    flexShrink: 0,
  }
}
