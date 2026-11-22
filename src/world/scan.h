#pragma once

#include <main.h>

namespace horse_races::world::scan {
// Central adapter for expensive pool enumeration. Callers cache the result
// appropriate to their own sampling cadence and never mutate returned actors.
inline int Peds(int* handles,int capacity){return worldGetAllPeds(handles,capacity);}
inline int Vehicles(int* handles,int capacity){return worldGetAllVehicles(handles,capacity);}
} // namespace horse_races::world::scan
