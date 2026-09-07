# Modo Test Sintético — Runbook de Validación

Validación del pipeline completo (filtro → rebound → velocity → TDOA → MIDI)
**sin hardware**: el generador sintético inyecta golpes de piezo artificiales
directamente al ring buffer (#19) y la consola dispara los tests (#20).

---

## 1. Activación

**Vía consola** (recomendada, puerto serial 115200 baud):

```
test hit 100 64      → activa el modo si hace falta y dispara un golpe
test mode on         → solo activa el modo (ADC detenido)
test auto 500        → golpes periódicos cada 500 ms
test stop            → detiene todo y restaura el ADC real
test status          → estado del generador
```

**Vía flag de compilación** (sin consola):

```
idf.py menuconfig  →  Edrumulus Synthetic Test  →  [*] Enable synthetic test mode automatically at boot
```

Con AUTOSTART el firmware arranca directamente en modo sintético con
`test auto 500` activo. Volver a compilar sin el flag para restaurar el ADC.

> Mientras el modo está activo, **el ADC real está detenido** (regla de
> productor único del ring buffer). `test stop` / `test mode off` lo restauran.

---

## 2. Parámetros de la señal (runtime)

| Comando | Default | Efecto |
|---|---|---|
| `set synth_freq <hz>` | 340 | Frecuencia del burst. Pasa el filtro banda efectivo (~320 Hz–3.2 kHz, ver hallazgo A) |
| `set synth_decay <ms>` | 120 | Decay exponencial del burst |
| `set synth_rise <ms>` | 1.0 | Ataque |
| `set synth_dur <ms>` | 250 | Duración total del burst |

Mapeos internos (consistentes con el pipeline):

- **Posición → delay**: `Δt = ((pos−63.5)/63.5) · 3000 µs` entre el pico de
  piezo2 y piezo1. Es la inversa exacta del mapeo TDOA del pipeline.
- **Velocity → amplitud**: `A = 2·vel/127` (ver hallazgo B).

---

## 3. Qué esperar en MIDIView

Por cada golpe sintético (pad 0 → note 38, canal MIDI 10 / hex `0x99`):

```
99 26 LL      Note On C1, velocity LL
B9 10 PP      CC#16 = posición (0-127)
```

---

## 4. Matriz de validación

### 4.1. Detección + velocity (TDOA en el centro)

| Comando | Esperado en MIDIView |
|---|---|
| `test hit 100 64` | Note On vel ≈ **63** (ver hallazgo B) + CC16 ≈ **64** |
| `test hit 32 64`  | Note On vel ≈ 32 + CC16 ≈ 64 |
| `test hit 127 64` | Note On vel ≈ 63 (techo) + CC16 ≈ 64 |
| `test hit 5 64`   | Probablemente **no dispara** — amplitud A=0.08 queda al borde del umbral de flanco (0.02/muestra). Si hace falta, subir freq (`set synth_freq 800`) |

### 4.2. TDOA / posición (velocity fija)

La posición mostrada converge con el EMA del pipeline (α=0.35): usar
`test auto` y mirar el valor **estable**, o disparar 3-4 hits seguidos.
Valor esperado ≈ `0.7·pos + 0.3·64` (la amplitud es igual en ambos canales,
ver hallazgo C).

| Comando | Δt inyectado | CC16 esperado |
|---|---|---|
| `test hit 100 0`   | −3000 µs | ≈ 19 |
| `test hit 100 32`  | −1488 µs | ≈ 42 |
| `test hit 100 64`  | 0 (→ fallback amplitud) | ≈ 64 |
| `test hit 100 96`  | +1488 µs | ≈ 86 |
| `test hit 100 127` | +3000 µs | ≈ 108 |

Criterio: CC16 **monótono** con `pos`. Las desviaciones del EMA entre hits
consecutivos indican el tiempo de asentamiento del filtro de posición.

### 4.3. Mask time / dobles triggers

```
test auto 100      → 1 par Note On por segundo aprox, sin dobles
test auto 60       → mínimo permitido (50 ms); seguirá sin dobles
```

Dentro de un mismo burst la cola de 340 Hz podría generar un segundo flanco
ascendente luego del mask time (5-10 ms cubre ~2 ciclos). Es **esperado**
que en algunos golpes aparezca 1 Note On extra de velocity baja (artefacto
de rebote) — es justamente lo que el detector de rebotes debe filtrar; su
frecuencia es una métrica del pipeline (ver hallazgo D).

---

## 5. Hallazgos de la caracterización (conocidos)

- **A. Banda efectiva del filtro desplazada ×8**: el filtro IIR está diseñado
  a 1 kHz (`EDRUMULUS_FILTER_SAMPLE_RATE`) pero se ejecuta a 8 kHz → el
  band-pass real es ~320 Hz–3.2 kHz, no 40–400 Hz. Por eso el burst default
  es 340 Hz. Corregir los coeficientes es tarea aparte.
- **B. Techo de velocity ≈ 63**: `hit_velocity = peak·127` mide el pico de la
  señal filtrada **sin DC** (bipolar), cuyo pico ≈ A/2. Con A≤1 la velocity
  observable nunca supera ~63. El generador pide `A = 2·vel/127` para
  aprovechar todo el rango utilizable.
- **C. Un solo canal carga la posición**: ambos canales sintéticos usan la
  misma amplitud, así que el estimador por amplitud queda en ~64 y el TDOA
  (peso 70%) lleva la posición → CC esperado ≈ `0.7·pos + 19.2`.
- **D. Detección por canal (doble Note On)**: cada piezo ejecuta su propio
  pipeline sin arbitraje a nivel de pad → un golpe puede emitir 2 Note On
  (uno por canal, separados por Δt). visible también con piezos reales.
  Pendiente: arbitraje de pad.
- **E. Timestamps del ADC a nivel de frame**: el callback DMA estampa el
  mismo `esp_timer` a todas las muestras del frame → con ADC real el TDOA no
  tiene resolución (Δt=0 siempre). El modo sintético estampa por muestra y
  por eso valida el algoritmo TDOA. Para HW real hace falta timestamp por
  conversión dentro del frame.

---

## 6. Checklist de validación HITL

- [ ] Flashear y conectar MIDIView (loopMIDI/DAW)
- [ ] `test hit 100 64` → Note On + CC16 visibles, velocity ≈ 63
- [ ] `test auto 500` → golpes periódicos estables, sin flood
- [ ] Matriz 4.2 → CC16 monótono con la posición pedida
- [ ] `test hit 32 64` → velocity baja (~32) coherente
- [ ] `test stop` → ADC restaurado (log "Synthetic mode DISABLED (ADC restored)")
- [ ] (opcional) AUTOSTART=y → golpes desde el boot sin consola

Resultados y desvíos: comentarlos en el issue #21.
