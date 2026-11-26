/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A cue sound moved to another pitch with its length kept, by Signalsmith
 * Stretch's spectral pitch shift (built by a11y.cmake): its partials move,
 * its envelope and timing stay. For a sound from a file, which nothing else
 * can make again at another pitch; a sound made by code is better made
 * again at the pitch, which is exact. Slow next to playing a sound: made
 * once, at engine start, for a voice's pitched copies (cue_sounds.hpp). */
#pragma once
#include "cue_engine.hpp"

namespace a11y {

/* The sound a factor up in pitch (2: an octave), as long as it was. The
 * sound itself at a factor of 1. */
Sound shift_pitch(const Sound& sound, float factor);

}  // namespace a11y
