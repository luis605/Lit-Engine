import random


for i in range(1000):
    pos_x = random.uniform(-250, 250)
    pos_y = 300
    pos_z = random.uniform(-250, 250)
    
    origin = Vector3(pos_x, pos_y, pos_z)
    direction = Vector3(0,-1,0)
    ray = raycast(origin, direction)
    
    if (ray.hit):
        newEntity = Entity(ray.worldPoint, modelPath = "project/game/tree.obj", collider = False)













