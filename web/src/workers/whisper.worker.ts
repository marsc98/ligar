import { pipeline, env } from '@huggingface/transformers'
import type { AutomaticSpeechRecognitionPipeline, ProgressCallback } from '@huggingface/transformers'

env.allowLocalModels = false

class WhisperPipeline {
  static instance: Promise<AutomaticSpeechRecognitionPipeline> | null = null

  static getInstance(progress_callback: ProgressCallback) {
    this.instance ??= pipeline(
      'automatic-speech-recognition',
      'onnx-community/whisper-tiny',
      { progress_callback, dtype: 'fp32' }
    ) as Promise<AutomaticSpeechRecognitionPipeline>
    return this.instance
  }
}

self.addEventListener('message', async (event: MessageEvent) => {
  const { id, audio, language } = event.data as {
    id: string | null
    audio: Float32Array
    language: string
  }

  try {
    const transcriber = await WhisperPipeline.getInstance((p: unknown) => {
      self.postMessage(p)
    })

    const result = await transcriber(audio, { language, task: 'transcribe' })

    const text = Array.isArray(result) ? result[0].text : (result as { text: string }).text
    self.postMessage({ status: 'complete', id, text: text.trim() })
  } catch (e) {
    self.postMessage({ status: 'error', id, error: String(e) })
  }
})
