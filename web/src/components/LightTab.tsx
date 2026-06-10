import { useLightControl } from '../hooks/useLightControl'

const COLORS = [
  { name: 'vermelho', hex: '#ff0000' },
  { name: 'verde',    hex: '#00ff00' },
  { name: 'azul',     hex: '#0000ff' },
  { name: 'amarelo',  hex: '#ffff00' },
  { name: 'ciano',    hex: '#00ffff' },
  { name: 'magenta',  hex: '#ff00ff' },
  { name: 'laranja',  hex: '#ffa500' },
  { name: 'roxo',     hex: '#800080' },
  { name: 'branco',   hex: '#ffffff' },
]

interface LightTabProps {
  ip: string
}

export function LightTab({ ip }: LightTabProps) {
  const { selectedColor, intensity, setSelectedColor, setIntensity, sendCommand } = useLightControl(ip)

  const ipMissing = !ip.trim()

  const handleSwatchClick = (name: string) => {
    setSelectedColor(name)
    sendCommand(name, intensity)
  }

  const handleSliderChange = (e: React.ChangeEvent<HTMLInputElement>) => {
    setIntensity(Number(e.target.value))
  }

  const handleSliderRelease = () => {
    if (selectedColor) sendCommand(selectedColor, intensity)
  }

  return (
    <div>
      {ipMissing && (
        <p style={{ color: '#94a3b8', fontSize: 13, marginBottom: 16 }}>
          Configure o IP na aba Gravações
        </p>
      )}

      <div style={{
        display: 'grid',
        gridTemplateColumns: 'repeat(3, 1fr)',
        gap: 12,
        marginBottom: 24,
        pointerEvents: ipMissing ? 'none' : 'auto',
        opacity: ipMissing ? 0.4 : 1,
      }}>
        {COLORS.map(({ name, hex }) => {
          const isSelected = selectedColor === name
          const swatchOpacity = isSelected ? intensity / 100 : 1
          return (
            <button
              key={name}
              onClick={() => handleSwatchClick(name)}
              style={{
                display: 'flex',
                flexDirection: 'column',
                alignItems: 'center',
                gap: 6,
                background: 'none',
                border: 'none',
                cursor: 'pointer',
                padding: 4,
              }}
            >
              <span style={{
                width: 56,
                height: 56,
                borderRadius: '50%',
                background: hex,
                border: `3px solid ${isSelected ? '#3b82f6' : 'transparent'}`,
                display: 'block',
                opacity: swatchOpacity,
              }} />
              <span style={{ fontSize: 11, color: '#94a3b8' }}>{name}</span>
            </button>
          )
        })}
      </div>

      <div style={{
        background: '#1e293b',
        border: '1px solid #334155',
        borderRadius: 8,
        padding: '12px 16px',
      }}>
        <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: 8 }}>
          <label style={{ fontSize: 13, color: '#94a3b8' }}>Intensidade</label>
          <span style={{ fontSize: 13, color: '#f1f5f9' }}>{intensity}%</span>
        </div>
        <input
          type="range"
          min={0}
          max={100}
          value={intensity}
          onChange={handleSliderChange}
          onMouseUp={handleSliderRelease}
          onTouchEnd={handleSliderRelease}
          disabled={ipMissing}
          style={{ width: '100%', accentColor: '#3b82f6', cursor: ipMissing ? 'not-allowed' : 'pointer' }}
        />
        <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: 11, color: '#475569', marginTop: 2 }}>
          <span>0 — apagado</span>
          <span>brilho máximo — 100</span>
        </div>
      </div>
    </div>
  )
}
