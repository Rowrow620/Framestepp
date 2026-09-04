import bossFightSource from '../../examples/boss_fight.frame?raw'
import fibonacciSource from '../../examples/fibonacci.frame?raw'
import helloSource from '../../examples/hello.frame?raw'

export type ExampleId = 'boss-fight' | 'fibonacci' | 'hello'

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
    id: 'hello',
    name: 'Hello, Ada',
    category: 'Language basics',
    summary:
      'Builds a greeting with a typed function, string concatenation, and frameout.',
    concepts: ['String values', 'Function parameters', 'Return types', 'frameout'],
    source: helloSource,
    expectedOutput: 'Hello, Ada',
  },
]
