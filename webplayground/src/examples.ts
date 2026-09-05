import bossFightSource from '../../examples/boss_fight.frame?raw'
import connectionSource from '../../examples/connection.frame?raw'
import fibonacciSource from '../../examples/fibonacci.frame?raw'

export type ExampleId = 'boss-fight' | 'fibonacci' | 'connection'

interface PlaygroundExample {
  id: ExampleId
  name: string
  category: string
  summary: string
  concepts: string[]
  source: string
  expectedOutput: string
}

export const examples: PlaygroundExample[] = [
  {
    id: 'boss-fight',
    name: 'Boss Fight',
    category: 'Complete program',
    summary:
      'Calculates a critical hit, updates mutable boss health, and prints each result.',
    concepts: [
      'Typed functions',
      'Mutable globals',
      'Boolean branches',
      'Integer arithmetic',
    ],
    source: bossFightSource,
    expectedOutput: 'Critical hit!\n70\n50',
  },
  {
    id: 'fibonacci',
    name: 'Fibonacci',
    category: 'Recursion',
    summary:
      'Computes the tenth Fibonacci number with a recursive function and an if expression.',
    concepts: ['Recursion', 'If expressions', 'Comparisons', 'Return values'],
    source: fibonacciSource,
    expectedOutput: '55',
  },
  {
    id: 'connection',
    name: 'Connection',
    category: 'Language basics',
    summary:
      'Sends a connection sequence with a deliberate pause through a typed function and frameout.',
    concepts: ['String values', 'Function parameters', 'Repeated calls', 'frameout'],
    source: connectionSource,
    expectedOutput:
      'ARE YOU THERE?\nARE WE CONNECTED?\n\nEXCELLENT.\nTRULY EXCELLENT.\nNOW.\n\nWE MAY BEGIN.',
  },
]
