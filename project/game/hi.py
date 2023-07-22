velocity = 2
entity.name = str(entity.position.x)


total_duration = 8.0 
elapsed_time = 0.0001

target_position  = Vector3(0,0,0)
def update():
	global velocity, elapsed_time, total_duration

	entity.position = lerp(entity.position, target_position, time.dt)

	if (IsKeyDown(KeyboardKey.KEY_W)):
	    target_position.x -= velocity * time.dt
	    
	
	if (IsKeyDown(KeyboardKey.KEY_S)):
	    target_position.x += velocity * time.dt
	    print("Backwards")
	    
	if (IsKeyDown(KeyboardKey.KEY_A)):
	    target_position.z += velocity * time.dt
	    print("Left")
	
	if (IsKeyDown(KeyboardKey.KEY_D)):
	    target_position.z -= velocity * time.dt
	    print("Right")
		
	
	if IsKeyDown(KeyboardKey.KEY_LEFT_SHIFT):
	   velocity = 10
	else:
		velocity = 5
	
	if IsKeyDown(KeyboardKey.KEY_P):
		camera.target.x += velocity * time.dt
	elif IsKeyDown(KeyboardKey.KEY_O):
		camera.target.x -= velocity * time.dt
	
	camera.position = Vector3(entity.position.x + 10, entity.position.y + 2, entity.position.z)
	camera.target = Vector3(entity.position.x, entity.position.y, entity.position.z)

	hit_info = raycast(entity.position, Vector3(0, 1, 0), debug=False, ignore=[entity])
	if hit_info.hit:
		hit_info.entity.color.print()






































