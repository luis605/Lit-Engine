import math

# Constants
VELOCITY = 100.0
SENSITIVITY = 0.3
DISTANCE_FROM_ENTITY = 5.0
JUMP_FORCE = 100.0

# Initial values
yaw, pitch = 0.0, 0.0
grounded = False
entity.visible = True

movingForwardBackwards = False

# Function to convert spherical coordinates to Cartesian coordinates
def spherical_to_cartesian(radius, yaw, pitch):
    x = radius * math.cos(math.radians(yaw)) * math.cos(math.radians(pitch))
    y = radius * math.sin(math.radians(pitch))
    z = radius * math.sin(math.radians(yaw)) * math.cos(math.radians(pitch))
    return Vector3(x, y, z)

def update():
    global yaw, pitch, grounded

    handle_movement()
    handle_camera_rotation()
    update_camera_position()
    check_ground()
    set_entity_rotation()

def handle_movement():
    global yaw, pitch, movingForwardBackwards

    camera_direction = camera.front
    camera_direction.y = 0

    DeltaTimeVec3 = Vector3(time.dt, time.dt, time.dt)

    # Handle player movement inputs
    if IsKeyDown(KeyboardKey.KEY_W):
        movingForwardBackwards = True
        entity.applyImpulse(camera_direction * DeltaTimeVec3 * VELOCITY)
    elif IsKeyDown(KeyboardKey.KEY_S):
        movingForwardBackwards = True
        entity.applyImpulse(camera.back * DeltaTimeVec3 * VELOCITY)
    elif IsKeyDown(KeyboardKey.KEY_A):
        movingForwardBackwards = False
        entity.applyImpulse(camera.left * DeltaTimeVec3 * VELOCITY)
    elif IsKeyDown(KeyboardKey.KEY_D):
        movingForwardBackwards = False
        entity.applyImpulse(camera.right * DeltaTimeVec3 * VELOCITY)
    elif IsKeyPressed(KeyboardKey.KEY_SPACE) and grounded:
        entity.applyImpulse(Vector3(0, JUMP_FORCE, 0))
    else:
        movingForwardBackwards = False
       
    if (movingForwardBackwards):
        camera.fovy = Lerp(camera.fovy, 80, time.dt)
    else:
        camera.fovy = Lerp(camera.fovy, 60, time.dt)

def handle_camera_rotation():
    global yaw, pitch

    # Handle mouse input for camera rotation
    yaw -= GetMouseMovement().x * SENSITIVITY
    pitch -= GetMouseMovement().y * SENSITIVITY

    # Clamp pitch to avoid camera flipping
    pitch = max(-89.0, min(89.0, pitch))

def update_camera_position():
    global yaw, pitch

    # Calculate the front direction based on yaw and pitch
    front = Vector3(
        math.cos(math.radians(yaw)) * math.cos(math.radians(pitch)),
        math.sin(math.radians(pitch)),
        -math.sin(math.radians(yaw)) * math.cos(math.radians(pitch))
    )

    # Update camera position to rotate around the entity
    camera.position = entity.position + spherical_to_cartesian(DISTANCE_FROM_ENTITY, -yaw, -pitch)
    camera.look_at = entity.position
    camera.up = Vector3(0, 1, 0)

def check_ground():
    global grounded

    ray = Raycast(entity.position, Vector3(0, -1, 0), ignore=[entity])
    if (ray.hit):
        grounded = ray.distance < entity.scale.y / 2 + 0.01
    else:
        grounded = False

def set_entity_rotation():
    # Set entity rotation based on camera direction
    front = Vector3(math.cos(math.radians(yaw)), 0, -math.sin(math.radians(yaw)))
    entity_rotation_yaw = math.degrees(math.atan2(front.z, front.x)) + 90.0
    entity_rotation_pitch = math.degrees(math.asin(front.y))
    entity.rotation = Vector3(0, -entity_rotation_yaw, 0)
