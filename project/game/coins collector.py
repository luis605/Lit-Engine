import math

velocity = 10.0
entity.name = str(entity.position.x)
can_move_forward = True
can_move_back = True
can_move_left = True
can_move_right = True

trigger_distance = 1.5
gravity = 9.8
can_fall = True

print("GAME")
total_duration = 8.0 
elapsed_time = 0.0001

target_position = Vector3(
    entity.position.x + 3,
    entity.position.y,
    entity.position.z
    )

step = 0

yaw = 0.0
pitch = 0.0

def update():
    global velocity, elapsed_time, total_duration, can_move_forward, can_move_back, can_move_left, can_move_right, can_fall, gravity, step, yaw, pitch

    camera_direction = camera.front
    camera_direction.y = 0  # Keep movement in the horizontal plane

    DeltaTimeVec3 = Vector3(time.dt, time.dt, time.dt)

    if IsKeyDown(KeyboardKey.KEY_W) and can_move_forward:
        print("Forward")
        entity.applyImpulse(camera_direction)

    if IsKeyDown(KeyboardKey.KEY_S) and can_move_back:
        print("Back")
        entity.applyImpulse(Vector3Subtract(Vector3(0,0,0), camera_direction))

    if IsKeyDown(KeyboardKey.KEY_A):
        print("Left")
        right = Vector3CrossProduct(camera_direction, camera.up)
        entity.applyImpulse(Vector3Subtract(Vector3(0,0,0), right))


    if (IsKeyDown(KeyboardKey.KEY_D) and can_move_right):
        print("Right")
        right = Vector3CrossProduct(camera_direction, camera.up)
        entity.applyImpulse(right)

    entity.print_position()
    # Jumping behavior (example: press spacebar to jump)
    if IsKeyDown(KeyboardKey.KEY_SPACE):
        entity.applyImpulse(Vector3(0, 0.5, 0))  # Adjust the value for the desired jump height



    # Camera controls
    camera.position = Vector3(entity.position.x, entity.position.y, entity.position.z)
    camera.target = Vector3(entity.position.x + 0.1, entity.position.y, entity.position.z)

    # Mouse rotation
    sensitivity = 0.3
    yaw -= GetMouseMovement().x * sensitivity
    pitch += GetMouseMovement().y * sensitivity
    camera.target = Vector3(entity.position.x, entity.position.y, entity.position.z)
    camera.target.x += math.sin(math.radians(yaw))
    camera.target.y += pitch
    camera.target.z += math.cos(math.radians(yaw))
    camera.up = Vector3(0, 1, 0)



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


















