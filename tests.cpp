#define GAME_SHIPPING
#include "include_all_tests.h"

const char* redColor = "\033[1;31m";
const char* yellowColor = "\033[1;33m";
const char* greenColor = "\033[1;32m";
const char* resetColor = "\033[0m";

void printRed(const std::string& text) {
    std::cout << redColor << text << resetColor << std::endl;
}

void printYellow(const std::string& text) {
    std::cout << yellowColor << text << resetColor << std::endl;
}

void printGreen(const std::string& text) {
    std::cout << greenColor << text << resetColor << std::endl;
}


void AddEntity(
	bool is_create_entity_a_child = is_create_entity_a_child,
	const char* model_path = "assets/models/tree.obj",
	Model model = Model(),
	string name = "Unnamed Entity",
	bool isDynamic = false
)
{
	Color entity_color_raylib = GRAY;

	Entity entity_create;
	entity_create.setColor(entity_color_raylib);
	entity_create.setScale(Vector3{1,1,1});
	entity_create.setName(name);
	entity_create.isChild = is_create_entity_a_child;
	entity_create.isDynamic = isDynamic;
	entity_create.setModel(model_path, model);
	entity_create.setShader(shader);


	if (!entities_list_pregame.empty())
	{
		int id = entities_list_pregame.back().id + 1;
		entity_create.id = id;
	}
	else
		entity_create.id = "0";

	if (!is_create_entity_a_child)
	{
		entities_list_pregame.reserve(1);
		entities_list_pregame.emplace_back(entity_create);
	}
	else
	{
		if (selected_game_object_type == "entity")
		{
			if (selected_entity->isChild)
				selected_entity->addChild(entity_create);
			else
				entities_list_pregame.back().addChild(entity_create);
		}
	}

	selected_entity = &entity_create;

	int last_entity_index = entities_list_pregame.size() - 1;
	listViewExActive = last_entity_index;

	create = false;
	is_create_entity_a_child = false;
	canAddEntity = false;
}






int testImGui()
{
    int screenWidth = 0;
    int screenHeight = 0;

    InitWindow(screenWidth, screenHeight, "ImGui Test");
    SetTargetFPS(144);

    // Try-catch block to handle ImGui assertion failure
    try {
        rlImGuiSetup(true);

        {
            BeginDrawing();
            ClearBackground(DARKGRAY);

            rlImGuiBegin();

            if (ImGui::Button("Hi There", ImVec2(100, 100)))
            {
                std::cout << "pressed\n";
            }

            ImGui::Begin("Another Window");

            if (ImGui::Button("Hi There 2", ImVec2(100, 100)))
            {
                std::cout << "pressed\n";
            }

            ImGui::End();
            rlImGuiEnd();
            EndDrawing();
        }

        rlImGuiShutdown();
        CloseWindow();
    } catch (const std::exception& e) {
        std::cout << "ImGui Error: " << e.what() << std::endl;
        return 0; // Test failed due to ImGui error
    }

    return 1; // Test passed
}

int testInputs()
{
    int screenWidth = 0;
    int screenHeight = 0;

    InitWindow(screenWidth, screenHeight, "Input Detection Test");
    SetTargetFPS(144);

    // Try-catch block to handle ImGui assertion failure
    try {
        rlImGuiSetup(true);

		bool closeWindow = false;
		int state = 0;
		while (!closeWindow)
        {
            BeginDrawing();
            ClearBackground(DARKGRAY);

            rlImGuiBegin();

			if (state == 0)
			{
				if (ImGui::Button("Hi. Please press me", ImVec2(350, 100)))
				{
					state += 1;
				}
			}
			else if (state == 1)
			{
				ImGui::SetNextWindowSize(ImVec2(400, 200));
				ImGui::Begin("Another Window");

				ImGui::Text("Now Press SPACE");

				if (IsKeyPressed(KEY_SPACE))
				{
					state += 1;
				}

				ImGui::End();
			}
			else if (state == 2)
			{
				ImGui::SetNextWindowSize(ImVec2(400, 200));
				ImGui::Begin("Yet Another Window");

				ImGui::Text("Now Press your left mouse button");

				if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
				{
					state += 1;
				}

				ImGui::End();
			}
			else if (state == 3)
			{
				closeWindow = true;
			}

            rlImGuiEnd();
            EndDrawing();
        }

        rlImGuiShutdown();
        CloseWindow();
    } catch (const std::exception& e) {
        std::cout << "ImGui Error: " << e.what() << std::endl;
        return 0; // Test failed due to ImGui error
    }

    return 1; // Test passed
}

int testPhysics()
{
    int screenWidth = 0;
    int screenHeight = 0;

    InitWindow(screenWidth, screenHeight, "Physics and Collisions Test");
    SetTargetFPS(144);

    try {

		bool closeWindow = false;
		int state = 0;
		SetupPhysicsWorld();

		AddEntity(false, "", LoadModelFromMesh(GenMeshCube(1,1,1)), "Entity 1", true);
		
		float time;
		while (!closeWindow || !WindowShouldClose())
        {
            BeginDrawing();
            ClearBackground(DARKGRAY);
			BeginMode3D(camera);

			if (state == 0)
			{
				dynamicsWorld->stepSimulation(GetFrameTime(), 10);

				time += GetFrameTime();
				if (time > 5)
				{
					return 0;
				}

				for (Entity& entity : entities_list_pregame)
				{
					if (entity.name != "Entity 1") continue;

					entity.calc_physics = true;
					entity.running_first_time = true;
					entity.render();

					
					if (entity.isDynamic)
					{
						if (entity.position.y < -10)
						{
							state += 1;
							AddEntity(false, "", LoadModelFromMesh(GenMeshCube(1, 1, 1)), "Entity 2", false);
							entities_list.assign(entities_list_pregame.begin(), entities_list_pregame.end());
						}
					}
				}
			}
			else if (state == 1)
			{
				UpdateCamera(&camera, CAMERA_FREE);
				for (Entity& entity : entities_list)
				{
					entity.makePhysicsStatic();
					entity.render();

					if (entity.name == "Entity 1")
					{
						entity.color = RED;
						entity.position.y = 10;
						if (raycast(entity.position, Vector3{ 0, -1, 0 }, true).hit)
						{
							state++;
						}
					}
				}
			}
			else if (state == 2)
			{
				state++;
			}
			else if (state == 3)
			{
				closeWindow = true;
			}
			
			EndMode3D();
            EndDrawing();
        }

        CloseWindow();
    } catch (const std::exception& e) {
        std::cout << "ImGui Error: " << e.what() << std::endl;
        return 0; // Test failed due to ImGui error
    }

    return 1; // Test passed
}


int main()
{
    printGreen("[ ####  Starting Test  #### ]\n");
    SetTraceLogLevel(LOG_FATAL);

	InitWindow(0, 0, "Setup");
    shader = LoadShader("Engine/Lighting/shaders/lighting_vertex.glsl", "Engine/Lighting/shaders/lighting_fragment.glsl");
    instancing_shader = LoadShader("Engine/Lighting/shaders/instancing_lighting_vertex.glsl", "Engine/Lighting/shaders/lighting_fragment.glsl");
    InitLighting();
	CloseWindow();
	printYellow("Tests Ready\nStarting...");

    if (testImGui())
        printGreen("Test 1 PASSED - [ ImGui ]");
    else
        printRed("Test 1 FAILED - [ ImGui ]");

/*
    if (testInputs())
        printGreen("Test 2 PASSED - [ Input Handling ]");
    else
        printRed("Test 2 FAILED - [ Input Handling ]");

*/
    if (testPhysics())
        printGreen("Test 3 PASSED - [ Physics && Collisions ]");
    else
        printRed("Test 3 FAILED - [ Physics && Collisions ]");



    return 0;
}