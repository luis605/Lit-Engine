entity.name = str(entity.x)
print("Entity name:", entity.name, "\n")

if (IsKeyDown(KeyboardKey.KEY_A)):
    entity.z += 1

if (IsKeyDown(KeyboardKey.KEY_D)):
    entity.z -= 1

if (IsKeyDown(KeyboardKey.KEY_W)):
    entity.x -= 1

if (IsKeyDown(KeyboardKey.KEY_S)):
    entity.x += 1

if (IsKeyDown(KeyboardKey.KEY_E)):
    entity.y += 1

if (IsKeyDown(KeyboardKey.KEY_R)):
    entity.y -= 1
