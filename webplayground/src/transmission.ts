export const TRANSMISSION_CHARACTER_MS = 20
export const TRANSMISSION_LINE_BREAK_MS = 120
export const MAX_TRANSMISSION_DURATION_MS = 3500
export const MAX_ANIMATED_CHARACTERS = 8000

export interface TransmissionPlan {
  characters: string[]
  revealAt: number[]
  playbackRate: number
}

const splitCharacters = (text: string) => {
  if (typeof Intl.Segmenter === 'function') {
    const segmenter = new Intl.Segmenter(undefined, {
      granularity: 'grapheme',
    })
    return Array.from(segmenter.segment(text), ({ segment }) => segment)
  }

  return Array.from(text)
}

export const shouldAnimateTransmission = (
  textLength: number,
  prefersReducedMotion: boolean,
) =>
  !prefersReducedMotion &&
  textLength > 0 &&
  textLength <= MAX_ANIMATED_CHARACTERS

export const createTransmissionPlan = (text: string): TransmissionPlan => {
  const characters = splitCharacters(text)
  const revealAt: number[] = []
  let duration = 0

  for (const character of characters) {
    duration +=
      character === '\n'
        ? TRANSMISSION_LINE_BREAK_MS
        : TRANSMISSION_CHARACTER_MS
    revealAt.push(duration)
  }

  return {
    characters,
    revealAt,
    playbackRate: Math.max(1, duration / MAX_TRANSMISSION_DURATION_MS),
  }
}
