import math

def rotate_around_point(entity, point, angle):
    # Calculate the relative position of the entity to the point of rotation
    relative_position = entity.position - point

    # Calculate the new rotated position
    x = relative_position.x * math.cos(math.radians(angle)) - relative_position.y * math.sin(math.radians(angle))
    y = relative_position.x * math.sin(math.radians(angle)) + relative_position.y * math.cos(math.radians(angle))
    z = relative_position.x * math.sin(math.radians(angle)) + relative_position.z * math.cos(math.radians(angle))

    # Update the entity's position
    entity.position = Vector3(x, y, z) + point

def update():
    print(entity.position)
    entity.rotation.x += 30 * time.dt
    entity.rotation.y += 30 * time.dt

    camera.pos = Vector3(10, 10, 0)
    camera.look_at = Vector3(0, 0, 0)
    
    rotate_around_point(entity, Vector3(0, 0, 0), float(time.dt * 50))

