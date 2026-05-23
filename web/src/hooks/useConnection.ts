import { useRef, useState, useEffect } from 'react'
import { assemblePcmToWav } from '../lib/wav'
import type { ConnectionState } from '../types'

const SAMPLE_RATE = 16000

export function useConnection(onRecordingSaved: (blob: Blob, duration: number) => void): {
  state: ConnectionState
  error: string | null
  connect(ip: string): void
  disconnect(): void
} {
  const [state, setState] = useState<ConnectionState>('disconnected')
  const [error, setError] = useState<string | null>(null)
  const wsRef    = useRef<WebSocket | null>(null)
  const chunksRef = useRef<ArrayBuffer[]>([])
  const stateRef  = useRef<ConnectionState>('disconnected')

  const setStateBoth = (s: ConnectionState) => {
    stateRef.current = s
    setState(s)
  }

  const connect = (ip: string) => {
    setError(null)
    setStateBoth('connecting')
    const ws = new WebSocket('ws://' + ip + '/record')
    ws.binaryType = 'arraybuffer'
    wsRef.current = ws

    ws.onopen = () => {
      setStateBoth('idle')
    }

    ws.onmessage = (ev) => {
      if (typeof ev.data === 'string') {
        if (ev.data === 'RECORDING_START') {
          chunksRef.current = []
          setStateBoth('recording')
        } else if (ev.data.startsWith('RECORDING_END:')) {
          const numSamples = parseInt(ev.data.split(':')[1], 10)
          const chunks = chunksRef.current
          chunksRef.current = []
          setStateBoth('saving')
          const blob     = assemblePcmToWav(chunks, numSamples)
          const duration = numSamples / SAMPLE_RATE
          onRecordingSaved(blob, duration)
          setStateBoth('idle')
        }
      } else {
        if (stateRef.current === 'recording') {
          setStateBoth('receiving')
        }
        chunksRef.current.push(ev.data as ArrayBuffer)
      }
    }

    ws.onclose = () => {
      if (stateRef.current === 'receiving') {
        setError('Gravação incompleta — reconecte e tente novamente')
      }
      chunksRef.current = []
      setStateBoth('disconnected')
    }

    ws.onerror = () => {
      setError('Não foi possível conectar ao IP informado')
    }
  }

  const disconnect = () => {
    wsRef.current?.close()
    wsRef.current = null
    chunksRef.current = []
    setStateBoth('disconnected')
  }

  useEffect(() => {
    return () => { wsRef.current?.close() }
  }, [])

  return { state, error, connect, disconnect }
}
