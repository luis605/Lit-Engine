#include "string"
#include <sstream>
#include <vector>

using namespace std;
using std::vector;

std::string colorToString(const Color& color) {
  std::stringstream ss;
  ss << "(" << color.r << ", " << color.g << ", " << color.b << ", " << color.a << ")";
  return ss.str();
}

/* Entities */
class Entity {
public:
  string name = "Entity";
  int x = 0;
  int y = 0;
  int z = 0;
  Vector3 position;
  Color color;
  float size = 1;
  Vector3 rotation;
  Vector3 scale = { 1, 1, 1 };

  // Create a model object
  Model model;


  Entity(Color color = { 255, 255, 255, 255 }, Vector3 rotation = { 0, 0, 0 }, Vector3 scale = { 1, 1, 1 }, string name = "entity", int x = 0, int y = 0, int z = 0, Vector3 position = {0, 0, 0})
    : color(color), size(size), rotation(rotation), scale(scale), name(name), x(x), y(y), z(z), position(position)
   {
    // initializeModel();
   }



    void setColor(Color newColor) {
        color = newColor;
    }

    void setName(const std::string& newName) {
        name = newName;
    }

    std::string getName() const {
        return name;
    }

    void setScale(Vector3 newScale) {
        scale = newScale;
    }

    // Initialize the model with a cube mesh
    void initializeModel() {
        // Create a mesh for a cube
        Mesh mesh = GenMeshCube(scale.x, scale.y, scale.z);

        // Create a model from the mesh
        model = LoadModelFromMesh(mesh);
    }

    // Load a model from a file and set its material properties
    void loadModel(const char* filename, const char* textureFilename) {
        model = LoadModel(filename);
    }

    // Set the model's position, rotation, and scale
    void setTransform(Vector3 position, float rotation, float scale) {
        model.transform = MatrixScale(scale, scale, scale);
        // model.transform = MatrixRotateY(model.transform, rotation);
        // model.transform = MatrixTranslate(model.transform, position.x, position.y, position.z);
    }

    void setModel(char* modelPath)
    {
        model = LoadModel(modelPath);
    }

    bool hasModel()
    {

        if (model.meshCount > 0)
        {
            // model has been initialized with data
            return true;
        }
        else
        {
            // model has not been initialized with data
            return false;
        }
    }

    // Draw the model
    void draw() {
        if (!hasModel())
        {
            initializeModel();
        }
        DrawModel(model, {position.x, position.y, position.z}, 1.0f, color);
    }



};


