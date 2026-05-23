import { useState } from 'react'
import { RecordingItem } from './RecordingItem'
import type { Recording } from '../types'

interface RecordingListProps {
  recordings: Recording[]
  loading: boolean
  currentId: string | null
  transcribingIds: Set<string>
  audioRef: React.RefObject<HTMLAudioElement | null>
  onPlay(recording: Recording): void
  onDelete(id: string): void
  onDownload(recording: Recording): void
}

export function RecordingList({ recordings, loading, currentId, transcribingIds, audioRef, onPlay, onDelete, onDownload }: RecordingListProps) {
  const [expandedId, setExpandedId] = useState<string | null>(null)

  const toggleViz = (id: string) =>
    setExpandedId(prev => prev === id ? null : id)

  if (loading) {
    return <p style={{ color: '#94a3b8', fontSize: 14 }}>Carregando...</p>
  }

  if (recordings.length === 0) {
    return (
      <p style={{ color: '#94a3b8', fontSize: 14, textAlign: 'center', padding: '32px 0' }}>
        Nenhuma gravação ainda
      </p>
    )
  }

  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 8 }}>
      {recordings.map((r) => (
        <RecordingItem
          key={r.id}
          recording={r}
          isPlaying={currentId === r.id}
          transcribing={transcribingIds.has(r.id)}
          isExpanded={expandedId === r.id}
          audioRef={audioRef}
          onPlay={onPlay}
          onDelete={onDelete}
          onDownload={onDownload}
          onToggleViz={() => toggleViz(r.id)}
        />
      ))}
    </div>
  )
}
