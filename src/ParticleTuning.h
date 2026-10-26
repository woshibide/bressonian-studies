#pragma once

#include <cstddef>

// Constants that shape how travelling pixels move. Edit, then run `make dev`.
// Speeds are in pixels per output frame. Steering weights are the share of a unit direction
// a traveller may turn toward each goal per output frame, so 0 disables that goal.
namespace particleTuning {

// Most travellers alive at once; new motion waits for free slots.
constexpr std::size_t maxTravellers = 2500000;
// Flow shorter than this, in pixels per measured interval, does not release a traveller.
constexpr float minFlowMagnitude = 0.3f;
// A traveller slower than this stops and is removed.
constexpr float stopSpeed = 0.035f;

// Boids only turn travellers; their speed still decays with the frame offset half-life.
// Neighbours are found on a grid of square cells this many pixels wide; each traveller
// looks at its own cell and the eight around it.
constexpr int boidCellSize = 16;
// Turn toward the average heading of the neighbours.
constexpr float boidAlignment = 0.8f;
// Turn toward the neighbours' centre. Full strength at boidCellSize pixels away.
constexpr float boidCohesion = 0.04f;
// Turn away from the centre of a crowded cell.
constexpr float boidSeparation = 0.10f;
// Travellers in one cell before separation starts; it is full strength at twice this.
constexpr float boidCrowdCount = 24.0f;
// Turn sideways around the neighbours' centre, so flocks circle instead of collapsing.
constexpr float boidSwirl = 0.1f;
// 1 circles clockwise on screen, -1 counter-clockwise.
constexpr float boidSwirlDirection = 1.0f;

} // namespace particleTuning
