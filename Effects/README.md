# AURA Built-in Effects

All effects are implemented in `Engine/DSP/` (`Aura::Dsp` namespace),
64-bit float, real-time safe after `prepare()`.

| Effect | Class | Presets |
|--------|-------|---------|
| Parametric EQ (6-band + HPF/LPF) | `ParametricEQ` | `Presets/EQ/*.json` |
| Compressor | `Compressor` | `Presets/Dynamics/*.json` |
| Limiter | `Limiter` | — |
| Gate | `Gate` | — |
| Reverb (Schroeder) | `Reverb` | `Presets/Creative/*.json` |
| Delay | `Delay` | — |
| Chorus | `Chorus` | — |
| Flanger | `Flanger` | — |
| Distortion | `Distortion` | — |

Preset JSON schema:

```json
{
  "effect": "ParametricEQ",
  "name": "Vocal Air",
  "parameters": { "band4_gainDb": 4.0, "band4_freqHz": 12000.0 }
}
```

QML views for each effect live in `UI/qml/` (Phase 2 wires them to the
`Aura::Mixer::IInsertProcessor` adapters).
