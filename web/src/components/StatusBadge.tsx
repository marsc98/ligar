import React from 'react'
import type { ConnectionState } from '../types'

interface StatusBadgeProps {
  state: ConnectionState
}

const CONFIG: Record<ConnectionState, { label: string; color: string; pulse: boolean }> = {
  disconnected: { label: 'Desconectado', color: '#6b7280', pulse: false },
  connecting:   { label: 'Conectando...', color: '#3b82f6', pulse: true },
  idle:         { label: 'Pronto',        color: '#22c55e', pulse: false },
  recording:    { label: 'Gravando...',   color: '#ef4444', pulse: true },
  receiving:    { label: 'Recebendo...', color: '#eab308', pulse: false },
  saving:       { label: 'Salvando...',  color: '#eab308', pulse: false },
}

export function StatusBadge({ state }: StatusBadgeProps): React.JSX.Element {
  const { label, color, pulse } = CONFIG[state]
  return (
    <>
      <style>{`
        @keyframes pulse {
          0%, 100% { opacity: 1; }
          50% { opacity: 0.5; }
        }
        .badge-pulse { animation: pulse 1.2s ease-in-out infinite; }
      `}</style>
      <span
        className={pulse ? 'badge-pulse' : undefined}
        style={{
          display: 'inline-flex',
          alignItems: 'center',
          gap: 6,
          fontSize: 13,
          fontWeight: 500,
          padding: '3px 10px',
          borderRadius: 12,
          backgroundColor: color + '22',
          color,
          border: `1px solid ${color}55`,
        }}
      >
        <span style={{ width: 7, height: 7, borderRadius: '50%', backgroundColor: color, display: 'inline-block' }} />
        {label}
      </span>
    </>
  )
}
