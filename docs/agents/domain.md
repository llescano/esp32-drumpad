# Domain Docs

How the engineering skills should consume this repo's domain documentation when exploring the codebase.

## Layout: single-context

This is a single-context repo (embedded firmware, one bounded system). `CONTEXT.md` and `docs/adr/` live at the repo root.

```
/
├── CONTEXT.md
├── docs/adr/
│   └── ...
├── main/                      ← app entry point
├── components/                ← ESP-IDF components
└── ...
```

## Before exploring, read these

- **`CONTEXT.md`** at the repo root
- **`docs/adr/`**: read ADRs that touch the area you're about to work in

If any of these files don't exist, **proceed silently**. Don't flag their absence; don't suggest creating them upfront. The `/domain-modeling` skill (reached via `/grill-with-docs` and `/improve-codebase-architecture`) creates them lazily when terms or decisions actually get resolved.

## Hardware domain glossary

Key terms used in this project (mirrors `ARQUITECTURA.md`):

- **Pad**: una zona de golpeo con 2 piezos (primario + secundario). Cada pad tiene su propio ID, nota MIDI, y umbral.
- **Piezo 1 / Piezo 2**: sensores del pad. Piezo 1 es el primario (GPIO4), piezo 2 el secundario para positional sensing (GPIO5).
- **TDOA**: Time Difference of Arrival — la diferencia temporal entre los picos de los dos piezos para calcular posición.
- **Amplitude Ratio**: comparación de amplitudes entre ambos piezos como método alternativo de posición.
- **Rebound detector**: algoritmo Phase 3 que filtra rebotes mecánicos usando edge detection, decay analysis, y velocity validation.
- **Mask time**: ventana de tiempo post-golpe donde se ignoran nuevos triggers (previene retriggering).
- **Hit event**: estructura con channel, pad_id, velocity, position, cc_position, note, timestamp.

## Use the glossary's vocabulary

When your output names a domain concept (in an issue title, a refactor proposal, a hypothesis, a test name), use the term as defined here and in `ARQUITECTURA.md`. Don't drift to synonyms the glossary explicitly avoids.

If the concept you need isn't in the glossary yet, that's a signal: either you're inventing language the project doesn't use (reconsider) or there's a real gap (note it for `/domain-modeling`).

## Flag ADR conflicts

If your output contradicts an existing ADR, surface it explicitly rather than silently overriding:

> _Contradicts ADR-0007 (event-sourced orders), but worth reopening because…_
