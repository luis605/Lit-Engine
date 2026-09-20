module;

#include <cstddef>
#include <vector>

export module Engine.mesh;

export constexpr std::size_t kLodLevelCount = 7;

export class Mesh {
  public:
    std::vector<float> vertices;
    std::vector<unsigned int> indices;

    Mesh(std::vector<float>&& vertices, std::vector<unsigned int>&& indices)
        : vertices(std::move(vertices)), indices(std::move(indices)) {}

    Mesh(const std::vector<float>& vertices, const std::vector<unsigned int>& indices)
        : vertices(vertices), indices(indices) {}

    ~Mesh() = default;

    Mesh(const Mesh&) = default;
    Mesh& operator=(const Mesh&) = default;

    Mesh(Mesh&& other) noexcept = default;
    Mesh& operator=(Mesh&& other) noexcept = default;

    Mesh simplify(float targetRatio, float targetError = 0.01f) const;
    Mesh optimized() const;
    Mesh makeImpostor() const;
    std::vector<float> normalSamples(std::size_t count) const;
    bool isRound() const;
    float surfaceArea() const;

    std::vector<Mesh> generateLODChain() const;
};