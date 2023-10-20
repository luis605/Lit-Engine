

def update():
	normalFovy = 60.0
	runningFovy = 160.0
	
	if (IsKeyDown(KeyboardKey.KEY_W)):
		camera.fovy = lerp(camera.fovy, runningFovy, time.dt)
	else:
		camera.fovy = lerp(camera.fovy, normalFovy, time.dt)


	camera.pos = Vector3(entity.position.x, entity.position.y, entity.position.z)
	camera.pos.x = -5
	camera.pos.z = 10
	camera.pos.y = 2
	camera.look_at = Vector3(entity.position.x + 0.1, entity.position.y, entity.position.x + 0)










