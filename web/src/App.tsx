import { useState } from 'react'
import { useConnection } from './hooks/useConnection'
import { useRecordings } from './hooks/useRecordings'
import { useAudioPlayer } from './hooks/useAudioPlayer'
import { useWhisper } from './hooks/useWhisper'
import { useStream } from './hooks/useStream'
import { ConnectionPanel } from './components/ConnectionPanel'
import { RecordingList } from './components/RecordingList'
import { LanguageSelect, WHISPER_LANG } from './components/LanguageSelect'
import { LiveTranscriptPanel } from './components/LiveTranscriptPanel'
import type { LanguageCode } from './components/LanguageSelect'
import type { Recording } from './types'

function handleDownload(recording: Recording) {
  const url = URL.createObjectURL(recording.blob)
  const a = document.createElement('a')
  a.href = url
  a.download = recording.name + '.wav'
  a.click()
  setTimeout(() => URL.revokeObjectURL(url), 100)
}

export function App() {
  const [language, setLanguage] = useState<LanguageCode>('pt')
  const [liveText, setLiveText] = useState('')
  const [ip, setIp] = useState('')

  const { recordings, loading, dbError, addRecording, deleteRecording, updateTranscription } = useRecordings()
  const { currentId, audioRef, play, stop } = useAudioPlayer()

  const { transcribe, transcribeLive, modelStatus, transcribingIds } = useWhisper({
    language: WHISPER_LANG[language],
    onComplete: updateTranscription,
    onLive: (text) => setLiveText(prev => prev ? prev + ' ' + text : text),
  })

  const { streaming, connectStream, disconnectStream } = useStream({ onWindow: transcribeLive })

  const { state, error, connect, disconnect } = useConnection(
    async (blob, duration) => {
      const id = await addRecording(blob, duration)
      if (id) transcribe(id, blob)
    }
  )

  const handleDelete = (id: string) => {
    if (currentId === id) stop()
    deleteRecording(id)
  }

  const handleDisconnect = () => {
    disconnect()
  }

  const handleDisconnectStream = () => {
    disconnectStream()
    setLiveText('')
  }

  return (
    <main>
      <h1 style={{ fontSize: 24, fontWeight: 600, marginBottom: 24, color: '#f1f5f9' }}>
        ESP32 Audio Recorder
      </h1>
      {dbError && (
        <p role="alert" style={{ color: '#f87171', fontSize: 14, marginBottom: 16 }}>
          {dbError}
        </p>
      )}
      <div style={{ display: 'flex', alignItems: 'center', gap: 12, marginBottom: 16 }}>
        <LanguageSelect value={language} onChange={setLanguage} />
        {modelStatus === 'loading' && (
          <p style={{ color: '#94a3b8', fontSize: 13, margin: 0 }}>
            Baixando modelo de fala (primeira vez)...
          </p>
        )}
      </div>
      <ConnectionPanel
        state={state}
        error={error}
        ip={ip}
        onIpChange={setIp}
        onConnect={connect}
        onDisconnect={handleDisconnect}
      />
      <div style={{ marginBottom: 16 }}>
        <button
          onClick={() => streaming ? handleDisconnectStream() : connectStream(ip)}
          disabled={!streaming && !ip.trim()}
          style={{
            background: streaming ? '#ef444422' : '#22c55e22',
            color: streaming ? '#ef4444' : '#22c55e',
            border: `1px solid ${streaming ? '#ef4444' : '#22c55e'}`,
            borderRadius: 6,
            padding: '6px 14px',
            fontSize: 13,
            cursor: 'pointer',
            opacity: !streaming && !ip.trim() ? 0.4 : 1,
          }}
        >
          {streaming ? 'Parar stream ao vivo' : 'Iniciar stream ao vivo'}
        </button>
      </div>
      <LiveTranscriptPanel text={liveText} active={streaming} />
      <RecordingList
        recordings={recordings}
        loading={loading}
        currentId={currentId}
        transcribingIds={transcribingIds}
        onPlay={play}
        onDelete={handleDelete}
        onDownload={handleDownload}
      />
      <audio ref={audioRef} />
    </main>
  )
}

export default App
