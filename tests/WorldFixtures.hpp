#pragma once

struct Health {
    int hp;
};

struct Link {
    EntityHandle target;
};

struct Patrol : Script {
    int waypoint;
    float speed;
    Patrol(int initialWaypoint, float initialSpeed) : waypoint(initialWaypoint), speed(initialSpeed) {}
};

struct Counter : Script {
    int* starts;
    int* updates;
    int* destroys;
    Counter(int* startCounter, int* updateCounter, int* destroyCounter) : starts(startCounter), updates(updateCounter), destroys(destroyCounter) {}
    void onStart(World&, EntityHandle) override { ++*starts; }
    void onUpdate(World& world, EntityHandle entity, float) override {
        ++*updates;
        world.destroy(entity);
    }
    void onDestroy(World&, EntityHandle) override { ++*destroys; }
};

inline bool nearlyEqual(const glm::vec3& first, const glm::vec3& second, float tolerance = 1.0e-4f) { return glm::length(first - second) < tolerance; }

inline void registerHealth(World& world) {
    world.registerComponent<Health>(
        "Health", [](const Health& health, std::ostream& out) { out << health.hp; },
        [](std::istream& in) -> std::optional<Health> {
            int hp;
            if (!(in >> hp)) return std::nullopt;
            return Health{hp};
        });
}

inline void registerLink(World& world) {
    world.registerComponent<Link>(
        "Link", [](const Link& link, std::ostream& out) { out << link.target.index; },
        [](std::istream& in, const LoadContext& context) -> std::optional<Link> {
            uint32_t index;
            if (!(in >> index)) return std::nullopt;
            return Link{context.resolve(index)};
        });
}

inline void registerPatrol(World& world) {
    world.registerScript<Patrol>(
        "Patrol", [](const Patrol& patrol, std::ostream& out) { out << patrol.waypoint << ' ' << patrol.speed; },
        [](std::istream& in) -> std::optional<Patrol> {
            int waypoint;
            float speed;
            if (!(in >> waypoint >> speed)) return std::nullopt;
            return Patrol(waypoint, speed);
        });
}
