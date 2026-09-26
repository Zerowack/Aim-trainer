// rank_badge.h - Vector emblems for the aim rank tiers (original designs,
// drawn with the UI toolkit; no game artwork is used).
#pragma once

#include "rank.h"

// Draws the emblem for 'r' centred on (cx, cy), about 'size' units tall
// (virtual UI units). 'alpha' dims the emblem (used for the tier ladder).
// Division pips are drawn underneath unless showDivision is false.
void DrawRankBadge(float cx, float cy, float size, const AimRank& r, float alpha = 1.0f, bool showDivision = true);
