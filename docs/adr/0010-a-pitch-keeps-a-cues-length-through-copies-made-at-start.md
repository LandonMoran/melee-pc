# A pitch keeps a cue's length through copies made at start

Status: accepted (2026-11-16)

The position cues and the item cues say how high something is by pitch. The cue engine played a sound at another pitch by playing it faster or slower, so a sound an octave up lasted half as long and sounded thinner, and an octave down twice as long: your tick, 32 ms at its own pitch, was 64 ms at the bottom of the range. The maintainer's Star Fox 64 fork has no such change of length: it runs its cues through a spectral pitch shifter, Signalsmith Stretch, which moves a sound's pitch and keeps its length.

Every pitched cue here plays a short, fixed sound from its start at each pulse or play, so the shifting can happen before anything plays. A pulsed voice or a one-shot may carry copies of its sound made when the engine starts, a semitone apart across the pitches it plays at (`PitchedCopies` in `cue_engine.hpp`, `pitched_copies` in `cue_sounds.hpp`). Each pulse or play takes the copy nearest its target pitch and plays it at the rest of the pitch, a quarter-tone at most within the copies' span, which changes its length by 3% at most; a glide during the pulse still slides the pitch by playback rate. A pulse that starts just after a jump, while the pitch still glides there, takes the copy of the pitch the glide is heading for: it plays fast or slow only until the glide ends, mostly over its silent lead-in. A sound made by code is made again at each copy's pitch, which is exact; a sound from a file is moved there by Signalsmith Stretch (`pitch_shift.cpp`). Primer: `docs/a11y/cues.md`, "The engine's rules" and "Sounds".

## Considered options

- **The shifter running live, as in the Star Fox 64 fork**: every voice's sound goes through Signalsmith Stretch as it plays. It costs about a tenth of a second of delay on the sound, too late for a fighting game, and softens a sharp attack; the Star Fox 64 fork keeps its own click cues on playback rate for that reason, and your tick is its aim click. A pitch change during a pulse would also reach the ear only through the shifter's delay.
- **Playback rate alone**, as before: no delay and exact, but a pitch drags the length with it, so every pulse's length had to fit its fastest interval an octave down, and a high item sounded thin and short beside a low one.
- **Making every sound again at the pitch as it plays**, with no copies: exact for sounds made by code, impossible for a file.
- **The copy nearest the pitch the glide has reached**: a pulse just after a jump then takes the old pitch's copy and plays it sped up or slowed down all the way to the new pitch, nearly as shortened or stretched as with no copies (found in review).
- **Copies by Signalsmith Stretch for every sound**: the copies of a sound made by code would carry the shifter's small smear for nothing, when making the sound again is exact.

## Consequences

- No delay and no work on the audio thread beyond picking a copy at each pulse. The copies take memory, one sound's length per semitone across the widest pitch range (`MatchCues::kWidestPitchRange`), which is small for sounds of a tenth of a second, and a little time at start.
- A looped voice cannot carry copies: a held sound changing copies would need a crossfade. The gap warning's hum, the one looped cue, keeps one pitch.
- A pitch beyond the copies' span plays the last copy at the rest, changing the length again; the span is the widest the settings allow, so a match cue never goes beyond it.
- The shifter is a configure-time download, header only, compiled once in its own library, and its two MIT licences (the shifter's and the Signalsmith DSP library's inside it) ship in the CI packages.
- The shifter's analysis block is set for sounds of about a tenth of a second (`pitch_shift.cpp`); a much longer or lower sound from a file may want a longer block.
