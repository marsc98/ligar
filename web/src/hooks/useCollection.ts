import { useEffect, useRef, useState } from 'react'
import { assemblePcmToWav } from '../lib/wav'

const SAMPLE_RATE = 16000
const CHUNK_SAMPLES = 24000  // 1.5s
const MIN_FINAL_SAMPLES = 8000

export function useCollection(
  onSave: (blob: Blob, duration: number, collection: { word: string; sessionId: string }) => void
): {
  state: 'idle' | 'connecting' | 'collecting'
  sampleCount: number
  startCollection(ip: string, word: string): void
  stopCollection(): void
} {
  const [state, setState] = useState<'idle' | 'connecting' | 'collecting'>('idle')
  const [sampleCount, setSampleCount] = useState(0)
  const wsRef = useRef<WebSocket | null>(null)
  const bufferRef = useRef<Int16Array>(new Int16Array(0))
  const sessionRef = useRef<{ word: string; sessionId: string; sampleIndex: number } | null>(null)

  const flushChunk = (samples: Int16Array) => {
    const session = sessionRef.current
    if (!session) return
    session.sampleIndex++
    const blob = assemblePcmToWav(
      [samples.buffer.slice(samples.byteOffset, samples.byteOffset + samples.byteLength)],
      CHUNK_SAMPLES
    )
    onSave(blob, CHUNK_SAMPLES / SAMPLE_RATE, { word: session.word, sessionId: session.sessionId })
    setSampleCount(c => c + 1)
  }

  const appendChunk = (data: ArrayBuffer) => {
    const incoming = new Int16Array(data)
    const prev = bufferRef.current
    const merged = new Int16Array(prev.length + incoming.length)
    merged.set(prev)
    merged.set(incoming, prev.length)
    bufferRef.current = merged

    while (bufferRef.current.length >= CHUNK_SAMPLES) {
      const slice = bufferRef.current.slice(0, CHUNK_SAMPLES)
      bufferRef.current = bufferRef.current.slice(CHUNK_SAMPLES)
      flushChunk(slice)
    }
  }

  useEffect(() => {
    return () => {
      wsRef.current?.close()
      wsRef.current = null
    }
  }, [])

  const startCollection = (ip: string, word: string) => {
    setState('connecting')
    setSampleCount(0)
    bufferRef.current = new Int16Array(0)
    sessionRef.current = { word, sessionId: crypto.randomUUID(), sampleIndex: 0 }

    const ws = new WebSocket('ws://' + ip + '/record')
    ws.binaryType = 'arraybuffer'
    wsRef.current = ws

    ws.onopen = () => setState('collecting')

    ws.onmessage = (ev) => {
      if (typeof ev.data === 'string') {
        if (ev.data.startsWith('RECORDING_END:')) {
          const remaining = bufferRef.current
          if (remaining.length >= MIN_FINAL_SAMPLES) {
            const session = sessionRef.current
            if (session) {
              session.sampleIndex++
              const blob = assemblePcmToWav(
                [remaining.buffer.slice(remaining.byteOffset, remaining.byteOffset + remaining.byteLength)],
                remaining.length
              )
              onSave(blob, remaining.length / SAMPLE_RATE, { word: session.word, sessionId: session.sessionId })
              setSampleCount(c => c + 1)
            }
          }
          bufferRef.current = new Int16Array(0)
          wsRef.current?.close()
          wsRef.current = null
          setState('idle')
        }
      } else {
        appendChunk(ev.data as ArrayBuffer)
      }
    }

    ws.onclose = () => {
      bufferRef.current = new Int16Array(0)
      setState('idle')
    }

    ws.onerror = () => {
      setState('idle')
    }
  }

  const stopCollection = () => {
    wsRef.current?.close()
    wsRef.current = null
    bufferRef.current = new Int16Array(0)
    setState('idle')
  }

  return { state, sampleCount, startCollection, stopCollection }
}
