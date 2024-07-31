import random

def generate_maze(maze_size, path_width):
    wall_height = 10
    walls = []

    grid = [['W' for _ in range(maze_size)] for _ in range(maze_size)]

    def carve_passages_from(cx, cy):
        directions = [(0, path_width), (path_width, 0), (0, -path_width), (-path_width, 0)]
        random.shuffle(directions)
        
        for direction in directions:
            nx, ny = cx + direction[0] * 2, cy + direction[1] * 2
            if 0 <= nx < maze_size and 0 <= ny < maze_size and grid[ny][nx] == 'W':
                for i in range(path_width):
                    for j in range(path_width):
                        grid[cy + direction[1] + i][cx + direction[0] + j] = ' '
                        grid[ny + i][nx + j] = ' '
                carve_passages_from(nx, ny)

    # Initialize start point
    start_x, start_y = path_width, path_width
    for i in range(path_width):
        for j in range(path_width):
            grid[start_y + i][start_x + j] = ' '
    carve_passages_from(start_x, start_y)

    # Ensure path from start to end
    start = (1, 0)
    end = (maze_size - 2, maze_size - 1)
    grid[start[1]][start[0]] = ' '
    grid[end[1]][end[0]] = ' '

    # Add outer walls to the walls list
    Entity(Vector3(0, 0, maze_size * 0.5), scale=Vector3(1, wall_height, maze_size), name=f"OuterWall_left", collider=CollisionShape.Box)
    Entity(Vector3(maze_size, 0, maze_size * 0.5), scale=Vector3(1, wall_height, maze_size), name=f"OuterWall_right", collider=CollisionShape.Box)
    Entity(Vector3(maze_size * 0.5, 0, 0), scale=Vector3(maze_size, wall_height, 1), name=f"OuterWall_top", collider=CollisionShape.Box)
    Entity(Vector3(maze_size * 0.5, 0, maze_size), scale=Vector3(maze_size, wall_height, 1), name=f"OuterWall_bottom", collider=CollisionShape.Box)

    def create_entity(x, y, width, height):
        Entity(Vector3(x + width * 0.5 - 0.5, 0, y + height * 0.5 - 0.5), scale=Vector3(width, wall_height, height), name=f"Block_{x}_{y}", collider=CollisionShape.Box)

    def find_and_mark_block(x, y):
        # Calculate the dimensions of the block
        width = 0
        while x + width < maze_size and grid[y][x + width] == 'W':
            width += 1

        height = 0
        valid_height = True
        while y + height < maze_size and valid_height:
            for wx in range(width):
                if grid[y + height][x + wx] != 'W':
                    valid_height = False
                    break
            if valid_height:
                height += 1

        # Mark the block as visited
        for wy in range(height):
            for wx in range(width):
                grid[y + wy][x + wx] = 'V'

        return width, height

    # Create blocks by grouping contiguous 'W' cells
    for y in range(maze_size):
        for x in range(maze_size):
            if grid[y][x] == 'W':
                width, height = find_and_mark_block(x, y)
                create_entity(x, y, width, height)

    return walls

# Example usage
maze_size = 150
path_width = 3
walls = generate_maze(maze_size, path_width)
