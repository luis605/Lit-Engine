module;

#include <vector>

export module Engine.mesh;

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
    std::vector<Mesh> generateLODs(const std::vector<float>& targetRatios) const;
};