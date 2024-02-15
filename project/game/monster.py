player = None
for entity in entitiesList:
    if entity.name == "Player":
        player = entity

def update():
    direction = Vector3(player.position.x - entity.position.x,
                            player.position.y - entity.position.y,
                            player.position.z - entity.position.z)
    
    entity.position += (direction / 4) * time.dt
    entity.position.y = 0
    ray = raycast(entity.position, direction, ignore=[entity])
    if (ray.hit and ray.entity == player):
        if (ray.distance < 0.75):
            print("DEAD!")
            player.position = Vector3(0,10,0)




