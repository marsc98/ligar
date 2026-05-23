import { type KeyboardEvent } from 'react'
import { StatusBadge } from './StatusBadge'
import type { ConnectionState } from '../types'

interface ConnectionPanelProps {
  state: ConnectionState
  error: string | null
  ip: string
  onIpChange(ip: string): void
  onConnect(ip: string): void
  onDisconnect(): void
}

export function ConnectionPanel({ state, error, ip, onIpChange, onConnect, onDisconnect }: ConnectionPanelProps) {
  const connected = state !== 'disconnected'

  const handleConnect = () => {
    if (ip.trim()) onConnect(ip.trim())
  }

  const handleKeyDown = (e: KeyboardEvent<HTMLInputElement>) => {
    if (e.key === 'Enter' && !connected) handleConnect()
  }

  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 12, marginBottom: 24 }}>
      <div style={{ display: 'flex', alignItems: 'center', gap: 12 }}>
        <input
          type="text"
          value={ip}
          onChange={(e) => onIpChange(e.target.value)}
          onKeyDown={handleKeyDown}
          disabled={connected}
          placeholder="192.168.1.100"
          style={{
            padding: '8px 12px',
            borderRadius: 6,
            border: '1px solid #334155',
            background: connected ? '#1e293b' : '#0f172a',
            color: '#e2e8f0',
            fontSize: 14,
            width: 200,
            opacity: connected ? 0.6 : 1,
          }}
        />
        {!connected ? (
          <button
            onClick={handleConnect}
            style={btnStyle('#3b82f6')}
          >
            Conectar
          </button>
        ) : (
          <button
            onClick={onDisconnect}
            style={btnStyle('#6b7280')}
          >
            Desconectar
          </button>
        )}
        <StatusBadge state={state} />
      </div>
      {error && (
        <p style={{ margin: 0, fontSize: 13, color: '#f87171' }}>{error}</p>
      )}
    </div>
  )
}

function btnStyle(bg: string): React.CSSProperties {
  return {
    padding: '8px 16px',
    borderRadius: 6,
    border: 'none',
    background: bg,
    color: '#fff',
    fontSize: 14,
    cursor: 'pointer',
    fontWeight: 500,
  }
}
