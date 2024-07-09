def update():
	if IsKeyDown(KeyboardKey.KEY_W):
		entity.position.x += 50 * time.dt
	if IsKeyDown(KeyboardKey.KEY_S):
		entity.position.x -= 50 * time.dt
	if IsKeyDown(KeyboardKey.KEY_A):
		entity.position.z += 50 * time.dt
	if IsKeyDown(KeyboardKey.KEY_D):
		entity.position.z -= 50 * time.dt

	camera.position = entity.position + Vector3(3,3,3)
	camera.look_at = entity.position




