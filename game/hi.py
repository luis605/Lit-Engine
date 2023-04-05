import collisions_module
import camera_module

print("Hello World")

velocity = .2
entity.name = str(entity.position.x)

if (IsKeyDown(KeyboardKey.KEY_A)):
    entity.position.z += velocity

if (IsKeyDown(KeyboardKey.KEY_D)):
    entity.position.z -= velocity

if (IsKeyDown(KeyboardKey.KEY_W)):
    entity.position.x -= velocity

if (IsKeyDown(KeyboardKey.KEY_S)):
    entity.position.x += velocity

if (IsKeyDown(KeyboardKey.KEY_E)):
    entity.scale.y += velocity

if (IsKeyDown(KeyboardKey.KEY_R)):
    entity.scale.y -= velocity

if (IsKeyDown(KeyboardKey.KEY_W) and IsKeyDown(KeyboardKey.KEY_LEFT_SHIFT)):
    entity.position.x -= 6


camera.position = collisions_module.Vector3(entity.position.x - 10, entity.position.y + 2, entity.position.z)
camera.target = collisions_module.Vector3(entity.position.x, entity.position.y, entity.position.z)

if (raycast(collisions_module.Vector3(-10, -6, -6), collisions_module.Vector3(1, 0, 0), True)):
    print("you lose")
#    entity.visible