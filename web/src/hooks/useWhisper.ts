import { useRef, useState, useEffect, useCallback } from 'react'

type UseWhisperOptions = {
  language: string
  onComplete: (id: string, text: string) => void
  onLive: (text: string) => void
}

type UseWhisperReturn = {
  modelStatus: 'idle' | 'loading' | 'ready'
  transcribingIds: Set<string>
  transcribe: (id: string, blob: Blob) => void
  transcribeLive: (audio: Float32Array) => void
}

type WorkerMessage =
  | { status: 'initiate' | 'progress' | 'done' | 'ready' }
  | { status: 'complete'; id: string; text: string }
  | { status: 'error'; id: string | null; error: string }

export function useWhisper({ language, onComplete, onLive }: UseWhisperOptions): UseWhisperReturn {
  const [modelStatus, setModelStatus] = useState<'idle' | 'loading' | 'ready'>('idle')
  const [transcribingIds, setTranscribingIds] = useState<Set<string>>(new Set())
  const workerRef = useRef<Worker | null>(null)
  const busyRef = useRef(false)
  const queueRef = useRef<Array<{ id: string | null; audio: Float32Array }>>([])
  const languageRef = useRef(language)
  const onCompleteRef = useRef(onComplete)
  const onLiveRef = useRef(onLive)

  useEffect(() => { languageRef.current = language }, [language])
  useEffect(() => { onCompleteRef.current = onComplete }, [onComplete])
  useEffect(() => { onLiveRef.current = onLive }, [onLive])

  useEffect(() => {
    const worker = new Worker(
      new URL('../workers/whisper.worker.ts', import.meta.url),
      { type: 'module' }
    )
    workerRef.current = worker

    worker.onmessage = (ev: MessageEvent<WorkerMessage>) => {
      const msg = ev.data
      if (msg.status === 'initiate' || msg.status === 'progress') {
        setModelStatus('loading')
      } else if (msg.status === 'done' || msg.status === 'ready') {
        setModelStatus('ready')
      } else if (msg.status === 'complete') {
        busyRef.current = false
        if (msg.id) {
          onCompleteRef.current(msg.id, msg.text)
          setTranscribingIds(prev => {
            const next = new Set(prev)
            next.delete(msg.id)
            return next
          })
        } else {
          onLiveRef.current(msg.text)
        }
        drainQueue()
      } else if (msg.status === 'error') {
        busyRef.current = false
        if ('id' in msg && msg.id) {
          setTranscribingIds(prev => {
            const next = new Set(prev)
            next.delete(msg.id as string)
            return next
          })
        }
        drainQueue()
      }
    }

    return () => worker.terminate()
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [])

  const drainQueue = () => {
    if (busyRef.current || queueRef.current.length === 0) return
    const job = queueRef.current.shift()!
    busyRef.current = true
    workerRef.current?.postMessage({ ...job, language: languageRef.current })
  }

  const transcribe = useCallback((id: string, blob: Blob) => {
    setTranscribingIds(prev => new Set([...prev, id]))
    const ctx = new AudioContext({ sampleRate: 16000 })
    blob.arrayBuffer().then(buf => ctx.decodeAudioData(buf)).then(decoded => {
      const float32 = decoded.getChannelData(0)
      queueRef.current.push({ id, audio: float32 })
      drainQueue()
    }).catch(() => {
      setTranscribingIds(prev => {
        const next = new Set(prev)
        next.delete(id)
        return next
      })
    })
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [])

  const transcribeLive = useCallback((audio: Float32Array) => {
    queueRef.current.push({ id: null, audio })
    drainQueue()
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [])

  return { modelStatus, transcribingIds, transcribe, transcribeLive }
}
