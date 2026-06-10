import { useState, useEffect, useRef } from 'react'
import { openDB, saveRecording, getAllRecordings, deleteRecording as dbDeleteRecording, updateTranscription as dbUpdateTranscription } from '../lib/db'
import type { Recording } from '../types'

export function useRecordings(): {
  recordings: Recording[]
  loading: boolean
  dbError: string | null
  addRecording(blob: Blob, duration: number, collection?: { word: string; sessionId: string }): Promise<string>
  deleteRecording(id: string): Promise<void>
  updateTranscription(id: string, text: string): Promise<void>
} {
  const [recordings, setRecordings] = useState<Recording[]>([])
  const [loading, setLoading] = useState(true)
  const [dbError, setDbError] = useState<string | null>(null)
  const dbRef = useRef<IDBDatabase | null>(null)

  useEffect(() => {
    if (!window.indexedDB) {
      setDbError('Seu browser não suporta armazenamento local')
      setLoading(false)
      return
    }
    openDB()
      .then((db) => {
        dbRef.current = db
        return getAllRecordings(db)
      })
      .then((recs) => {
        setRecordings(recs)
        setLoading(false)
      })
      .catch(() => {
        setDbError('Armazenamento indisponível')
        setLoading(false)
      })
  }, [])

  const addRecording = async (blob: Blob, duration: number, collection?: { word: string; sessionId: string }): Promise<string> => {
    const db = dbRef.current
    if (!db) return ''
    const id = crypto.randomUUID()
    const name = 'rec-' + new Date().toISOString().replace(/[:.]/g, '-').slice(0, 19)
    const rec: Recording = {
      id,
      name,
      timestamp: Date.now(),
      duration,
      size: blob.size,
      blob,
      ...(collection ? { collection } : {}),
    }
    try {
      await saveRecording(db, rec)
      setRecordings((prev) => [rec, ...prev])
    } catch (e) {
      if (e instanceof DOMException && e.name === 'QuotaExceededError') {
        setDbError('Armazenamento cheio — exclua gravações antigas')
      } else {
        setDbError('Erro ao salvar gravação')
      }
    }
    return id
  }

  const updateTranscription = async (id: string, text: string) => {
    const db = dbRef.current
    if (!db) return
    try {
      await dbUpdateTranscription(db, id, text)
      setRecordings(prev =>
        prev.map(r => r.id === id ? { ...r, transcription: text } : r)
      )
    } catch {
      // transcrição perdida, gravação intacta
    }
  }

  const deleteRecording = async (id: string) => {
    const db = dbRef.current
    if (!db) return
    try {
      await dbDeleteRecording(db, id)
      setRecordings((prev) => prev.filter((r) => r.id !== id))
    } catch {
      setDbError('Erro ao excluir gravação')
    }
  }

  return { recordings, loading, dbError, addRecording, deleteRecording, updateTranscription }
}
