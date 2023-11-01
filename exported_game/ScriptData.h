#include "../include_all.h"
std::map<std::string, const char*> scriptMap = {
    {"file10", R"(


def update():
	normalFovy = 60.0
	runningFovy = 160.0
	velocity = 3
	camera_direction = camera.front * time.dt * velocity
	camera_direction.y = 0

	if (IsKeyDown(KeyboardKey.KEY_W)):
		camera.fovy = lerp(camera.fovy, runningFovy, time.dt)
		entity.position = camera_direction
	else:
		camera.fovy = lerp(camera.fovy, normalFovy, time.dt)


	camera.pos = Vector3(entity.position.x, entity.position.y, entity.position.z)
	camera.pos.x = -5
	camera.pos.z = 10
	camera.pos.y = 2
	camera.look_at = Vector3(entity.position.x + 0.1, entity.position.y, entity.position.x + 0)













)"},
};
