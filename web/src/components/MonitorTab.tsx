import { useEffect, useRef, useState } from 'react'
import {
  type MonitorEvent,
  startSession,
  logEvent,
  stopSession,
  downloadSession,
  sessionStats,
} from '../utils/monitorLogger'

const THRESHOLD_MIN = 0.5
const THRESHOLD_MAX = 8
const THRESHOLD_DEFAULT = 4.2

export function MonitorTab({ ip }: { ip: string }) {
  const [events, setEvents] = useState<MonitorEvent[]>([])
  const [connected, setConnected] = useState(false)
  const [lastDetection, setLastDetection] = useState<{ word: string; ts: number } | null>(null)
  const [kwsMode, setKwsMode] = useState<string>('idle')
  const [threshold, setThreshold] = useState(THRESHOLD_DEFAULT)
  const [inputText, setInputText] = useState(String(THRESHOLD_DEFAULT))
  const [connectKey, setConnectKey] = useState(0)
  const [stats, setStats] = useState(sessionStats())
  const wsRef = useRef<WebSocket | null>(null)
  const syncedRef = useRef(false)

  useEffect(() => {
    if (!ip.trim()) return

    syncedRef.current = false
    let ws: WebSocket | null = null

    const tid = setTimeout(() => {
      ws = new WebSocket('ws://' + ip + '/monitor')
      wsRef.current = ws

      ws.onopen = () => setConnected(true)

      ws.onmessage = (ev) => {
        try {
          const data = JSON.parse(ev.data as string)
          const event: MonitorEvent = {
            ts: Date.now(),
            rms: data.rms,
            threshold: data.threshold,
            word: data.word ?? null,
            probs: data.probs ?? {},
            var: data.var,
            rejected: data.rejected,
            kws_mode: data.kws_mode,
          }
          setEvents(prev => [event, ...prev].slice(0, 200))
          logEvent(event).then(() => setStats(sessionStats()))
          if (event.word) setLastDetection({ word: event.word, ts: event.ts })
          if (data.kws_mode) setKwsMode(data.kws_mode)
          if (!syncedRef.current && typeof data.threshold === 'number' && isFinite(data.threshold)) {
            const v = Math.round(data.threshold * 100) / 100
            setThreshold(v)
            setInputText(String(v))
            syncedRef.current = true
          }
        } catch {
          // ignore malformed frames
        }
      }

      ws.onclose = () => { setConnected(false); syncedRef.current = false }
      ws.onerror = () => { setConnected(false); syncedRef.current = false }
    }, 50)

    return () => {
      clearTimeout(tid)
      ws?.close()
      wsRef.current = null
    }
  }, [ip, connectKey])

  const retryRef = useRef<ReturnType<typeof setTimeout> | null>(null)

  useEffect(() => {
    if (connected || !ip.trim()) {
      if (retryRef.current) {
        clearTimeout(retryRef.current)
        retryRef.current = null
      }
      return
    }
    retryRef.current = setTimeout(() => {
      setConnectKey(k => k + 1)
    }, 3000)
    return () => {
      if (retryRef.current) clearTimeout(retryRef.current)
    }
  }, [connected, ip])

  const debounceRef = useRef<ReturnType<typeof setTimeout> | null>(null)
  const sendThreshold = (value: number) => {
    if (!ip.trim()) return
    if (debounceRef.current) clearTimeout(debounceRef.current)
    debounceRef.current = setTimeout(() => {
      fetch(`http://${ip}/threshold?v=${value.toFixed(2)}`).catch(() => {})
    }, 150)
  }

  const handleSliderChange = (e: React.ChangeEvent<HTMLInputElement>) => {
    const value = Number(e.target.value)
    setThreshold(value)
    setInputText(String(value))
    sendThreshold(value)
  }

  const handleInputTextChange = (e: React.ChangeEvent<HTMLInputElement>) => {
    setInputText(e.target.value)
  }

  const commitInput = () => {
    const value = Number(inputText)
    if (!isNaN(value) && value >= THRESHOLD_MIN && value <= THRESHOLD_MAX) {
      setThreshold(value)
      sendThreshold(value)
    } else {
      setInputText(String(threshold))
    }
  }

  const handleInputKeyDown = (e: React.KeyboardEvent<HTMLInputElement>) => {
    if (e.key === 'Enter') commitInput()
    if (e.key === 'Escape') setInputText(String(threshold))
  }

  const handleStartRecording = async () => {
    await startSession(ip)
    setStats(sessionStats())
  }

  const handleStopRecording = async () => {
    await stopSession()
    setStats(sessionStats())
  }

  const handleDownload = () => {
    downloadSession()
  }

  return (
    <div>
      {/* Status + reconectar */}
      <div style={{ display: 'flex', alignItems: 'center', gap: 8, marginBottom: 16 }}>
        <span style={{
          width: 8, height: 8, borderRadius: '50%',
          background: connected ? '#22c55e' : '#ef4444',
          display: 'inline-block', flexShrink: 0,
        }} />
        <span style={{ fontSize: 13, color: '#94a3b8', flex: 1 }}>
          {connected ? `ws://${ip}/monitor` : (ip.trim() ? `Desconectado — ${ip}` : 'Defina o IP na aba Gravações')}
        </span>
        {connected && (
          <span style={{
            fontSize: 11, padding: '2px 8px', borderRadius: 4,
            background: kwsMode === 'await_color' ? '#f59e0b22' : '#334155',
            color: kwsMode === 'await_color' ? '#f59e0b' : '#64748b',
            border: `1px solid ${kwsMode === 'await_color' ? '#f59e0b44' : '#475569'}`,
          }}>
            {kwsMode === 'await_color' ? 'aguardando cor' : 'idle'}
          </span>
        )}
        {!connected && ip.trim() && (
          <button
            onClick={() => setConnectKey(k => k + 1)}
            style={{
              background: '#3b82f622', color: '#3b82f6',
              border: '1px solid #3b82f6', borderRadius: 4,
              padding: '3px 10px', fontSize: 12, cursor: 'pointer',
            }}
          >
            Reconectar
          </button>
        )}
      </div>

      {/* Threshold MLP */}
      <div style={{
        background: '#1e293b', border: '1px solid #334155',
        borderRadius: 8, padding: '12px 16px', marginBottom: 16,
      }}>
        <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', marginBottom: 10 }}>
          <label style={{ fontSize: 13, color: '#94a3b8' }}>Threshold MLP</label>
          <input
            type="number"
            value={inputText}
            onChange={handleInputTextChange}
            onBlur={commitInput}
            onKeyDown={handleInputKeyDown}
            min={THRESHOLD_MIN} max={THRESHOLD_MAX} step={0.1}
            disabled={!connected}
            style={{
              background: '#0f172a', border: '1px solid #334155', borderRadius: 4,
              color: '#f1f5f9', fontSize: 13, padding: '3px 8px',
              width: 72, textAlign: 'right', outline: 'none',
            }}
          />
        </div>
        <input
          type="range" min={THRESHOLD_MIN} max={THRESHOLD_MAX} step={0.1}
          value={Math.min(Math.max(threshold, THRESHOLD_MIN), THRESHOLD_MAX)}
          onChange={handleSliderChange} disabled={!connected}
          style={{ width: '100%', accentColor: '#3b82f6', cursor: connected ? 'pointer' : 'not-allowed' }}
        />
        <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: 11, color: '#475569', marginTop: 2 }}>
          <span>{THRESHOLD_MIN} — mais sensível</span>
          <span>menos sensível — {THRESHOLD_MAX}</span>
        </div>
      </div>

      {/* Controles de log */}
      <div style={{
        display: 'flex', alignItems: 'center', gap: 8,
        marginBottom: 12, padding: '8px 12px',
        background: '#1e293b', border: '1px solid #334155', borderRadius: 8,
      }}>
        <span style={{ fontSize: 12, color: '#64748b', flex: 1 }}>
          {stats.isRecording
            ? `Gravando — ${stats.total} eventos, ${stats.detections} detecções`
            : stats.hasEvents
              ? `${stats.total} eventos · ${stats.detections} detecções · ${stats.rejections} rejeições`
              : 'Sem sessão de log'}
        </span>
        {!stats.isRecording && (
          <button
            onClick={handleStartRecording}
            style={{
              background: '#22c55e22', color: '#22c55e',
              border: '1px solid #22c55e44', borderRadius: 4,
              padding: '3px 10px', fontSize: 12, cursor: 'pointer',
            }}
          >
            Iniciar log
          </button>
        )}
        {stats.isRecording && (
          <button
            onClick={handleStopRecording}
            style={{
              background: '#ef444422', color: '#ef4444',
              border: '1px solid #ef444444', borderRadius: 4,
              padding: '3px 10px', fontSize: 12, cursor: 'pointer',
            }}
          >
            Parar log
          </button>
        )}
        {stats.hasEvents && (
          <button
            onClick={handleDownload}
            style={{
              background: '#3b82f622', color: '#3b82f6',
              border: '1px solid #3b82f644', borderRadius: 4,
              padding: '3px 10px', fontSize: 12, cursor: 'pointer',
            }}
          >
            Baixar .jsonl
          </button>
        )}
      </div>

      {!ip.trim() && (
        <p style={{ color: '#64748b', fontSize: 13 }}>Informe o IP na aba Gravações para conectar.</p>
      )}

      {/* Última detecção */}
      {lastDetection && (
        <div style={{
          display: 'flex', alignItems: 'center', gap: 12,
          background: '#22c55e18', border: '1px solid #22c55e44',
          borderRadius: 8, padding: '10px 16px', marginBottom: 12,
        }}>
          <span style={{ fontSize: 20, color: '#22c55e' }}>✓</span>
          <div>
            <div style={{ color: '#22c55e', fontWeight: 700, fontSize: 15 }}>
              {lastDetection.word}
            </div>
            <div style={{ color: '#64748b', fontSize: 11 }}>
              última detecção — {new Date(lastDetection.ts).toLocaleTimeString('pt-BR')}
            </div>
          </div>
        </div>
      )}

      {/* Lista de eventos */}
      <div style={{ display: 'flex', flexDirection: 'column', gap: 2 }}>
        {events.map((ev, i) => {
          const isDetection = !!ev.word
          const isRejected = !!ev.rejected
          const isHeartbeat = Object.keys(ev.probs).length === 0 && !ev.rejected && !ev.word
          const sortedProbs = Object.entries(ev.probs).sort(([, a], [, b]) => b - a)

          return (
            <div
              key={i}
              style={{
                padding: '5px 10px', borderRadius: 6,
                fontSize: 12, fontFamily: 'monospace',
                background: isDetection ? '#22c55e18' : isRejected ? '#f59e0b08' : 'transparent',
                borderLeft: isDetection ? '3px solid #22c55e' : isRejected ? '3px solid #f59e0b' : '3px solid transparent',
                color: '#94a3b8',
                opacity: isHeartbeat ? 0.45 : 1,
              }}
            >
              <span style={{ color: '#475569', marginRight: 8 }}>
                {new Date(ev.ts).toLocaleTimeString('pt-BR')}
              </span>

              {isDetection && (
                <span style={{
                  background: '#22c55e33', color: '#22c55e',
                  borderRadius: 4, padding: '1px 6px',
                  marginRight: 8, fontWeight: 700,
                }}>
                  ✓ {ev.word}
                </span>
              )}

              {ev.rejected && (
                <span style={{
                  background: '#f59e0b22', color: '#f59e0b',
                  borderRadius: 4, padding: '1px 6px',
                  marginRight: 8, fontSize: 11,
                }}>
                  ✗ {ev.rejected}
                </span>
              )}

              <span style={{ color: ev.rms > 300 ? '#cbd5e1' : '#475569' }}>
                rms={ev.rms.toFixed(0)}
              </span>

              {ev.var !== undefined && (
                <span style={{ marginLeft: 8, color: '#475569' }}>
                  var={ev.var.toFixed(2)}
                </span>
              )}

              {sortedProbs.slice(0, 4).map(([w, p]) => (
                <span
                  key={w}
                  style={{
                    marginLeft: 8,
                    color: p > 0.75 ? '#22c55e' : p > 0.4 ? '#f59e0b' : '#475569',
                    fontWeight: ev.word === w ? 700 : 400,
                  }}
                >
                  {w}={p.toFixed(2)}
                </span>
              ))}
            </div>
          )
        })}
        {events.length === 0 && connected && (
          <p style={{ color: '#64748b', fontSize: 13, textAlign: 'center', padding: '24px 0' }}>
            Conectado — aguardando heartbeat da ESP32...
          </p>
        )}
        {events.length === 0 && !connected && ip.trim() && (
          <p style={{ color: '#64748b', fontSize: 13, textAlign: 'center', padding: '24px 0' }}>
            Sem conexão — verifique o IP e clique em Reconectar.
          </p>
        )}
      </div>
    </div>
  )
}
