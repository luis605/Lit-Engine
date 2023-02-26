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



/* File Extension */
namespace filesys = boost::filesystem;

std::string getFileExtension(std::string filePath)
{
    filesys::path pathObj(filePath);

    if (pathObj.has_extension()) {
        return pathObj.extension().string();
    }

    // In case of no extension return empty string
    return "no file extension";
}


class Entity;

std::vector<Entity> entities_list_pregame;


/* Entity */
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
  string script = "";

  // Create a model object
  Model model;


  Entity(Color color = { 255, 255, 255, 255 }, Vector3 rotation = { 0, 0, 0 }, Vector3 scale = { 1, 1, 1 }, string name = "entity", int x = 0, int y = 0, int z = 0, Vector3 position = {0, 0, 0}, string script = "")
    : color(color), size(size), rotation(rotation), scale(scale), name(name), x(x), y(y), z(z), position(position), script(script)
   {
    // initializeModel();
   }


    void remove() {
        entities_list_pregame.erase(std::remove_if(entities_list_pregame.begin(), entities_list_pregame.end(),
        [this](const Entity& entity) {
        return entity.getName() == this->name;
        }),
        entities_list_pregame.end());
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

    void setShader(Shader shader)
    {
        model.materials[0].shader = shader;
    }

    void runScript()
    {

        FILE* file = fopen(script.c_str(), "r");
        PyRun_SimpleFile(file, script.c_str());
    }

    // Draw the model
    void draw() {
        if (!hasModel())
        {
            initializeModel();
        }
        
        model.transform = MatrixScale(scale.x, scale.y, scale.z);
        DrawModel(model, {position.x, position.y, position.z}, 1.0f, color);
    }


};

