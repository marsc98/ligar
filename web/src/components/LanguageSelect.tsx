export type LanguageCode = 'pt' | 'en' | 'es' | 'fr' | 'de'

export const WHISPER_LANG: Record<LanguageCode, string> = {
  pt: 'portuguese',
  en: 'english',
  es: 'spanish',
  fr: 'french',
  de: 'german',
}

const LABELS: Record<LanguageCode, string> = {
  pt: 'Português',
  en: 'English',
  es: 'Español',
  fr: 'Français',
  de: 'Deutsch',
}

type Props = {
  value: LanguageCode
  onChange: (code: LanguageCode) => void
}

export function LanguageSelect({ value, onChange }: Props) {
  return (
    <select
      value={value}
      onChange={e => onChange(e.target.value as LanguageCode)}
      style={{
        background: '#1e293b',
        color: '#f1f5f9',
        border: '1px solid #334155',
        borderRadius: 6,
        padding: '6px 10px',
        fontSize: 13,
        cursor: 'pointer',
      }}
    >
      {(Object.keys(WHISPER_LANG) as LanguageCode[]).map(code => (
        <option key={code} value={code}>{LABELS[code]}</option>
      ))}
    </select>
  )
}
