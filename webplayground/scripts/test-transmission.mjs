import assert from 'node:assert/strict'
import { readFile } from 'node:fs/promises'
import ts from 'typescript'

const source = await readFile(
  new URL('../src/transmission.ts', import.meta.url),
  'utf8',
)
const { outputText } = ts.transpileModule(source, {
  compilerOptions: {
    module: ts.ModuleKind.ESNext,
    target: ts.ScriptTarget.ES2023,
  },
})
const transmissionModuleUrl = `data:text/javascript;base64,${Buffer.from(outputText).toString('base64')}`
const {
  MAX_ANIMATED_CHARACTERS,
  MAX_TRANSMISSION_DURATION_MS,
  TRANSMISSION_CHARACTER_MS,
  TRANSMISSION_LINE_BREAK_MS,
  createTransmissionPlan,
  shouldAnimateTransmission,
} = await import(transmissionModuleUrl)

const shortPlan = createTransmissionPlan('AB\nC')
assert.deepEqual(shortPlan.characters, ['A', 'B', '\n', 'C'])
assert.deepEqual(shortPlan.revealAt, [20, 40, 160, 180])
assert.equal(shortPlan.playbackRate, 1)
assert.equal(shortPlan.revealAt[1], TRANSMISSION_CHARACTER_MS * 2)
assert.equal(
  shortPlan.revealAt[2] - shortPlan.revealAt[1],
  TRANSMISSION_LINE_BREAK_MS,
)

const defaultBlankLinePlan = createTransmissionPlan('A\n\nB')
assert.deepEqual(defaultBlankLinePlan.revealAt, [20, 140, 260, 280])

const smoothBlankLinePlan = createTransmissionPlan('A\n\nB', {
  batchConsecutiveLineBreaks: true,
})
assert.equal(smoothBlankLinePlan.characters.join(''), 'A\n\nB')
assert.deepEqual(smoothBlankLinePlan.revealAt, [20, 280, 280, 280])

const smoothSingleLineBreakPlan = createTransmissionPlan('A\nB', {
  batchConsecutiveLineBreaks: true,
})
assert.deepEqual(smoothSingleLineBreakPlan.revealAt, [20, 140, 160])

const trailingBlankLinePlan = createTransmissionPlan('A\n\n', {
  batchConsecutiveLineBreaks: true,
})
assert.deepEqual(trailingBlankLinePlan.revealAt, [20, 140, 260])

const connectionPlan = createTransmissionPlan(
  'ARE YOU THERE?\nARE WE CONNECTED?\n\nEXCELLENT.\nTRULY EXCELLENT.\nNOW.\n\nWE MAY BEGIN.\n',
  {
    batchConsecutiveLineBreaks: true,
    characterMs: 40,
    lastLineCharacterMs: 100,
    lineBreakMs: 240,
    maxDurationMs: 7000,
    pauseBeforeLine: {
      line: 4,
      durationMs: 300,
    },
    pauseBeforeLastLineMs: 800,
  },
)
const connectionText = connectionPlan.characters.join('')
const connectedEnd =
  connectionText.indexOf('ARE WE CONNECTED?') +
  'ARE WE CONNECTED?'.length -
  1
const excellentStart = connectionText.indexOf('EXCELLENT.')
const nowEnd = connectionText.indexOf('NOW.') + 'NOW.'.length - 1
const finalLineStart = connectionText.lastIndexOf('WE MAY BEGIN.')
const firstLineBreak = connectionPlan.characters.indexOf('\n')
assert.equal(connectionPlan.playbackRate, 1)
assert.equal(connectionPlan.revealAt[0], 40)
assert.equal(
  connectionPlan.revealAt[firstLineBreak] -
    connectionPlan.revealAt[firstLineBreak - 1],
  240,
)
assert.equal(
  connectionPlan.revealAt[excellentStart] -
    connectionPlan.revealAt[connectedEnd],
  820,
)
assert.equal(
  connectionPlan.revealAt[excellentStart - 2],
  connectionPlan.revealAt[excellentStart],
)
assert.equal(
  connectionPlan.revealAt[excellentStart - 1],
  connectionPlan.revealAt[excellentStart],
)
assert.equal(
  connectionPlan.revealAt[finalLineStart] -
    connectionPlan.revealAt[nowEnd],
  1380,
)
assert.equal(
  connectionPlan.revealAt[finalLineStart - 2],
  connectionPlan.revealAt[finalLineStart],
)
assert.equal(
  connectionPlan.revealAt[finalLineStart - 1],
  connectionPlan.revealAt[finalLineStart],
)
assert.equal(
  connectionPlan.revealAt[finalLineStart + 1] -
    connectionPlan.revealAt[finalLineStart],
  100,
)
assert.equal(connectionPlan.revealAt.at(-1), 6760)

const longPlan = createTransmissionPlan('x'.repeat(1000) + '\n'.repeat(30))
const displayedDuration =
  longPlan.revealAt.at(-1) / longPlan.playbackRate
assert.ok(longPlan.playbackRate > 1)
assert.ok(Number.isFinite(longPlan.playbackRate))
assert.ok(
  Math.abs(displayedDuration - MAX_TRANSMISSION_DURATION_MS) < 0.001,
)

assert.equal(shouldAnimateTransmission(0, false), false)
assert.equal(shouldAnimateTransmission(80, false), true)
assert.equal(shouldAnimateTransmission(80, true), false)
assert.equal(shouldAnimateTransmission(MAX_ANIMATED_CHARACTERS, false), true)
assert.equal(
  shouldAnimateTransmission(MAX_ANIMATED_CHARACTERS + 1, false),
  false,
)

console.log('Transmission timing contract passed')
