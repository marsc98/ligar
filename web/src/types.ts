export interface Recording {
  id: string
  name: string
  timestamp: number
  duration: number
  size: number
  blob: Blob
  transcription?: string
  collection?: {
    word: string
    sessionId: string
  }
}

export type VisualizerMode = 'waveform' | 'fft' | 'both'

export type ConnectionState =
  | 'disconnected'
  | 'connecting'
  | 'idle'
  | 'recording'
  | 'receiving'
  | 'saving'
