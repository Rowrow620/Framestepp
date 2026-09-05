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
  batchConsecutiveLineBreaks?: boolean
  characterMs?: number
  lastLineCharacterMs?: number
  lineBreakMs?: number
  maxDurationMs?: number
  pauseBeforeLine?: {
    line: number
    durationMs: number
  }
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

export const removeFinalLineBreak = (text: string) => {
  if (text.endsWith('\r\n')) {
    return text.slice(0, -2)
  }

  return text.endsWith('\n') || text.endsWith('\r') ? text.slice(0, -1) : text
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

const findLineStart = (characters: string[], targetLine: number) => {
  if (targetLine <= 1) {
    return 0
  }

  let currentLine = 1
  for (const [index, character] of characters.entries()) {
    if (character === '\n') {
      currentLine += 1
      if (currentLine === targetLine) {
        return index + 1
      }
    }
  }

  return -1
}

const batchConsecutiveLineBreaks = (
  characters: string[],
  revealAt: number[],
) => {
  let index = 0

  while (index < characters.length) {
    if (characters[index] !== '\n' || characters[index + 1] !== '\n') {
      index += 1
      continue
    }

    let nextCharacter = index + 2
    while (
      nextCharacter < characters.length &&
      characters[nextCharacter] === '\n'
    ) {
      nextCharacter += 1
    }

    if (nextCharacter < characters.length) {
      const revealTime = revealAt[nextCharacter]
      for (
        let lineBreak = index;
        lineBreak < nextCharacter;
        lineBreak += 1
      ) {
        revealAt[lineBreak] = revealTime
      }
    }

    index = nextCharacter
  }
}

export const createTransmissionPlan = (
  text: string,
  options: TransmissionOptions = {},
): TransmissionPlan => {
  const characters = splitCharacters(text)
  const revealAt: number[] = []
  const characterMs = options.characterMs ?? TRANSMISSION_CHARACTER_MS
  const lastLineCharacterMs = options.lastLineCharacterMs ?? characterMs
  const lineBreakMs = options.lineBreakMs ?? TRANSMISSION_LINE_BREAK_MS
  const maxDurationMs =
    options.maxDurationMs ?? MAX_TRANSMISSION_DURATION_MS
  const pauseBeforeLastLineMs = options.pauseBeforeLastLineMs ?? 0
  const lastLineStart = findLastLineStart(characters)
  const pauseBeforeLineStart = options.pauseBeforeLine
    ? findLineStart(characters, options.pauseBeforeLine.line)
    : -1
  let duration = 0

  for (const [index, character] of characters.entries()) {
    if (index === pauseBeforeLineStart && pauseBeforeLineStart > 0) {
      duration += options.pauseBeforeLine?.durationMs ?? 0
    }

    if (index === lastLineStart && lastLineStart > 0) {
      duration += pauseBeforeLastLineMs
    }

    duration +=
      character === '\n'
        ? lineBreakMs
        : index >= lastLineStart
          ? lastLineCharacterMs
          : characterMs
    revealAt.push(duration)
  }

  if (options.batchConsecutiveLineBreaks) {
    batchConsecutiveLineBreaks(characters, revealAt)
  }

  return {
    characters,
    revealAt,
    playbackRate: Math.max(1, duration / maxDurationMs),
  }
}
