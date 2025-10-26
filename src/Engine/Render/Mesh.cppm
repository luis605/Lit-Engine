module;

#include <vector>

export module Engine.mesh;

export class Mesh {
  public:
    std::vector<float> vertices;
    std::vector<unsigned int> indices;

    Mesh(std::vector<float>&& vertices, std::vector<unsigned int>&& indices)
        : vertices(std::move(vertices)), indices(std::move(indices)) {}
    ~Mesh() = default;

    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    Mesh(Mesh&& other) noexcept = default;
    Mesh& operator=(Mesh&& other) noexcept = default;
};