import { useState } from 'react'

interface UseLightControlReturn {
  selectedColor: string | null
  intensity: number
  setSelectedColor: (color: string) => void
  setIntensity: (n: number) => void
  sendCommand: (color: string, intensity: number) => void
}

export function useLightControl(ip: string): UseLightControlReturn {
  const [selectedColor, setSelectedColor] = useState<string | null>(null)
  const [intensity, setIntensity] = useState(100)

  const sendCommand = (color: string, intensity: number) => {
    fetch(`http://${ip}/led?color=${color}&intensity=${intensity}`).catch(() => {})
  }

  return { selectedColor, intensity, setSelectedColor, setIntensity, sendCommand }
}
