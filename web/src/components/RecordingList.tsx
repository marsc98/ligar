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

type ListItem =
  | { kind: 'individual'; recording: Recording; ts: number }
  | { kind: 'session'; sessionId: string; word: string; items: Recording[]; ts: number }

export function RecordingList({ recordings, loading, currentId, transcribingIds, audioRef, onPlay, onDelete, onDownload }: RecordingListProps) {
  const [expandedVizId, setExpandedVizId] = useState<string | null>(null)
  const [expandedSessions, setExpandedSessions] = useState<Set<string>>(new Set())

  const toggleViz = (id: string) =>
    setExpandedVizId(prev => prev === id ? null : id)

  const toggleSession = (sessionId: string) =>
    setExpandedSessions(prev => {
      const next = new Set(prev)
      if (next.has(sessionId)) next.delete(sessionId)
      else next.add(sessionId)
      return next
    })

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

  // Agrupar por sessão
  const sessions = new Map<string, Recording[]>()
  const individual: Recording[] = []

  for (const r of recordings) {
    if (r.collection) {
      const key = r.collection.sessionId
      sessions.set(key, [...(sessions.get(key) ?? []), r])
    } else {
      individual.push(r)
    }
  }

  // Montar lista unificada
  const items: ListItem[] = [
    ...individual.map(r => ({ kind: 'individual' as const, recording: r, ts: r.timestamp })),
    ...Array.from(sessions.entries()).map(([sessionId, recs]) => ({
      kind: 'session' as const,
      sessionId,
      word: recs[0].collection!.word,
      items: [...recs].sort((a, b) => a.timestamp - b.timestamp),
      ts: Math.max(...recs.map(r => r.timestamp)),
    })),
  ].sort((a, b) => b.ts - a.ts)

  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 8 }}>
      {items.map((item) => {
        if (item.kind === 'individual') {
          return (
            <RecordingItem
              key={item.recording.id}
              recording={item.recording}
              isPlaying={currentId === item.recording.id}
              transcribing={transcribingIds.has(item.recording.id)}
              isExpanded={expandedVizId === item.recording.id}
              audioRef={audioRef}
              onPlay={onPlay}
              onDelete={onDelete}
              onDownload={onDownload}
              onToggleViz={() => toggleViz(item.recording.id)}
            />
          )
        }

        const isOpen = expandedSessions.has(item.sessionId)
        return (
          <div key={item.sessionId} style={{ borderRadius: 8, background: '#1e293b', border: '1px solid #334155' }}>
            <button
              onClick={() => toggleSession(item.sessionId)}
              style={{
                width: '100%',
                display: 'flex',
                alignItems: 'center',
                gap: 10,
                padding: '12px 16px',
                background: 'none',
                border: 'none',
                cursor: 'pointer',
                textAlign: 'left',
              }}
            >
              <span style={{ color: '#64748b', fontSize: 12 }}>{isOpen ? '▼' : '▶'}</span>
              <span style={{ color: '#f1f5f9', fontSize: 14, fontWeight: 500 }}>{item.word}</span>
              <span style={{ color: '#64748b', fontSize: 12 }}>— {item.items.length} amostras</span>
              <span style={{ color: '#64748b', fontSize: 12, marginLeft: 'auto' }}>
                {new Date(item.ts).toLocaleString('pt-BR')}
              </span>
            </button>
            {isOpen && (
              <div style={{ padding: '0 8px 8px', display: 'flex', flexDirection: 'column', gap: 4 }}>
                {item.items.map(r => (
                  <RecordingItem
                    key={r.id}
                    recording={r}
                    isPlaying={currentId === r.id}
                    transcribing={transcribingIds.has(r.id)}
                    isExpanded={expandedVizId === r.id}
                    audioRef={audioRef}
                    onPlay={onPlay}
                    onDelete={onDelete}
                    onDownload={onDownload}
                    onToggleViz={() => toggleViz(r.id)}
                  />
                ))}
              </div>
            )}
          </div>
        )
      })}
    </div>
  )
}
