import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import type { KeyboardEvent } from 'react'
import './App.css'
import { examples } from './examples'
import type { ExampleId } from './examples'

type CompilerAction = 'check' | 'run' | 'disassemble'
type CompilerStatus = 'loading' | 'ready' | 'error'
type ResultTab = 'output' | 'diagnostics' | 'bytecode'

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

const tabs: ResultTab[] = ['output', 'diagnostics', 'bytecode']

const tabLabels: Record<ResultTab, string> = {
  output: 'Output',
  diagnostics: 'Diagnostics',
  bytecode: 'Bytecode',
}

const emptyPanelCopy: Record<ResultTab, string> = {
  output: 'Run the selected program to see its output.',
  diagnostics: 'Check the program to see type and syntax diagnostics.',
  bytecode: 'Compile the program to inspect its verified bytecode.',
}

const makeEmptyPanels = (): Record<ResultTab, string> => ({
  output: '',
  diagnostics: '',
  bytecode: '',
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
  const [activeTab, setActiveTab] = useState<ResultTab>('output')
  const [panels, setPanels] =
    useState<Record<ResultTab, string>>(makeEmptyPanels)
  const [instructionCount, setInstructionCount] = useState(0)
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
      setInstructionCount(result.executedInstructions)
      setCompilerMessage('Compiler ready')

      if (action === 'check') {
        setPanels((current) => ({
          ...current,
          diagnostics: result.success ? result.output : result.diagnostic,
        }))
        setActiveTab('diagnostics')
      } else if (action === 'run') {
        setPanels((current) => ({
          ...current,
          output:
            result.output ||
            (result.success ? '(Program completed without output.)' : ''),
          diagnostics: result.diagnostic,
        }))
        setActiveTab(result.success ? 'output' : 'diagnostics')
      } else {
        setPanels((current) => ({
          ...current,
          bytecode: result.output,
          diagnostics: result.diagnostic,
        }))
        setActiveTab(result.success ? 'bytecode' : 'diagnostics')
      }

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
      clearRequestTimer()
      setIsWorking(false)
      setCompilerStatus('error')
      setCompilerMessage('Compiler stopped unexpectedly')
      setCompilerErrorDetail(
        'The compiler worker stopped unexpectedly. Retry to load it again.',
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
    setPanels(makeEmptyPanels())
    setInstructionCount(0)
    setActiveTab('output')
    setAnnouncement(`${nextExample.name} example selected.`)
  }

  const resetExample = () => {
    setSource(selectedExample.source)
    setPanels(makeEmptyPanels())
    setInstructionCount(0)
    setActiveTab('output')
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
      setPanels((current) => ({
        ...current,
        diagnostics:
          'The browser stopped this run because it took too long. Retry the compiler and check the program for an endless loop.',
      }))
      setActiveTab('diagnostics')
      setAnnouncement('Execution stopped because it took too long.')
    }, 8000)
  }

  const moveResultFocus = (
    event: KeyboardEvent<HTMLButtonElement>,
    index: number,
  ) => {
    let nextIndex = index
    if (event.key === 'ArrowRight') {
      nextIndex = (index + 1) % tabs.length
    } else if (event.key === 'ArrowLeft') {
      nextIndex = (index - 1 + tabs.length) % tabs.length
    } else if (event.key === 'Home') {
      nextIndex = 0
    } else if (event.key === 'End') {
      nextIndex = tabs.length - 1
    } else {
      return
    }

    event.preventDefault()
    const nextTab = tabs[nextIndex]
    setActiveTab(nextTab)
    document.getElementById(`result-tab-${nextTab}`)?.focus()
  }

  const statusClass = isWorking ? 'working' : compilerStatus
  const displayedResult = panels[activeTab] || emptyPanelCopy[activeTab]
  const lineCount = Math.max(source.split('\n').length, 1)
  const isModified = source !== selectedExample.source

  return (
    <div className="app-shell">
      <header className="topbar">
        <div className="brand-mark" aria-hidden="true">
          FS<span>++</span>
        </div>
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
            {isModified && <span className="modified-badge">Modified</span>}
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
                setPanels(makeEmptyPanels())
                setInstructionCount(0)
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
            <button
              className="reset-button"
              type="button"
              disabled={!isModified || isWorking}
              onClick={resetExample}
            >
              Reset example
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

        <section className="panel results-panel" aria-labelledby="results-title">
          <div className="result-toolbar">
            <div
              className="result-tabs"
              role="tablist"
              aria-label="Compiler results"
            >
              {tabs.map((tab, index) => (
                <button
                  id={`result-tab-${tab}`}
                  key={tab}
                  type="button"
                  role="tab"
                  aria-selected={activeTab === tab}
                  aria-controls="compiler-result-panel"
                  tabIndex={activeTab === tab ? 0 : -1}
                  onClick={() => setActiveTab(tab)}
                  onKeyDown={(event) => moveResultFocus(event, index)}
                >
                  {tabLabels[tab]}
                  {panels[tab] && (
                    <span className="tab-ready" aria-hidden="true" />
                  )}
                </button>
              ))}
            </div>
            {instructionCount > 0 && (
              <span className="instruction-count">
                {instructionCount.toLocaleString()} instructions
              </span>
            )}
          </div>
          <h2 id="results-title" className="visually-hidden">
            Compiler results
          </h2>
          <div
            id="compiler-result-panel"
            role="tabpanel"
            aria-labelledby={`result-tab-${activeTab}`}
            className={`result-content ${panels[activeTab] ? 'has-result' : ''}`}
            tabIndex={0}
          >
            <pre>{displayedResult}</pre>
          </div>
        </section>
      </main>

      <div className="visually-hidden" aria-live="polite">
        {announcement}
      </div>
    </div>
  )
}

export default App
