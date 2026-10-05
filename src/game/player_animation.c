#include "game/player_animation.h"

#include <math.h>

float player_swing_phase(float elapsed_seconds)
{
    if (!isfinite(elapsed_seconds) || elapsed_seconds <= 0.0f) {
        return 0.0f;
    }
    float phase = elapsed_seconds / PLAYER_SWING_DURATION;
    return phase < 1.0f ? phase : 1.0f;
}

float player_swing_weight(float phase)
{
    if (!isfinite(phase) || phase <= 0.0f || phase >= 1.0f) {
        return 0.0f;
    }
    return sinf(phase * 3.14159265358979323846f);
}
