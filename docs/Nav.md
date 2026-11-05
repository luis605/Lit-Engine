# Navigation

Module `Engine.Nav`. Grid navmesh built in tiles from a triangle soup, A* with string-pulling, crowd stepping with local avoidance.

## Dynamic obstacles

```cpp
uint32_t id = mesh.addObstacle(box);
mesh.moveObstacle(id, newBox);
mesh.removeObstacle(id);
mesh.rebuildDirty(maxTilesPerFrame);
```

Each change marks only the tiles touching the old and new box (plus agent-radius padding) dirty. `rebuildDirty` rebuilds a bounded number of tiles per call and bumps `version()`. Every rebuilt tile records the version that built it.

Agents repath automatically. On a version change `mesh.pathStale(path, sinceVersion)` samples the path against per-tile build versions, link changes and geometry resets. Only agents whose path crosses a rebuilt tile repath; the others just adopt the new version (`NavCrowdStats::repathsSkipped`).

## Path queries

- `findPath` / `findPathInto(from, to, out)`: synchronous. The search tables are thread-local and reused, so a steady-state `findPathInto` into a warm `NavPath` allocates nothing.
- `beginPath(query, from, to)` and `advancePath(query, expansionBudget)`: resumable A*. `advancePath` returns true when finished. If the mesh version changes mid-query the query restarts. A sliced result equals the synchronous result.
- Search: 8-connected A* with a horizontal octile heuristic scaled by 1.0005 (path cost within 0.05% of optimal; `NavPath::gridCost` is the raw grid cost, tested against Dijkstra on random obstacle maps) and larger-g tie-breaking. When the mesh has off-mesh links the heuristic falls back to Euclidean (no probe). Neighbour layers are resolved through a per-search tile cursor. After 1500 expansions a bounded flood from the goal checks whether the goal sits on a tiny island (for example a pillar top); if it does the search stops at once and returns the partial path to the closest reached node instead of flooding the whole map. No abstract cluster graph is kept, so there is nothing to invalidate per tile.
- Smoothing is greedy string-pulling over line-of-walkability (window 48 nodes), never across off-mesh links.

## Off-mesh links

`addLink` / `setLinkEnabled` / `removeLink`. Links appear in the path as an entry point (`link >= 0`) followed by an exit point (`linkExit`). Smoothing never skips them, agents reach the entry, then call the `NavLinkTraverseFn` (or teleport to the exit). Enabling or disabling a link invalidates every path.

## Crowds

```cpp
NavCrowdConfig config;
config.pathExpansionBudget = 4000;
NavCrowd crowd(config);
crowd.step(mesh, agents, dt);
```

- Neighbour queries use a counting-sorted spatial hash (cell = `neighborRange`, default 3 m), rebuilt per step into reused buffers. Each agent keeps its 8 nearest neighbours.
- Avoidance is ORCA (NavOrca.cpp): one half-plane per neighbour (2.5 s horizon, responsibility 0.5, 1.0 against near-stationary neighbours), solved by the 2D linear program with the 3D fallback when infeasible, so the result stays inside max speed. Deadlocks of exactly head-on agents are broken by a deterministic right-hand bias (preferred velocity rotated about 5.7 degrees) whenever neighbours are present. An agent touching a neighbour within `1.4 * radius` of its goal at low speed counts as arrived (crowd arrival). Neighbours come from a snapshot taken at the start of the step, so the result does not depend on agent order within the step.
- Path following: waypoints are cut within `NavAgent::cornerRadius` (0.5 m) when the straight line to the next waypoint is walkable; speed drops to 55% at sharp corners within 1 m; the last waypoint ramps speed down over `arriveRadius`.
- Repaths go through a direct-mapped path cache keyed by start node, goal node and mesh version (`cacheEntries`). Agents sharing a goal reuse one search.
- With a finite `pathExpansionBudget`, repaths are queued FIFO and time-sliced: at most that many A* expansions per step, at most `queriesPerStep` new queries started. Agents keep walking the old path while queued. `stats().pending` reports the backlog.
- `navStepAgents(mesh, agents, dt)` keeps the old signature and uses a thread-local `NavCrowd` with unlimited budget, so it is synchronous. `navStepAgent` (single agent) is unchanged in API.

## Determinism

No wall-clock, threads, hash-map iteration or randomness is used in stepping. Budgets are counted in expansions, not milliseconds. `tests/NavTests.cpp` runs two identical 300-agent simulations (budgeted repaths plus a mid-run obstacle) and compares position/velocity hashes.

## Measurements

Release build, `tests/NavTests.cpp` and `LitBench --filter nav`.

| Benchmark | Before | After |
|---|---|---|
| nav.rebuild_all_64m_arena | 46.4 ms, 389,903 allocs | 11.2 ms, 904 allocs |
| nav.find_path_x100 | 1177 ms, 371,481 allocs | 35 ms, 100 allocs (the returned vector; 0 with `findPathInto`) |
| nav.step_agents_200 | 13.0 ms, 10,343 allocs | 0.19 ms, 0 allocs |

Scaling test (`NavTests`, 120 m arena, random goals, 30 steps): 1,000 agents 0.6 ms/step, 5,000 agents 14 ms/step.
