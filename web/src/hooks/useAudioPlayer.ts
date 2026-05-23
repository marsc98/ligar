import { useRef, useState, useEffect } from 'react'
import type { Recording } from '../types'

export function useAudioPlayer(): {
  currentId: string | null
  audioRef: React.RefObject<HTMLAudioElement | null>
  play(recording: Recording): void
  stop(): void
} {
  const [currentId, setCurrentId] = useState<string | null>(null)
  const audioRef = useRef<HTMLAudioElement>(null)
  const currentUrl = useRef<string | null>(null)

  const stop = () => {
    const audio = audioRef.current
    if (audio) {
      audio.pause()
      audio.src = ''
    }
    if (currentUrl.current) {
      URL.revokeObjectURL(currentUrl.current)
      currentUrl.current = null
    }
    setCurrentId(null)
  }

  const play = (recording: Recording) => {
    if (currentUrl.current) {
      URL.revokeObjectURL(currentUrl.current)
      currentUrl.current = null
    }
    const url = URL.createObjectURL(recording.blob)
    currentUrl.current = url
    const audio = audioRef.current
    if (audio) {
      audio.src = url
      audio.play()
    }
    setCurrentId(recording.id)
  }

  useEffect(() => {
    return () => {
      if (currentUrl.current) {
        URL.revokeObjectURL(currentUrl.current)
      }
    }
  }, [])

  return { currentId, audioRef, play, stop }
}
