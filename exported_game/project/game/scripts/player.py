import math

# Constants
VELOCITY = 80
SENSITIVITY = 0.3
ENTITY_DISTANCE = 5
JUMP_FORCE = 100
FOV_FORWARD = 80
FOV_NORMAL = 60
FOV_BACKWARD = 40

# Initial values
yaw, pitch = 0, 0
entity.visible = True

LockMouse()

planetsPosition = [Vector3(0,0,0), Vector3( 540, 130, 30)]

def spherical_to_cartesian(radius, yaw, pitch):
    x = radius * math.cos(math.radians(yaw)) * math.cos(math.radians(pitch))
    y = radius * math.sin(math.radians(pitch))
    z = radius * math.sin(math.radians(yaw)) * math.cos(math.radians(pitch))
    return Vector3(x, y, z)

def get_camera_direction():
    direction = camera.front
    return direction

def is_moving_forward_backwards():
    return IsKeyDown(KeyboardKey.KEY_W) or IsKeyDown(KeyboardKey.KEY_S)

def update_camera_fovy():
    if IsKeyDown(KeyboardKey.KEY_W):
        camera.fovy = Lerp(camera.fovy, FOV_FORWARD, time.dt)
    elif IsKeyDown(KeyboardKey.KEY_S):
        camera.fovy = Lerp(camera.fovy, FOV_BACKWARD, time.dt)
    else:
        camera.fovy = Lerp(camera.fovy, FOV_NORMAL, time.dt)

def update_camera_rotation():
    global yaw, pitch
    yaw -= GetMouseMovement().x * SENSITIVITY
    pitch -= GetMouseMovement().y * SENSITIVITY
    pitch = max(-89, min(89, pitch))
    
def update_camera_position():
    global yaw, pitch
    front = spherical_to_cartesian(ENTITY_DISTANCE, -yaw, -pitch)
    camera.position = entity.position + front * 4
    camera.look_at = entity.position

def change_gravity():
    GRAVITY_STRENGTH = 9.8  # Gravitational constant similar to Earth's gravity
    gravity = Vector3(0, 0, 0)  # Initialize gravity vector

    # Initialize minimum distance to a large number
    min_distance = float('inf')
    closest_planet_pos = None

    # Find the closest planet
    for planet_pos in planetsPosition:
        distance = Vector3Distance(entity.position, planet_pos)
        if distance < min_distance:
            min_distance = distance
            closest_planet_pos = planet_pos

    if closest_planet_pos is not None:
        # Calculate the gravity vector as if the player is on the closest planet
        direction_to_planet = closest_planet_pos - entity.position
        direction_to_planet = direction_to_planet.normalized()  # Ensure the direction is a unit vector
        gravity = direction_to_planet * GRAVITY_STRENGTH

    # Set the computed gravity in the physics engine
    physics.gravity = gravity

def handle_movement():
    global yaw, pitch

    direction = get_camera_direction()

    if IsKeyDown(KeyboardKey.KEY_W):
        entity.applyImpulse(direction * time.dt * VELOCITY)
    if IsKeyDown(KeyboardKey.KEY_S):
        entity.applyImpulse(camera.back * time.dt * VELOCITY)
    if IsKeyDown(KeyboardKey.KEY_A):
        entity.applyImpulse(camera.left * time.dt * VELOCITY)
    if IsKeyDown(KeyboardKey.KEY_D):
        entity.applyImpulse(camera.right * time.dt * VELOCITY)
    
    update_camera_fovy()

def update():
    handle_movement()
    update_camera_rotation()
    update_camera_position()
    change_gravity()

























