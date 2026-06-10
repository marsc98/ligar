export interface MonitorEvent {
  ts: number
  rms: number
  threshold?: number
  word: string | null
  probs: Record<string, number>
  var?: number
  rejected?: string
  kws_mode?: string
}

interface SessionMeta {
  ip: string
  startedAt: number
}

interface LogState {
  meta: SessionMeta
  events: MonitorEvent[]
  fileHandle: FileSystemFileHandle | null
  writer: FileSystemWritableFileStream | null
}

const state: LogState = {
  meta: { ip: '', startedAt: 0 },
  events: [],
  fileHandle: null,
  writer: null,
}

const supportsFileSystemAccess = typeof window !== 'undefined' && 'showSaveFilePicker' in window

function formatLine(ev: MonitorEvent): string {
  return JSON.stringify({
    ts: new Date(ev.ts).toISOString(),
    rms: ev.rms,
    threshold: ev.threshold ?? null,
    var: ev.var ?? null,
    word: ev.word,
    probs: ev.probs,
    rejected: ev.rejected ?? null,
    kws_mode: ev.kws_mode ?? null,
  }) + '\n'
}

export async function startSession(ip: string): Promise<void> {
  state.meta = { ip, startedAt: Date.now() }
  state.events = []

  if (supportsFileSystemAccess) {
    try {
      const ts = new Date().toISOString().replace(/[:.]/g, '-').slice(0, 19)
      // eslint-disable-next-line @typescript-eslint/no-explicit-any
      state.fileHandle = await (window as any).showSaveFilePicker({
        suggestedName: `monitor_${ts}.jsonl`,
        types: [{ description: 'JSON Lines', accept: { 'application/jsonl': ['.jsonl'] } }],
      })
      state.writer = await state.fileHandle!.createWritable()
      const header = JSON.stringify({ _session: true, ip, startedAt: new Date(state.meta.startedAt).toISOString() }) + '\n'
      await state.writer.write(header)
    } catch {
      // user cancelled picker — fall back to in-memory only
      state.fileHandle = null
      state.writer = null
    }
  }
}

export async function logEvent(ev: MonitorEvent): Promise<void> {
  state.events.push(ev)
  if (state.writer) {
    try {
      await state.writer.write(formatLine(ev))
    } catch {
      state.writer = null
    }
  }
}

export async function stopSession(): Promise<void> {
  if (state.writer) {
    await state.writer.close()
    state.writer = null
    state.fileHandle = null
  }
}

export function downloadSession(): void {
  if (state.events.length === 0) return

  const lines: string[] = [
    JSON.stringify({ _session: true, ip: state.meta.ip, startedAt: new Date(state.meta.startedAt).toISOString() }) + '\n',
    ...state.events.map(formatLine),
  ]

  const blob = new Blob(lines, { type: 'application/jsonl' })
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  const ts = new Date().toISOString().replace(/[:.]/g, '-').slice(0, 19)
  a.href = url
  a.download = `monitor_${ts}.jsonl`
  a.click()
  URL.revokeObjectURL(url)
}

export function sessionStats() {
  const detections = state.events.filter(e => e.word)
  const rejections = state.events.filter(e => e.rejected)
  return {
    total: state.events.length,
    detections: detections.length,
    rejections: rejections.length,
    isRecording: !!state.writer,
    hasEvents: state.events.length > 0,
  }
}
