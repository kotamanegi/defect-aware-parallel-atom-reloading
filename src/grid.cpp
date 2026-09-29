#include "../include/grid.h"
#include <random>

// Initialize the storage region with an atom at every site.
Grid initializeStorage(int h, int w)
{
    return Grid(h, std::vector<int>(w, 1));
}

// Initialize the preparation region with 50% occupancy probability at each site.
Grid initializePreparation(int m)
{
    Grid prep(m, std::vector<int>(m, 0));
    std::uniform_real_distribution<> dis(0.0, 1.0);
    for (int i = 0; i < m; i++)
    {
        for (int j = 0; j < m; j++)
        {
            prep[i][j] = (dis(rng) < 0.5) ? 1 : 0;
        }
    }
    return prep;
}

// Apply a partial reloading instruction.
void applyReloading(Grid &A, const ReloadParams &params, int &discarded, const NoiseMask &reload_failure)
{
    discarded = 0;
    for (int r : params.R)
    {
        for (int c : params.C)
        {
            if (A[r][c] == 1)
                ++discarded;
            A[r][c] = reload_failure[r][c] ? 0 : 1;
        }
    }
}

// Apply environmental noise.
void applyEnvironmentNoise(Grid &A, const NoiseMask &idle_loss)
{
    int h = A.size();
    int w = A[0].size();
    for (int r = 0; r < h; r++)
    {
        for (int c = 0; c < w; c++)
        {
            if (idle_loss[r][c])
            {
                A[r][c] = 0;
            }
        }
    }
}

// Count atoms.
int countAtoms(const Grid &A)
{
    int count = 0;
    for (const auto &row : A)
    {
        for (int val : row)
        {
            count += val;
        }
    }
    return count;
}
