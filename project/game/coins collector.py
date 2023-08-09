velocity = 10
entity.name = str(entity.position.x)
can_move_forward = True
can_move_back = True
can_move_left = True
can_move_right = True

trigger_distance = 1.5
gravity = 9.8  # You can adjust the gravity value for different falling speeds.
can_fall = True  # Initialize to True to allow the entity to fall from the start.

print("GAME")
total_duration = 8.0 
elapsed_time = 0.0001

target_position = Vector3(
    entity.position.x + 3,
    entity.position.y,
    entity.position.z
    )

step = 0

def update():
    global velocity, elapsed_time, total_duration, can_move_forward, can_move_back, can_move_left, can_move_right, can_fall, gravity, step

    # Check if the entity is grounded before moving horizontally
    
    # Move horizontally based on keyboard input
    if IsKeyDown(KeyboardKey.KEY_W) and can_move_forward:
        entity.applyImpulse(Vector3(-velocity * time.dt, 0, 0))
    if IsKeyDown(KeyboardKey.KEY_S) and can_move_back:
        entity.applyImpulse(Vector3(velocity * time.dt, 0, 0))
    if IsKeyDown(KeyboardKey.KEY_A) and can_move_left:
        entity.applyImpulse(Vector3(0, 0, velocity * time.dt))
    if IsKeyDown(KeyboardKey.KEY_D) and can_move_right:
        entity.applyImpulse(Vector3(0, 0, -velocity * time.dt))

    # Jumping behavior (example: press spacebar to jump)
    if IsKeyDown(KeyboardKey.KEY_SPACE) and not can_fall:
        # Add a vertical velocity to simulate jumping
        entity.applyImpulse(Vector3(0, 5, 0))  # Adjust the value for the desired jump height


    # Update the entity's position using lerp function
    entity.position = lerp(entity.position, target_position, time.dt / 0.18)

    # Update camera position and target
    camera.position = Vector3(target_position.x + 10, target_position.y + 1, target_position.z)
    camera.target = Vector3(entity.position.x, entity.position.y, entity.position.z)

    # Front Raycast
    front_raycast = raycast(target_position, Vector3(1, 0, 0), debug=False, ignore=[entity])
    if front_raycast.hit:
        if front_raycast.distance < trigger_distance:
            can_move_forward = False
        else:
            can_move_forward = True
    else:
        can_move_forward = True

    # Back Raycast
    back_raycast = raycast(target_position, Vector3(-1, 0, 0), debug=False, ignore=[entity])
    if back_raycast.hit:
        if back_raycast.distance < trigger_distance:
            can_move_back = False
        else:
            can_move_back = True
    else:
        can_move_back = True

    # Left Raycast
    left_raycast = raycast(target_position, Vector3(0, 0, 1), debug=False, ignore=[entity])
    if left_raycast.hit:
        if left_raycast.distance < trigger_distance:
            can_move_left = False
        else:
            can_move_left = True
    else:
        can_move_left = True

    # Right Raycast
    right_raycast = raycast(target_position, Vector3(0, 0, -1), debug=False, ignore=[entity])
    if right_raycast.hit:
        if right_raycast.distance < trigger_distance:
            can_move_right = False
        else:
            can_move_right = True
    else:
        can_move_right = True


    down_raycast = raycast(Vector3(target_position.x, target_position.y - 0.5, target_position.z), Vector3(0, -1, 0), debug=False, ignore=[])
    if down_raycast.hit:
        # Compare the distance with the entity's height instead of trigger_distance
        if down_raycast.distance > entity.scale.y * 0.5:
            can_fall = True
        else:
            can_fall = False
    else:
        can_fall = True





