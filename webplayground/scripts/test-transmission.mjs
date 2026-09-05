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
