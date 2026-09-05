export const TRANSMISSION_CHARACTER_MS = 20
export const TRANSMISSION_LINE_BREAK_MS = 120
export const MAX_TRANSMISSION_DURATION_MS = 3500
export const MAX_ANIMATED_CHARACTERS = 8000

export interface TransmissionPlan {
  characters: string[]
  revealAt: number[]
  playbackRate: number
}

export interface TransmissionOptions {
  characterMs?: number
  lineBreakMs?: number
  maxDurationMs?: number
  pauseBeforeLastLineMs?: number
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

const findLastLineStart = (characters: string[]) => {
  let lastContentIndex = characters.length - 1

  while (
    lastContentIndex >= 0 &&
    (characters[lastContentIndex] === '\n' ||
      characters[lastContentIndex] === '\r')
  ) {
    lastContentIndex -= 1
  }

  for (let index = lastContentIndex; index >= 0; index -= 1) {
    if (characters[index] === '\n') {
      return index + 1
    }
  }

  return 0
}

export const createTransmissionPlan = (
  text: string,
  options: TransmissionOptions = {},
): TransmissionPlan => {
  const characters = splitCharacters(text)
  const revealAt: number[] = []
  const characterMs = options.characterMs ?? TRANSMISSION_CHARACTER_MS
  const lineBreakMs = options.lineBreakMs ?? TRANSMISSION_LINE_BREAK_MS
  const maxDurationMs =
    options.maxDurationMs ?? MAX_TRANSMISSION_DURATION_MS
  const pauseBeforeLastLineMs = options.pauseBeforeLastLineMs ?? 0
  const lastLineStart = findLastLineStart(characters)
  let duration = 0

  for (const [index, character] of characters.entries()) {
    if (index === lastLineStart && lastLineStart > 0) {
      duration += pauseBeforeLastLineMs
    }

    duration += character === '\n' ? lineBreakMs : characterMs
    revealAt.push(duration)
  }

  return {
    characters,
    revealAt,
    playbackRate: Math.max(1, duration / maxDurationMs),
  }
}
