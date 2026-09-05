import { useCallback, useEffect, useRef, useState } from 'react'
import {
  createTransmissionPlan,
  shouldAnimateTransmission,
} from './transmission'

export interface TransmissionFrame {
  active: boolean
  enabled: boolean
  freshStart: number
  target: string
  visibleText: string
}

const emptyFrame: TransmissionFrame = {
  active: false,
  enabled: false,
  freshStart: 0,
  target: '',
  visibleText: '',
}

const isInteractiveTarget = (target: EventTarget | null) => {
  if (!(target instanceof HTMLElement)) {
    return false
  }

  return (
    target.closest(
      'a, button, input, select, summary, textarea, [contenteditable="true"], [role="button"], [role="checkbox"], [role="link"], [role="menuitem"], [role="option"], [role="radio"], [role="switch"], [role="tab"]',
    ) !== null
  )
}

export const useTransmission = () => {
  const [frame, setFrame] = useState<TransmissionFrame>(emptyFrame)
  const animationFrameRef = useRef<number | null>(null)
  const generationRef = useRef(0)

  const clearAnimationFrame = useCallback(() => {
    if (animationFrameRef.current !== null) {
      window.cancelAnimationFrame(animationFrameRef.current)
      animationFrameRef.current = null
    }
  }, [])

  const cancelTransmission = useCallback(() => {
    generationRef.current += 1
    clearAnimationFrame()
    setFrame(emptyFrame)
  }, [clearAnimationFrame])

  const finishTransmission = useCallback(() => {
    generationRef.current += 1
    clearAnimationFrame()
    setFrame((current) =>
      current.enabled
        ? {
            ...current,
            active: false,
            freshStart: current.target.length,
            visibleText: current.target,
          }
        : current,
    )
  }, [clearAnimationFrame])

  const startTransmission = useCallback(
    (text: string) => {
      generationRef.current += 1
      const generation = generationRef.current
      clearAnimationFrame()

      const prefersReducedMotion = window.matchMedia(
        '(prefers-reduced-motion: reduce)',
      ).matches

      if (!shouldAnimateTransmission(text.length, prefersReducedMotion)) {
        setFrame({
          active: false,
          enabled: true,
          freshStart: text.length,
          target: text,
          visibleText: text,
        })
        return
      }

      const plan = createTransmissionPlan(text)
      let revealedCount = 0
      let startedAt: number | null = null

      setFrame({
        active: true,
        enabled: true,
        freshStart: 0,
        target: text,
        visibleText: '',
      })

      const reveal = (timestamp: number) => {
        if (generationRef.current !== generation) {
          return
        }

        startedAt ??= timestamp
        const playbackTime = (timestamp - startedAt) * plan.playbackRate
        let nextCount = revealedCount

        while (
          nextCount < plan.characters.length &&
          plan.revealAt[nextCount] <= playbackTime
        ) {
          nextCount += 1
        }

        if (nextCount !== revealedCount) {
          revealedCount = nextCount
          const visibleCharacters = plan.characters.slice(0, revealedCount)
          const freshCharacterStart = Math.max(0, revealedCount - 5)
          const visibleText = visibleCharacters.join('')
          const freshStart = plan.characters
            .slice(0, freshCharacterStart)
            .join('').length
          const active = revealedCount < plan.characters.length

          setFrame({
            active,
            enabled: true,
            freshStart,
            target: text,
            visibleText,
          })

          if (!active) {
            animationFrameRef.current = null
            return
          }
        }

        animationFrameRef.current = window.requestAnimationFrame(reveal)
      }

      animationFrameRef.current = window.requestAnimationFrame(reveal)
    },
    [clearAnimationFrame],
  )

  useEffect(() => {
    if (!frame.active) {
      return
    }

    const handleKeyDown = (event: KeyboardEvent) => {
      if (
        (event.key !== ' ' && event.code !== 'Space') ||
        event.altKey ||
        event.ctrlKey ||
        event.metaKey ||
        event.shiftKey ||
        isInteractiveTarget(event.target)
      ) {
        return
      }

      event.preventDefault()
      finishTransmission()
    }

    window.addEventListener('keydown', handleKeyDown)
    return () => window.removeEventListener('keydown', handleKeyDown)
  }, [finishTransmission, frame.active])

  useEffect(() => {
    const motionPreference = window.matchMedia(
      '(prefers-reduced-motion: reduce)',
    )
    const handleMotionChange = () => {
      if (motionPreference.matches) {
        finishTransmission()
      }
    }

    motionPreference.addEventListener('change', handleMotionChange)
    return () =>
      motionPreference.removeEventListener('change', handleMotionChange)
  }, [finishTransmission])

  useEffect(
    () => () => {
      generationRef.current += 1
      clearAnimationFrame()
    },
    [clearAnimationFrame],
  )

  return {
    cancelTransmission,
    finishTransmission,
    frame,
    startTransmission,
  }
}
