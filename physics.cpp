#include <raylib.h>
#include <raymath.h>
#include <iostream>

Camera3D camera;

// Physics 3D
float acceleration = 0.0f;
float gravity = 9.0f;
float friction = 0.8f;

float mass = 2.0f;

bool collided()
{
    
    return false;
}

class MotionSimulator {
public:
    Vector3 m_position;
    Vector3 m_velocity;
    Vector3 m_acceleration;
    Vector3 m_force;
    float m_mass;

    MotionSimulator() : m_position({0, 0, 0}), m_velocity({0, 0, 0}), m_acceleration({0, 0, 0}), m_force({0, 0, 0}), m_mass(1.0f) {}

    void setMass(float mass) { m_mass = mass; }

    void applyForce(Vector3 force) {
        m_force = Vector3Add(m_force, force);
    }

    void simulate(float deltaTime) {
        Vector3 massVector = { m_mass, m_mass, m_mass };
        m_acceleration = Vector3Divide(m_force, massVector);
        m_velocity = Vector3Add(m_velocity, Vector3Scale(m_acceleration, deltaTime));
        float speed = Vector3Length(m_velocity);
        if (speed > 0) {
            Vector3 direction = Vector3Normalize(m_velocity);
            float frictionForce = friction * mass * gravity;
            Vector3 frictionVector = Vector3Scale(direction, frictionForce);
            if (Vector3Length(frictionVector) > Vector3Length(m_velocity)) {
                m_velocity = { 0, 0, 0 };
            }
            else {
                Vector3 frictionAcceleration = Vector3Divide(frictionVector, massVector);
                m_velocity = Vector3Add(m_velocity, Vector3Scale(frictionAcceleration, deltaTime));
            }
        }
        m_position = Vector3Add(m_position, Vector3Scale(m_velocity, deltaTime));
        Vector3 forceApplied = Vector3Scale(m_force, deltaTime);
        m_force = Vector3Subtract(m_force, forceApplied);
        std::cout << "Force: (" << m_force.x << "," << m_force.y << "," << m_force.z << ") " << std::endl;
        std::cout << "Velocity: (" << m_velocity.x << "," << m_velocity.y << "," << m_velocity.z << ") " << std::endl;
    }

    Vector3 getPosition() const { return m_position; }

};

MotionSimulator motionSimulator;

Vector3 SimulateGravity(Vector3 position)
{
    position.x -= acceleration*GetFrameTime()*gravity;
    
    float max_acceleration = sqrt(2 * mass * gravity / (1.2 * 1 * 0.05 * friction));
    if (acceleration < max_acceleration)
    {
        acceleration += gravity*GetFrameTime();
    }
    
    return position;
}

int main()
{
    InitWindow(400, 400, "Physics Demo");
    

    camera.position = { 10.0f, 5.0f, 0.0f };
    camera.target = { 0.0f, 0.0f, 0.0f };
    camera.up = { 0.0f, 1.0f, 0.0f }; // Set the up vector to point along the global +Y axis

    Vector3 front = Vector3Subtract(camera.target, camera.position);
    front = Vector3Normalize(front); // Normalize the front vector to ensure it has a length of 1

    camera.fovy = 60.0f;
    camera.projection = CAMERA_PERSPECTIVE;


    Vector3 position = { 0, 0, 0 };

    // Apply force to the MotionSimulator object
    motionSimulator.applyForce((Vector3){4,4,0});

    while (!WindowShouldClose())
    {

        // Update the position of the MotionSimulator object
        motionSimulator.simulate(GetFrameTime());

        // Get the updated position of the MotionSimulator object
        Vector3 position = motionSimulator.getPosition();


        position = SimulateGravity(position);

        // Apply force to the MotionSimulator object
        motionSimulator.applyForce(position);

        // Update the position of the MotionSimulator object
        motionSimulator.simulate(GetFrameTime());

        // Get the updated position of the MotionSimulator object
        position = motionSimulator.getPosition();



        BeginDrawing();
        ClearBackground(DARKGRAY);
            BeginMode3D(camera);
                if (IsKeyPressed(KEY_A))
                {
                    motionSimulator.m_position = { 0, 0, 0 };
                }

                DrawCube(position, 2.0f, 2.0f, 2.0f, RED);

            EndMode3D();
        DrawFPS(GetScreenWidth()*.9, GetScreenHeight()*.1);
        // Finish drawing
        EndDrawing();
    }


    CloseWindow();
}