
def update():
	entity.rotation.y += 15*time.dt
	print("rotating")
	camera.position = Vector3(entity.position.x + 6, entity.position.y - 0, entity.position.z)
	camera.look_at = Vector3(entity.position.x, entity.position.y, entity.position.z)
