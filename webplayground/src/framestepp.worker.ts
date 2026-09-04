/// <reference lib="webworker" />

type CompilerAction = 'check' | 'run' | 'disassemble'

interface PlaygroundResult {
  success: boolean
  output: string
  diagnostic: string
  executedInstructions: number
}

interface FrameStepModule {
  check(source: string): PlaygroundResult
  run(source: string, input: string): PlaygroundResult
  disassemble(source: string): PlaygroundResult
}

type FrameStepModuleFactory = (options: {
  locateFile: (path: string) => string
}) => Promise<FrameStepModule>

type WorkerRequest =
  | { type: 'initialize' }
  | {
      type: 'execute'
      id: number
      action: CompilerAction
      source: string
      input: string
    }

let compilerPromise: Promise<FrameStepModule> | null = null

const loadCompiler = async () => {
  if (!compilerPromise) {
    const basePath = import.meta.env.BASE_URL
    const moduleUrl = new URL(
      `${basePath}wasm/framestepp.js`,
      self.location.origin,
    ).href
    const wasmDirectory = new URL(`${basePath}wasm/`, self.location.origin).href

    compilerPromise = import(/* @vite-ignore */ moduleUrl).then(
      async (module: { default: FrameStepModuleFactory }) =>
        module.default({
          locateFile: (path) => `${wasmDirectory}${path}`,
        }),
    )
  }

  return compilerPromise
}

self.onmessage = async (event: MessageEvent<WorkerRequest>) => {
  const request = event.data

  try {
    const compiler = await loadCompiler()

    if (request.type === 'initialize') {
      self.postMessage({ type: 'ready' })
      return
    }

    const result =
      request.action === 'check'
        ? compiler.check(request.source)
        : request.action === 'run'
          ? compiler.run(request.source, request.input)
          : compiler.disassemble(request.source)

    self.postMessage({
      type: 'result',
      id: request.id,
      action: request.action,
      result,
    })
  } catch (error) {
    compilerPromise = null
    self.postMessage({
      type: 'load-error',
      message:
        error instanceof Error
          ? error.message
          : 'The FrameStep++ compiler could not be loaded.',
    })
  }
}

export {}
