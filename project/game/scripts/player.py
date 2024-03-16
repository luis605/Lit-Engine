import math

# Constants
VELOCITY = 40
SENSITIVITY = 0.3
ENTITY_DISTANCE = 5
JUMP_FORCE = 100
FOV_FORWARD = 80
FOV_NORMAL = 60
FOV_BACKWARD = 40

# Initial values
yaw, pitch = 0, 0
grounded = False
entity.visible = True


#LockMouse()

# Helper functions
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
    
def update_camera_position():
    global yaw, pitch
    front = spherical_to_cartesian(ENTITY_DISTANCE, -yaw, -pitch)
    camera.position = entity.position + front
    camera.look_at = entity.position
    camera.up = Vector3(0, 1, 0)

def change_gravity():
    GRAVITY_STRENGTH = 9.8
    center_point = Vector3(0, 0, 0)
    direction_to_center = center_point - entity.position

    physics.gravity = direction_to_center



def check_ground():
    global grounded
    halfScale = entity.scale.y / 2
    ray = Raycast(entity.position - Vector3(0, halfScale - 0.1, 0), Vector3(0, -1, 0), ignore=[entity])
    grounded = ray.hit and ray.distance < 0.15  # floating pointers margin

def set_entity_rotation():
    global yaw
    front = get_camera_direction()
    entity_rotation_yaw = math.degrees(math.atan2(front.z, front.x)) + 90
    entity.rotation = Vector3(0, -entity_rotation_yaw, 0)

def handle_movement():
    global yaw, pitch, grounded

    direction = get_camera_direction()

    if IsKeyDown(KeyboardKey.KEY_W):
        entity.applyImpulse(direction * time.dt * VELOCITY)
    if IsKeyDown(KeyboardKey.KEY_S):
        entity.applyImpulse(camera.back * time.dt * VELOCITY)
    if IsKeyDown(KeyboardKey.KEY_A):
        entity.applyImpulse(camera.left * time.dt * VELOCITY)
    if IsKeyDown(KeyboardKey.KEY_D):
        entity.applyImpulse(camera.right * time.dt * VELOCITY)
    
    if IsKeyPressed(KeyboardKey.KEY_SPACE) and grounded:
        entity.applyImpulse(Vector3(0, JUMP_FORCE, 0))

    update_camera_fovy()

def update():
    handle_movement()
    update_camera_rotation()
    update_camera_position()
    check_ground()
    set_entity_rotation()
    change_gravity()
















