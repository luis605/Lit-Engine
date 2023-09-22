class Entity {
public:
    Entity* parent = nullptr;
    vector<variant<Entity*, Light*, Text*, LitButton*>> children;

private:
        vector<Entity*> instances;
    Matrix *transforms = nullptr;
    Material matInstances;

public:
    Entity(LitVector3 scale = { 1, 1, 1 }, LitVector3 rotation = { 0, 0, 0 }, string name = "entity",
    LitVector3 position = {0, 0, 0}, string script = "")
        : scale(scale), rotation(rotation), name(name), position(position), script(script)
    {   
        initialized = true;

    }

    void addInstance(Entity* instance) {
        instances.push_back(instance);


        if (transforms == nullptr) {
            transforms = (Matrix *)RL_CALLOC(instances.size(), sizeof(Matrix));
        } else {

            transforms = (Matrix *)RL_REALLOC(transforms, instances.size() * sizeof(Matrix));
        }

        int lastIndex = instances.size() - 1;
        calculateInstance(lastIndex);

        instancing_shader.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(instancing_shader, "mvp");
        instancing_shader.locs[SHADER_LOC_VECTOR_VIEW] = GetShaderLocation(instancing_shader, "viewPos");
        instancing_shader.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocationAttrib(instancing_shader, "instanceTransform");

    }

    bool hasInstances()
    {
        return instances.empty();
    }

    void calculateInstance(int index) {
        if (index < 0 || index >= instances.size()) {

            return;
        }

        Entity* entity = instances.at(index);

        Matrix translation = MatrixTranslate(entity->position.x, entity->position.y, entity->position.z);
        Matrix rotation = MatrixRotateXYZ(Vector3{ DEG2RAD * entity->rotation.x, DEG2RAD * entity->rotation.y, DEG2RAD * entity->rotation.z });    

        transforms[index] = MatrixMultiply(rotation, translation);

        matInstances = LoadMaterialDefault();
    }
        
    void addChild(Entity& entityChild) {
        Entity* newChild = new Entity(entityChild);

        newChild->relative_position = {
            newChild->position.x - this->position.x,
            newChild->position.y - this->position.y,
            newChild->position.z - this->position.z
        };

        newChild->parent = this;
        children.push_back(newChild);
    }

    void update_children()
    {
        if (children.empty()) return;
        
        for (std::variant<Entity*, Light*, Text*, LitButton*>& childVariant : children)
        {
            if (auto* child = std::get_if<Entity*>(&childVariant))
            {
                (*child)->render();

    #ifndef GAME_SHIPPING
                if (*child == selected_entity) continue;
    #endif

                (*child)->position = {this->position + (*child)->relative_position};
                (*child)->update_children();
            }
          
            else if (auto* child = std::get_if<Light*>(&childVariant))
            {                
                #ifndef GAME_SHIPPING
                            if (*child == selected_light && selected_game_object_type == "light") continue;
                #endif

                (*child)->position = glm::vec3(this->position.x, this->position.y, this->position.z) + (*child)->relative_position;
            }
        }
        UpdateLightsBuffer();
    }



    void render() {
        update_children();

        if (!visible) {
            return;
        }


        if (!instances.empty())
        {
            PassSurfaceMaterials();

            glUseProgram((GLuint)instancing_shader.id);

            bool normalMapInit = !normal_texture_path.empty();
            glUniform1i(glGetUniformLocation((GLuint)instancing_shader.id, "normalMapInit"), normalMapInit);

            bool roughnessMapInit = !roughness_texture_path.empty();
            glUniform1i(glGetUniformLocation((GLuint)instancing_shader.id, "roughnessMapInit"), roughnessMapInit);

            matInstances = LoadMaterialDefault();

            instancing_shader.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(instancing_shader, "mvp");
            instancing_shader.locs[SHADER_LOC_VECTOR_VIEW] = GetShaderLocation(instancing_shader, "viewPos");
            instancing_shader.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocationAttrib(instancing_shader, "instanceTransform");

            model.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = {
                static_cast<unsigned char>(surface_material.color.x * 255),
                static_cast<unsigned char>(surface_material.color.y * 255),
                static_cast<unsigned char>(surface_material.color.z * 255),
                static_cast<unsigned char>(surface_material.color.w * 255)
            };

            DrawMeshInstanced(model.meshes[0], model.materials[0], transforms, instances.size());
        }
        else
        {

            if (hasModel()) {
                if (!inFrustum()) {
                    return; // Early return if not in the frustum
                }
            }
            
            if (hasModel())
            {
                Matrix transformMatrix = MatrixIdentity();
                transformMatrix = MatrixScale(scale.x, scale.y, scale.z);
                transformMatrix = MatrixMultiply(transformMatrix, MatrixRotateXYZ(rotation));
                transformMatrix = MatrixMultiply(transformMatrix, MatrixTranslate(position.x, position.y, position.z));

                if (model.meshes != nullptr)
                    bounds = GetMeshBoundingBox(model.meshes[0]);
                
                bounds.min = Vector3Transform(bounds.min, transformMatrix);
                bounds.max = Vector3Transform(bounds.max, transformMatrix);
            }


            PassSurfaceMaterials();
            
            ReloadTextures();
            glUseProgram((GLuint)shader.id);

            bool normalMapInit = !normal_texture_path.empty();
            glUniform1i(glGetUniformLocation((GLuint)shader.id, "normalMapInit"), normalMapInit);

            bool roughnessMapInit = !roughness_texture_path.empty();
            glUniform1i(glGetUniformLocation((GLuint)shader.id, "roughnessMapInit"), roughnessMapInit);

            DrawModelEx(
                model, 
                position, 
                rotation, 
                GetExtremeValue(rotation), 
                scale, 
                (Color) {
                    static_cast<unsigned char>(surface_material.color.x * 255),
                    static_cast<unsigned char>(surface_material.color.y * 255),
                    static_cast<unsigned char>(surface_material.color.z * 255),
                    static_cast<unsigned char>(surface_material.color.w * 255)
                }
            );
        }
    }

};


