import { useState } from 'react'
import JSZip from 'jszip'
import { useCollection } from '../hooks/useCollection'

interface SessionItem {
  name: string
  duration: number
  blob: Blob
}

interface CollectionTabProps {
  ip: string
  onSave(blob: Blob, duration: number, collection: { word: string; sessionId: string }): void
}

async function downloadZip(word: string, items: SessionItem[]) {
  const zip = new JSZip()
  for (const item of items) {
    zip.file(`${item.name}.wav`, item.blob)
  }
  const blob = await zip.generateAsync({ type: 'blob' })
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = `${word}_samples.zip`
  a.click()
  setTimeout(() => URL.revokeObjectURL(url), 100)
}

export function CollectionTab({ ip, onSave }: CollectionTabProps) {
  const [word, setWord] = useState('')
  const [sessionWord, setSessionWord] = useState('')
  const [sessionItems, setSessionItems] = useState<SessionItem[]>([])

  const { state, sampleCount, startCollection, stopCollection } = useCollection(
    (blob, duration, collection) => {
      setSessionItems(prev => {
        const name = `${collection.word}_${String(prev.length + 1).padStart(3, '0')}`
        return [...prev, { name, duration, blob }]
      })
      onSave(blob, duration, collection)
    }
  )

  const handleStart = () => {
    const trimmed = word.trim()
    setSessionWord(trimmed)
    setSessionItems([])
    startCollection(ip, trimmed)
  }

  const handleStop = () => {
    stopCollection()
  }

  const collecting = state === 'collecting'
  const canStart = !!ip.trim() && !!word.trim() && state === 'idle'
  const showDownload = state === 'idle' && sessionItems.length > 0

  return (
    <div>
      <div style={{ marginBottom: 16 }}>
        <label style={{ display: 'block', fontSize: 13, color: '#94a3b8', marginBottom: 6 }}>
          Nome da palavra
        </label>
        <input
          value={word}
          onChange={e => setWord(e.target.value)}
          disabled={collecting}
          placeholder="ex: ligar"
          style={{
            background: '#1e293b',
            border: '1px solid #334155',
            borderRadius: 6,
            color: '#f1f5f9',
            fontSize: 14,
            padding: '8px 12px',
            width: 200,
            outline: 'none',
          }}
        />
      </div>

      {!collecting ? (
        <div style={{ display: 'flex', gap: 8, alignItems: 'center', flexWrap: 'wrap' }}>
          <button
            onClick={handleStart}
            disabled={!canStart}
            style={{
              background: canStart ? '#3b82f622' : '#1e293b',
              color: canStart ? '#3b82f6' : '#64748b',
              border: `1px solid ${canStart ? '#3b82f6' : '#334155'}`,
              borderRadius: 6,
              padding: '8px 18px',
              fontSize: 14,
              cursor: canStart ? 'pointer' : 'default',
            }}
          >
            Iniciar Coleta
          </button>
          {showDownload && (
            <button
              onClick={() => downloadZip(sessionWord, sessionItems)}
              style={{
                background: '#22c55e22',
                color: '#22c55e',
                border: '1px solid #22c55e',
                borderRadius: 6,
                padding: '8px 18px',
                fontSize: 14,
                cursor: 'pointer',
              }}
            >
              Baixar ZIP ({sessionItems.length} amostras)
            </button>
          )}
        </div>
      ) : (
        <div style={{ display: 'flex', flexDirection: 'column', gap: 8 }}>
          <div style={{ fontSize: 14, color: '#f1f5f9' }}>
            <span style={{ color: '#ef4444', marginRight: 6 }}>●</span>
            Gravando: <strong>{sessionWord}</strong> · {sampleCount} amostras capturadas
          </div>
          <div style={{ fontSize: 12, color: '#64748b' }}>✂ corte automático a cada 1.5s</div>
          <button
            onClick={handleStop}
            style={{
              background: '#ef444422',
              color: '#ef4444',
              border: '1px solid #ef4444',
              borderRadius: 6,
              padding: '8px 18px',
              fontSize: 14,
              cursor: 'pointer',
              alignSelf: 'flex-start',
            }}
          >
            Encerrar Coleta
          </button>
        </div>
      )}

      {sessionItems.length > 0 && (
        <div style={{ marginTop: 20 }}>
          <div style={{ fontSize: 13, color: '#64748b', marginBottom: 8 }}>
            Amostras desta sessão:
          </div>
          <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
            {sessionItems.map((item, i) => (
              <div key={i} style={{
                display: 'flex', alignItems: 'center', gap: 8,
                fontSize: 13, color: '#94a3b8',
                padding: '4px 0',
              }}>
                <span style={{ color: '#22c55e', fontSize: 10 }}>✓</span>
                {item.name}.wav
                <span style={{ color: '#64748b' }}>{item.duration.toFixed(1)}s</span>
              </div>
            ))}
          </div>
        </div>
      )}
    </div>
  )
}
