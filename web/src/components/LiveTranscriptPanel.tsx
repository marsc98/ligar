type Props = {
  text: string
  active: boolean
}

export function LiveTranscriptPanel({ text, active }: Props) {
  if (!active) return null

  return (
    <div style={{
      marginTop: 16,
      padding: '12px 16px',
      borderRadius: 8,
      background: '#1e293b',
      border: '1px solid #334155',
    }}>
      <div style={{ fontSize: 12, color: '#64748b', marginBottom: 6, fontWeight: 500, textTransform: 'uppercase', letterSpacing: '0.05em' }}>
        Transcrição ao vivo
      </div>
      <div style={{ fontSize: 14, color: text ? '#f1f5f9' : '#475569', fontStyle: text ? 'normal' : 'italic' }}>
        {text || 'Aguardando fala...'}
      </div>
    </div>
  )
}
