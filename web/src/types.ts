export interface Recording {
  id: string
  name: string
  timestamp: number
  duration: number
  size: number
  blob: Blob
  transcription?: string
}

export type ConnectionState =
  | 'disconnected'
  | 'connecting'
  | 'idle'
  | 'recording'
  | 'receiving'
  | 'saving'
