#ifndef GRID_H
#define GRID_H

#include "types.h"
#include "noise.h"

// Initialize the storage region with an atom at every site.
Grid initializeStorage(int h, int w);

// Initialize the preparation region with 50% occupancy probability at each site.
Grid initializePreparation(int m);

// Apply a partial reloading instruction.
void applyReloading(Grid &A, const ReloadParams &params, int &discarded, const NoiseMask &reload_failure);

// Apply environmental noise.
void applyEnvironmentNoise(Grid &A, const NoiseMask &idle_loss);

// Count atoms.
int countAtoms(const Grid &A);

#endif // GRID_H
