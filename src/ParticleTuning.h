#pragma once

#include <cstddef>

// Constants that shape how travelling pixels move. Edit, then run `make dev`.
// Speeds are in pixels per output frame. Steering weights are the share of a unit direction
// a traveller may turn toward each goal per output frame, so 0 disables that goal.
namespace particleTuning {

// Most travellers alive at once; new motion waits for free slots.
constexpr std::size_t maxTravellers = 2500000;
// Most stopped travellers kept by Bake stopped pixels; the oldest are dropped first.
// Each costs 12 bytes per vertex and is redrawn every frame.
constexpr std::size_t maxBakedTravellers = 10000000;
// Flow shorter than this, in pixels per measured interval, does not release a traveller.
constexpr float minFlowMagnitude = 0.3f;
// A traveller slower than this stops and is removed.
constexpr float stopSpeed = 0.035f;


} // namespace particleTuning
