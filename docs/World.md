# World API

`Engine` owns one `World`. It is the single control surface for scene content: entities, hierarchy, components, scripts, prefabs, scenes and spatial queries.

```cpp
Engine engine;
engine.init(window, 1280, 720);
World& world = engine.world();
const uint32_t cube = engine.loadMesh("cube");
```

## Handles

Every entity is an `EntityHandle{index, generation}`. Destroying an entity bumps its slot generation, so a stale handle is rejected by every call.

```cpp
EntityHandle a = world.create("crate", cube, glm::vec3(0, 1, 0));
world.destroy(a);
world.isAlive(a);
```

`EntityDesc` covers everything at creation time, and `createBatch(count, fn)` creates many entities with a single version bump.

## Hierarchy

```cpp
EntityHandle child = world.create("wheel", cube, glm::vec3(1, 0, 0), glm::vec3(1), a);
world.setParent(child, NULL_ENTITY);
for (EntityHandle c : world.getChildren(a)) { }
```

`setParent` keeps the world transform by default and refuses cycles. Child lists are intrusive, so `getChildren` costs O(children).

## Transform, visibility, layers, tags

```cpp
world.setPosition(a, glm::vec3(2, 0, 0));
world.translate(a, glm::vec3(0, 1, 0));
world.setVisible(a, false);
world.setLayer(a, 0b10);
world.setTag(a, "enemy");
world.findByTag("enemy");
```

Render flags (`RENDER_HIDDEN`, `RENDER_NO_OCCLUDER`) are read directly by the GPU culling shaders.

## Components

```cpp
struct Health { int hp; };
world.add<Health>(a, 100);
world.get<Health>(a)->hp -= 10;
world.view<Health>([](EntityHandle e, Health& h) { });
```

Register a serializer so scenes and prefabs carry the component:

```cpp
world.registerComponent<Health>(
    "Health", [](const Health& h, std::ostream& out) { out << h.hp; },
    [](std::istream& in) -> std::optional<Health> {
        int hp;
        if (!(in >> hp)) return std::nullopt;
        return Health{hp};
    });
```

## Scripts

```cpp
struct Spin : Script {
    void onUpdate(World& w, EntityHandle e, float dt) override { w.translate(e, glm::vec3(dt, 0, 0)); }
};
world.attach<Spin>(a);
engine.tick(dt);
```

`onFixedUpdate` runs at the fixed timestep. Destroying an entity from inside a script is deferred until the update pass finishes.

## Prefabs and scenes

```cpp
Prefab prefab = world.capture(a);
EntityHandle copy = world.instantiate(prefab, parent);
world.saveScene("level.litscene");
world.loadScene("level.litscene");
```

Scenes store mesh names, so they survive changes in upload order.

## Queries and picking

```cpp
auto hit = world.raycast(origin, direction);
auto near = world.overlapSphere(center, 5.0f);
auto picked = engine.pick(mouseX, mouseY);
```

The spatial grid is built on the first query and updated incrementally afterwards.

## Events and debug drawing

```cpp
world.events().subscribe<EntityCreated>([](const EntityCreated& e) { });
engine.debugSphere(center, 1.0f);
engine.debugHierarchy();
```

Events are queued and dispatched at the end of `Engine::tick`.
