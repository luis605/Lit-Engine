def update():
    if IsKeyDown(KeyboardKey.KEY_W):
        entity.position.x += 1.0 * time.dt
    if IsKeyDown(KeyboardKey.KEY_A):
        entity.position.z += 1.0 * time.dt
    if IsKeyDown(KeyboardKey.KEY_Q):
        entity.position.y += 1.0 * time.dt
    if IsKeyDown(KeyboardKey.KEY_SPACE):
        entity.position = Vector3(0,0,0)




