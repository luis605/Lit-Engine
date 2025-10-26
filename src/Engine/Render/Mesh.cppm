module;

#include <vector>

export module Engine.mesh;

export class Mesh {
  public:
    Mesh(const std::vector<float>& vertices, const std::vector<unsigned int>& indices);
    ~Mesh();

    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    Mesh(Mesh&& other) noexcept;
    Mesh& operator=(Mesh&& other) noexcept;

    void release();
    void draw() const;

  private:
    void setupMesh(const std::vector<float>& vertices, const std::vector<unsigned int>& indices);

    unsigned int m_VAO = 0;
    unsigned int m_VBO = 0;
    unsigned int m_EBO = 0;
    unsigned int m_indexCount = 0;
};
