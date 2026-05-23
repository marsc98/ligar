import { useRef, useState } from 'react'

type UseStreamOptions = {
  onWindow: (audio: Float32Array) => void
}

type UseStreamReturn = {
  streaming: boolean
  connectStream: (ip: string) => void
  disconnectStream: () => void
}

export function useStream({ onWindow }: UseStreamOptions): UseStreamReturn {
  const [streaming, setStreaming] = useState(false)
  const wsRef = useRef<WebSocket | null>(null)
  const bufferRef = useRef<number[]>([])

  const flush = () => {
    const buf = bufferRef.current
    if (buf.length === 0) return
    const float32 = new Float32Array(buf.length)
    for (let i = 0; i < buf.length; i++) {
      float32[i] = buf[i] / 32768
    }
    onWindow(float32)
    bufferRef.current = []
  }

  const connectStream = (ip: string) => {
    const ws = new WebSocket('ws://' + ip + '/stream')
    ws.binaryType = 'arraybuffer'
    wsRef.current = ws

    ws.onopen = () => setStreaming(true)

    ws.onmessage = (ev) => {
      if (!(ev.data instanceof ArrayBuffer)) return
      const chunk = new Int16Array(ev.data)
      for (let i = 0; i < chunk.length; i++) {
        bufferRef.current.push(chunk[i])
      }

      const buf = bufferRef.current
      const rmsStart = Math.max(0, buf.length - 512)
      let sum = 0
      for (let i = rmsStart; i < buf.length; i++) {
        sum += buf[i] * buf[i]
      }
      const rms = Math.sqrt(sum / (buf.length - rmsStart))

      if (buf.length >= 64000 || (buf.length >= 8000 && rms < 200)) {
        flush()
      }
    }

    ws.onclose = () => {
      bufferRef.current = []
      setStreaming(false)
    }

    ws.onerror = () => {
      bufferRef.current = []
      setStreaming(false)
    }
  }

  const disconnectStream = () => {
    wsRef.current?.close()
    wsRef.current = null
    bufferRef.current = []
    setStreaming(false)
  }

  return { streaming, connectStream, disconnectStream }
}
