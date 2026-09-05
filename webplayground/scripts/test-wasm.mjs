import assert from 'node:assert/strict'
import { readFile } from 'node:fs/promises'
import { fileURLToPath, pathToFileURL } from 'node:url'

const wasmDirectory = new URL('../public/wasm/', import.meta.url)
const modulePath = new URL('framestepp.js', wasmDirectory)
const createFrameStepModule = (await import(pathToFileURL(fileURLToPath(modulePath)).href))
  .default

const compiler = await createFrameStepModule({
  locateFile: (path) => fileURLToPath(new URL(path, wasmDirectory)),
})

const examples = [
  {
    name: 'connection',
    source: new URL('../../examples/connection.frame', import.meta.url),
    output:
      'ARE YOU THERE?\nARE WE CONNECTED?\nEXCELLENT.\nTRULY EXCELLENT.\nNOW.\nWE MAY BEGIN.\n',
    bytecodeMarker: 'function 0000 transmit',
  },
  {
    name: 'fibonacci',
    source: new URL('../../examples/fibonacci.frame', import.meta.url),
    output: '55\n',
    bytecodeMarker: 'function 0000 fibonacci',
  },
  {
    name: 'boss fight',
    source: new URL('../../examples/boss_fight.frame', import.meta.url),
    output: 'Critical hit!\n70\n50\n',
    bytecodeMarker: 'mut boss_health',
  },
]

for (const example of examples) {
  const source = await readFile(example.source, 'utf8')

  const check = compiler.check(source)
  assert.equal(check.success, true, `${example.name} should type-check`)
  assert.equal(check.output, 'type check passed\n')

  const run = compiler.run(source, '')
  assert.equal(run.success, true, `${example.name} should run`)
  assert.equal(run.output, example.output)
  assert.ok(run.executedInstructions > 0)

  const disassembly = compiler.disassemble(source)
  assert.equal(disassembly.success, true, `${example.name} should compile`)
  assert.match(disassembly.output, new RegExp(example.bytecodeMarker))
  assert.ok(disassembly.executedInstructions > 0)
}

const typeError = compiler.check('frameout(1 + false);')
assert.equal(typeError.success, false)
assert.match(typeError.diagnostic, /operator `\+`/)

const runtimeError = compiler.run('frameout("before"); frameout(10 / 0);', '')
assert.equal(runtimeError.success, false)
assert.equal(runtimeError.output, 'before\n')
assert.match(runtimeError.diagnostic, /division by zero/)

console.log('FrameStep++ WebAssembly contract: 3 examples and 2 error cases passed')
