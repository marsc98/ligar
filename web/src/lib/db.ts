import type { Recording } from '../types'

function idbRequest<T>(req: IDBRequest<T>): Promise<T> {
  return new Promise((resolve, reject) => {
    req.onsuccess = () => resolve(req.result)
    req.onerror = () => reject(req.error)
  })
}

export function openDB(): Promise<IDBDatabase> {
  return new Promise((resolve, reject) => {
    const req = indexedDB.open('poc-microfone', 1)
    req.onupgradeneeded = () => {
      const db = req.result
      const store = db.createObjectStore('recordings', { keyPath: 'id' })
      store.createIndex('by-timestamp', 'timestamp')
    }
    req.onsuccess = () => resolve(req.result)
    req.onerror = () => reject(req.error)
  })
}

export function saveRecording(db: IDBDatabase, rec: Recording): Promise<void> {
  const tx = db.transaction('recordings', 'readwrite')
  return idbRequest(tx.objectStore('recordings').put(rec)).then(() => undefined)
}

export function getAllRecordings(db: IDBDatabase): Promise<Recording[]> {
  const tx = db.transaction('recordings', 'readonly')
  return idbRequest<Recording[]>(tx.objectStore('recordings').getAll()).then(
    (recs) => recs.sort((a, b) => b.timestamp - a.timestamp)
  )
}

export function deleteRecording(db: IDBDatabase, id: string): Promise<void> {
  const tx = db.transaction('recordings', 'readwrite')
  return idbRequest(tx.objectStore('recordings').delete(id))
}

export function updateTranscription(db: IDBDatabase, id: string, text: string): Promise<void> {
  return new Promise((resolve, reject) => {
    const tx = db.transaction('recordings', 'readwrite')
    const store = tx.objectStore('recordings')
    const req = store.get(id)
    req.onsuccess = () => {
      const rec = req.result
      if (rec) store.put({ ...rec, transcription: text })
      resolve()
    }
    req.onerror = () => reject(req.error)
  })
}
