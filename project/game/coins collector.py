import math

entity.visible = False
# Constants
GROUND_TOLERANCE = 0.1
JUMP_HEIGHT = 7.0
SENSITIVITY = 0.3

# Initialize variables
velocity = 500.0
yaw, pitch = 0.0, 0.0
grounded = False

def update_camera(entity):
    global yaw, pitch

    sensitivity = 0.3
    mouse_movement = GetMouseMovement()

    yaw -= mouse_movement.x * sensitivity
    pitch -= mouse_movement.y * sensitivity

    pitch = max(-89.0, min(89.0, pitch))

    # Calculate the front direction based on yaw and pitch
    front = Vector3(
        math.cos(math.radians(yaw)) * math.cos(math.radians(pitch)),
        math.sin(math.radians(pitch)),
        -math.sin(math.radians(yaw)) * math.cos(math.radians(pitch))
    )

    camera.position = Vector3(entity.position.x, entity.position.y + entity.scale.y / 2, entity.position.z)
    camera.look_at = camera.position + front
    camera.up = Vector3(0, 1, 0)

def apply_movement(entity):
    global grounded

    camera_direction = camera.front * time.dt * velocity
    camera_direction.y = 0

    delta_time_vec3 = Vector3(time.dt, time.dt, time.dt)

    if IsKeyDown(KeyboardKey.KEY_W):
        entity.applyForce(camera_direction)

    if IsKeyDown(KeyboardKey.KEY_S):
        entity.applyForce(camera.back * delta_time_vec3 * velocity)

    if IsKeyDown(KeyboardKey.KEY_A):
        left = camera.left * delta_time_vec3 * velocity
        entity.applyForce(left)

    if IsKeyDown(KeyboardKey.KEY_D):
        right = camera.right * delta_time_vec3 * velocity
        entity.applyForce(right)

    if IsKeyPressed(KeyboardKey.KEY_SPACE):
        if grounded:
            entity.applyImpulse(Vector3(0, JUMP_HEIGHT, 0))

def update():
    global grounded

    apply_movement(entity)

    update_camera(entity)

    distance = raycast(
        entity.position,
        Vector3(0, -1, 0),
        ignore=[entity]
    ).distance

    grounded = distance < entity.scale.y / 2 + GROUND_TOLERANCE

