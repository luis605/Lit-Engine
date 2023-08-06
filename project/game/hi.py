velocity = 2
entity.name = str(entity.position.x)
can_move_forward = True
can_move_back = True
can_move_left = True
can_move_right = True
can_move_up = True
can_move_down = True

print("GAME")
total_duration = 8.0 
elapsed_time = 0.0001

target_position  = Vector3(0,1,0)

def update():
    global velocity, elapsed_time, total_duration, can_move_forward, can_move_back, can_move_left, can_move_right, can_move_up, can_move_down

    entity.position = lerp(entity.position, target_position, time.dt)

    if IsKeyDown(KeyboardKey.KEY_W) and can_move_forward:
        target_position.x -= velocity * time.dt
    if IsKeyDown(KeyboardKey.KEY_S) and can_move_back:
        target_position.x += velocity * time.dt
    if IsKeyDown(KeyboardKey.KEY_A) and can_move_left:
        target_position.z += velocity * time.dt
    if IsKeyDown(KeyboardKey.KEY_D) and can_move_right:
        target_position.z -= velocity * time.dt
    if IsKeyDown(KeyboardKey.KEY_E) and can_move_up:
        target_position.y += velocity * time.dt
    if IsKeyDown(KeyboardKey.KEY_Q) and can_move_down:
        target_position.y -= velocity * time.dt

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

    # Front Raycast
    front_raycast = raycast(target_position, Vector3(1, 0, 0), debug=False, ignore=[entity])
    if front_raycast.hit:
        if front_raycast.distance < 0.1:
            can_move_forward = False
        else:
            can_move_forward = True
    else:
        can_move_forward = True

    # Back Raycast
    back_raycast = raycast(target_position, Vector3(-1, 0, 0), debug=False, ignore=[entity])
    if back_raycast.hit:
        if back_raycast.distance < 0.1:
            can_move_back = False
        else:
            can_move_back = True
    else:
        can_move_back = True

    # Left Raycast
    left_raycast = raycast(target_position, Vector3(0, 0, 1), debug=False, ignore=[entity])
    if left_raycast.hit:
        if left_raycast.distance < 0.1:
            can_move_left = False
        else:
            can_move_left = True
    else:
        can_move_left = True

    # Right Raycast
    right_raycast = raycast(target_position, Vector3(0, 0, -1), debug=False, ignore=[entity])
    if right_raycast.hit:
        if right_raycast.distance < 0.1:
            can_move_right = False
        else:
            can_move_right = True
    else:
        can_move_right = True

    # Up Raycast
    up_raycast = raycast(target_position, Vector3(0, 1, 0), debug=False, ignore=[entity])
    if up_raycast.hit:
        if up_raycast.distance < 0.1:
            can_move_up = False
        else:
            can_move_up = True
    else:
        can_move_up = True

    # Down Raycast
    down_raycast = raycast(target_position, Vector3(0, -1, 0), debug=False, ignore=[entity])
    if down_raycast.hit:
        if down_raycast.distance < 0.1:
            can_move_down = False
        else:
            can_move_down = True
    else:
        can_move_down = True

