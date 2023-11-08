import math

velocity = 10.0
dragging_item = None
hovered_entity = None

target_distance = 5.0  # Adjust the distance at which the object appears in front of the entity
gravity = 9.8

yaw = 0.0
pitch = 0.0

rotation_angle = 45.0  # Angle in degrees for rotation

entity.visible = False
def update():
    global velocity, dragging_item, hovered_entity, yaw, pitch


    camera_direction = camera.front * time.dt * velocity
    camera_direction.y = 0

    DeltaTimeVec3 = Vector3(time.dt, time.dt, time.dt)

    if IsKeyDown(KeyboardKey.KEY_W):
        entity.applyImpulse(camera_direction)

    if IsKeyDown(KeyboardKey.KEY_S):
        entity.applyImpulse(camera.back * DeltaTimeVec3 * velocity)

    if IsKeyDown(KeyboardKey.KEY_A):
        left = camera.left * DeltaTimeVec3 * velocity
        entity.applyImpulse(left)

    if IsKeyDown(KeyboardKey.KEY_D):
        right = camera.right * DeltaTimeVec3 * velocity
        entity.applyImpulse(right)

    if IsKeyDown(KeyboardKey.KEY_SPACE):
        if (entity.position.y < 10):
            entity.applyImpulse(Vector3(0, 0.5, 0))  # Adjust for desired jump height

    camera.pos = Vector3(entity.position.x, entity.position.y-2, entity.position.z)
    camera.look_at = Vector3(entity.position.x + 0.1, entity.position.y, entity.position.z)

    sensitivity = 0.3
    yaw -= GetMouseMovement().x * sensitivity
    pitch -= GetMouseMovement().y * sensitivity

    pitch = max(-89.0, min(89.0, pitch))

    # Calculate the front direction based on yaw and pitch
    front = Vector3(
        math.cos(math.radians(yaw)) * math.cos(math.radians(pitch)),
        math.sin(math.radians(pitch)),
        -math.sin(math.radians(yaw)) * math.cos(math.radians(pitch))
    )

    camera.look_at = camera.pos + front
    camera.up = Vector3(0, 1, 0)



    # camera_raycast = raycast(entity.position, camera_direction, ignore=[entity])

    # if IsKeyDown(KeyboardKey.KEY_F) and camera_raycast.hit and not dragging_item:
    #     hovered_entity = camera_raycast.entity
    #     hovered_entity.makeStatic()
    #     dragging_item = hovered_entity
    #     dragging_item.position = entity.position + front * target_distance
    #     print("Picking up item")

    # if dragging_item:
    #     print("Dragging:", hovered_entity.name)
    #     dragging_item.position = entity.position + front * target_distance
    #     hovered_entity.applyForce(camera_direction * 6.0)

    # if IsKeyDown(KeyboardKey.KEY_G) and dragging_item:
    #     # Stop dragging and make the item dynamic again
    #     dragging_item.makeDynamic()
    #     dragging_item = None
    #     print("Dropping item")

    # if IsKeyDown(KeyboardKey.KEY_X) and dragging_item:
    #     # Rotate the dragged item around the X-axis
    #     dragging_item.rotation = Vector3(rotation_angle, 0, 0)

    # if IsKeyDown(KeyboardKey.KEY_Y) and dragging_item:
    #     # Rotate the dragged item around the Y-axis
    #     dragging_item.rotation = Vector3(0, rotation_angle, 0)

    # if IsKeyDown(KeyboardKey.KEY_Z) and dragging_item:
    #     # Rotate the dragged item around the Z-axis
    #     dragging_item.rotation = Vector3(0, 0, rotation_angle)



