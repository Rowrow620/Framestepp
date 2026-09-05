import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import './App.css'
import { examples } from './examples'
import type { ExampleId } from './examples'

type CompilerAction = 'check' | 'run' | 'disassemble'
type CompilerStatus = 'loading' | 'ready' | 'error'
type TerminalPhase = 'idle' | 'working' | 'success' | 'error'

interface PlaygroundResult {
  success: boolean
  output: string
  diagnostic: string
  executedInstructions: number
}

type WorkerResponse =
  | { type: 'ready' }
  | { type: 'load-error'; message: string }
  | {
      type: 'result'
      id: number
      action: CompilerAction
      result: PlaygroundResult
    }

interface TerminalState {
  action: CompilerAction | null
  phase: TerminalPhase
  text: string
  instructionCount: number
}

const terminalTitles: Record<CompilerAction, string> = {
  check: 'Type check',
  run: 'Program output',
  disassemble: 'Bytecode',
}

const makeIdleTerminal = (): TerminalState => ({
  action: null,
  phase: 'idle',
  text: 'Choose Check types, Run, or View bytecode.',
  instructionCount: 0,
})

function App() {
  const [selectedId, setSelectedId] = useState<ExampleId>('boss-fight')
  const selectedExample = useMemo(
    () => examples.find((example) => example.id === selectedId) ?? examples[0],
    [selectedId],
  )
  const [source, setSource] = useState(selectedExample.source)
  const [compilerStatus, setCompilerStatus] =
    useState<CompilerStatus>('loading')
  const [compilerMessage, setCompilerMessage] = useState(
    'Loading the C++ compiler',
  )
  const [compilerErrorDetail, setCompilerErrorDetail] = useState('')
  const [isWorking, setIsWorking] = useState(false)
  const [terminal, setTerminal] =
    useState<TerminalState>(makeIdleTerminal)
  const [announcement, setAnnouncement] = useState('')
  const workerRef = useRef<Worker | null>(null)
  const requestIdRef = useRef(0)
  const timeoutRef = useRef<number | null>(null)
  const gutterRef = useRef<HTMLDivElement | null>(null)

  const clearRequestTimer = useCallback(() => {
    if (timeoutRef.current !== null) {
      window.clearTimeout(timeoutRef.current)
      timeoutRef.current = null
    }
  }, [])

  const applyResult = useCallback(
    (action: CompilerAction, result: PlaygroundResult) => {
      setCompilerMessage('Compiler ready')

      let text = result.success ? result.output : result.diagnostic
      if (action === 'run') {
        if (result.success) {
          text = result.output || '(Program completed without output.)'
        } else if (result.output) {
          const separator = result.output.endsWith('\n') ? '\n' : '\n\n'
          text = `${result.output}${separator}[error]\n${result.diagnostic}`
        }
      }

      setTerminal({
        action,
        phase: result.success ? 'success' : 'error',
        text,
        instructionCount: result.executedInstructions,
      })

      const resultName =
        action === 'disassemble'
          ? 'Bytecode'
          : action === 'check'
            ? 'Type check'
            : 'Program run'
      const outcome = result.success ? 'completed' : 'completed with errors'
      setAnnouncement(`${resultName} ${outcome}.`)
    },
    [],
  )

  const startCompiler = useCallback(() => {
    clearRequestTimer()
    workerRef.current?.terminate()
    requestIdRef.current += 1
    setCompilerStatus('loading')
    setCompilerMessage('Loading the C++ compiler')
    setCompilerErrorDetail('')
    setIsWorking(false)

    const worker = new Worker(
      new URL('./framestepp.worker.ts', import.meta.url),
      { type: 'module' },
    )
    workerRef.current = worker

    worker.onmessage = (event: MessageEvent<WorkerResponse>) => {
      if (workerRef.current !== worker) {
        return
      }

      const response = event.data

      if (response.type === 'ready') {
        clearRequestTimer()
        setCompilerStatus('ready')
        setCompilerMessage('Compiler ready')
        setAnnouncement('FrameStep++ compiler ready.')
        return
      }

      if (response.type === 'load-error') {
        clearRequestTimer()
        setIsWorking(false)
        setCompilerStatus('error')
        setCompilerMessage('Compiler could not load')
        setCompilerErrorDetail(response.message)
        setTerminal((current) =>
          current.phase === 'working'
            ? {
                ...current,
                phase: 'error',
                text: response.message,
                instructionCount: 0,
              }
            : current,
        )
        setAnnouncement(response.message)
        return
      }

      if (response.id !== requestIdRef.current) {
        return
      }

      clearRequestTimer()
      setIsWorking(false)
      applyResult(response.action, response.result)
    }

    worker.onerror = () => {
      if (workerRef.current !== worker) {
        return
      }

      clearRequestTimer()
      setIsWorking(false)
      setCompilerStatus('error')
      setCompilerMessage('Compiler stopped unexpectedly')
      const message =
        'The compiler worker stopped unexpectedly. Retry to load it again.'
      setCompilerErrorDetail(message)
      setTerminal((current) =>
        current.phase === 'working'
          ? {
              ...current,
              phase: 'error',
              text: message,
              instructionCount: 0,
            }
          : current,
      )
      setAnnouncement('The compiler stopped unexpectedly. Retry to reload it.')
    }

    worker.postMessage({ type: 'initialize' })
    timeoutRef.current = window.setTimeout(() => {
      if (workerRef.current !== worker) {
        return
      }

      worker.terminate()
      workerRef.current = null
      timeoutRef.current = null
      setCompilerStatus('error')
      setCompilerMessage('Compiler load timed out')
      setCompilerErrorDetail(
        'The compiler took too long to load. Check your connection, then retry.',
      )
      setAnnouncement('The compiler took too long to load. Retry when ready.')
    }, 15000)
  }, [applyResult, clearRequestTimer])

  useEffect(() => {
    const startupTimer = window.setTimeout(startCompiler, 0)

    return () => {
      window.clearTimeout(startupTimer)
      clearRequestTimer()
      workerRef.current?.terminate()
    }
  }, [clearRequestTimer, startCompiler])

  const chooseExample = (id: ExampleId) => {
    const nextExample = examples.find((example) => example.id === id)
    if (!nextExample || isWorking) {
      return
    }

    requestIdRef.current += 1
    setSelectedId(id)
    setSource(nextExample.source)
    setTerminal(makeIdleTerminal())
    setAnnouncement(`${nextExample.name} example selected.`)
  }

  const resetExample = () => {
    setSource(selectedExample.source)
    setTerminal(makeIdleTerminal())
    setAnnouncement(`${selectedExample.name} restored.`)
  }

  const execute = (action: CompilerAction) => {
    if (compilerStatus !== 'ready' || isWorking || !workerRef.current) {
      return
    }

    const id = requestIdRef.current + 1
    const actionMessage =
      action === 'check'
        ? 'Checking types'
        : action === 'run'
          ? 'Running program'
          : 'Compiling bytecode'
    requestIdRef.current = id
    setIsWorking(true)
    setCompilerMessage(actionMessage)
    setTerminal({
      action,
      phase: 'working',
      text: `${actionMessage}...`,
      instructionCount: 0,
    })
    setAnnouncement(`${actionMessage}.`)
    workerRef.current.postMessage({
      type: 'execute',
      id,
      action,
      source,
      input: '',
    })

    timeoutRef.current = window.setTimeout(() => {
      if (requestIdRef.current !== id) {
        return
      }
      timeoutRef.current = null
      workerRef.current?.terminate()
      workerRef.current = null
      setIsWorking(false)
      setCompilerStatus('error')
      setCompilerMessage('Execution limit reached')
      setCompilerErrorDetail(
        'The browser stopped this run because it took too long. Retry the compiler and check the program for an endless loop.',
      )
      setTerminal({
        action,
        phase: 'error',
        text: 'The browser stopped this run because it took too long. Retry the compiler and check the program for an endless loop.',
        instructionCount: 0,
      })
      setAnnouncement('Execution stopped because it took too long.')
    }, 8000)
  }

  const statusClass = isWorking ? 'working' : compilerStatus
  const terminalTitle = terminal.action
    ? terminalTitles[terminal.action]
    : 'Terminal'
  const lineCount = Math.max(source.split('\n').length, 1)
  const isModified = source !== selectedExample.source

  return (
    <div className="app-shell">
      <header className="topbar">
        <img
          className="brand-image"
          src={`${import.meta.env.BASE_URL}framestepp.png`}
          alt=""
          aria-hidden="true"
          width="48"
          height="48"
        />
        <div className="brand-copy">
          <h1>FrameStep++ Playground</h1>
          <p>Explore a typed language and its bytecode virtual machine.</p>
        </div>
          <div className={`compiler-status ${statusClass}`}>
          <span className="status-dot" aria-hidden="true" />
          {compilerMessage}
        </div>
        <a
          className="repository-link"
          href="https://github.com/Rowrow620/Framestepp"
          target="_blank"
          rel="noreferrer"
        >
          View repository <span aria-hidden="true">↗</span>
        </a>
      </header>

      <main className="workspace">
        <aside className="panel examples-panel" aria-labelledby="examples-title">
          <div className="panel-heading">
            <div>
              <p className="eyebrow">Program library</p>
              <h2 id="examples-title">Examples</h2>
            </div>
            <span className="count-badge">{examples.length}</span>
          </div>
          <nav className="example-list" aria-label="FrameStep++ examples">
            {examples.map((example, index) => (
              <button
                className={`example-option ${selectedId === example.id ? 'selected' : ''}`}
                type="button"
                key={example.id}
                aria-pressed={selectedId === example.id}
                disabled={isWorking}
                onClick={() => chooseExample(example.id)}
              >
                <span className="example-index">0{index + 1}</span>
                <span>
                  <strong>{example.name}</strong>
                  <small>{example.category}</small>
                </span>
              </button>
            ))}
          </nav>
          <div className="pipeline-card">
            <p className="eyebrow">Execution pipeline</p>
            <ol>
              <li>Parse</li>
              <li>Check</li>
              <li>Compile</li>
              <li>Verify</li>
              <li>Run</li>
            </ol>
          </div>
        </aside>

        <section className="panel editor-panel" aria-labelledby="source-title">
          <div className="panel-heading editor-heading">
            <div>
              <p className="eyebrow">playground.frame</p>
              <h2 id="source-title">Source code</h2>
            </div>
            {isModified && (
              <div className="editor-heading-actions">
                <span className="modified-badge">Modified</span>
                <button
                  className="reset-button"
                  type="button"
                  disabled={isWorking}
                  onClick={resetExample}
                >
                  Reset example
                </button>
              </div>
            )}
          </div>
          <div className="code-editor">
            <div className="line-numbers" ref={gutterRef} aria-hidden="true">
              {Array.from({ length: lineCount }, (_, index) => (
                <span key={index}>{index + 1}</span>
              ))}
            </div>
            <textarea
              value={source}
              onChange={(event) => {
                setSource(event.target.value)
                setTerminal(makeIdleTerminal())
              }}
              onScroll={(event) => {
                if (gutterRef.current) {
                  gutterRef.current.scrollTop = event.currentTarget.scrollTop
                }
              }}
              aria-label="FrameStep++ source code"
              disabled={isWorking}
              spellCheck={false}
              wrap="off"
            />
          </div>
          <div className="action-bar" aria-label="Compiler actions">
            <button
              className="action-button secondary"
              type="button"
              disabled={compilerStatus !== 'ready' || isWorking}
              onClick={() => execute('check')}
            >
              Check types
            </button>
            <button
              className="action-button primary"
              type="button"
              disabled={compilerStatus !== 'ready' || isWorking}
              onClick={() => execute('run')}
            >
              <span aria-hidden="true">▶</span> Run
            </button>
            <button
              className="action-button secondary"
              type="button"
              disabled={compilerStatus !== 'ready' || isWorking}
              onClick={() => execute('disassemble')}
            >
              View bytecode
            </button>
          </div>
          {compilerStatus === 'error' && (
            <div className="compiler-error" role="alert">
              <span>
                {compilerErrorDetail || 'The browser compiler is unavailable.'}
              </span>
              <button type="button" onClick={startCompiler}>
                Retry
              </button>
            </div>
          )}
        </section>

        <section
          className="panel results-panel"
          aria-labelledby="results-title"
          aria-busy={terminal.phase === 'working'}
        >
          <div className="result-toolbar">
            <div className="terminal-heading">
              <p className="eyebrow">Compiler terminal</p>
              <h2 id="results-title">{terminalTitle}</h2>
            </div>
            {terminal.instructionCount > 0 && (
              <span className="instruction-count">
                {terminal.instructionCount.toLocaleString()} instructions
              </span>
            )}
          </div>
          <div
            className={`result-content ${terminal.phase !== 'idle' ? 'has-result' : ''} ${terminal.phase}`}
            tabIndex={0}
          >
            <pre>{terminal.text}</pre>
          </div>
        </section>

        <aside
          className="panel explanation-panel"
          aria-labelledby="explanation-title"
        >
          <div className="panel-heading">
            <div>
              <p className="eyebrow">Selected program</p>
              <h2 id="explanation-title">{selectedExample.name}</h2>
            </div>
          </div>
          <p className="example-summary">{selectedExample.summary}</p>
          <div className="concept-block">
            <h3>What it demonstrates</h3>
            <ul className="concept-list">
              {selectedExample.concepts.map((concept) => (
                <li key={concept}>{concept}</li>
              ))}
            </ul>
          </div>
          <div className="expected-block">
            <h3>Expected output</h3>
            <pre>{selectedExample.expectedOutput}</pre>
          </div>
          <p className="tip-copy">
            Change the source, run it again, then reset whenever you want the
            original example back.
          </p>
        </aside>
      </main>

      <div className="visually-hidden" aria-live="polite">
        {announcement}
      </div>
    </div>
  )
}

export default App
