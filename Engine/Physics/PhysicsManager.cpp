#include "PhysicsManager.h"

PhysicsManager::PhysicsManager() : foundation(nullptr), physics(nullptr), cooking(nullptr), scene(nullptr), mPvd(nullptr), manager(nullptr), pActor(nullptr) {}

PhysicsManager::~PhysicsManager() {
    //Cleanup();
}

void PhysicsManager::Initialize() 
{

    foundation = PxCreateFoundation(PX_PHYSICS_VERSION, gDefaultAllocatorCallback, gDefaultErrorCallback);
    if (!foundation)
    {
        printf("PxCreateFoundation failed!\n");
        return;
    }

    bool recordMemoryAllocations = true;

    mPvd = PxCreatePvd(*foundation);
    PxPvdTransport* transport = PxDefaultPvdSocketTransportCreate("127.0.O.1", 5425, 10); // PhysX Visual Debugger bind
    mPvd->connect(*transport, PxPvdInstrumentationFlag::eALL);

    physics = PxCreatePhysics(PX_PHYSICS_VERSION, *foundation,
        PxTolerancesScale(), recordMemoryAllocations, mPvd);

    if (!physics)
    {
        printf("PxCreatePhysics failed!\n");
        return;
    }

    cooking = PxCreateCooking(PX_PHYSICS_VERSION, *foundation, PxCookingParams(PxTolerancesScale()));
    if (!cooking)
    {
        printf("PxCreateCooking failed!\n");
        return;
    }

    PxSceneDesc sceneDesc(physics->getTolerancesScale());
    sceneDesc.gravity = m_gravity;
    sceneDesc.cpuDispatcher = PxDefaultCpuDispatcherCreate(1);
    sceneDesc.filterShader = PxDefaultSimulationFilterShader;

    scene = physics->createScene(sceneDesc);
    if (!scene)
    {
        printf("PxCreateScene failed!");
        return;
    }
    scene->setVisualizationParameter(PxVisualizationParameter::eSCALE, 1.0f);
    scene->setVisualizationParameter(PxVisualizationParameter::eACTOR_AXES, 2.0f);
    scene->setVisualizationParameter(PxVisualizationParameter::eCOLLISION_SHAPES, 3.0f);

    manager = PxCreateControllerManager(*scene);

    //cudaContextManagerDesc;
    //cudaContextManager = PxCreateCudaContextManager(*foundation, cudaContextManagerDesc, PxGetProfilerCallback());

    //particleSystem = physics->createPBDParticleSystem(*cudaContextManager, 96);

}

void PhysicsManager::TestParticle()
{
    //const PxReal particleSpacing = 0.2f;
    //const PxReal fluidDensity = 1000.f;
    //const PxReal restOffset = 0.5f * particleSpacing / 0.6f;
    //const PxReal solidRestOffset = restOffset;
    //const PxReal fluidRestOffset = restOffset * 0.6f;
    //const PxReal renderRadius = fluidRestOffset;
    //const PxReal particleMass = fluidDensity * 1.333f * 3.14159f * renderRadius * renderRadius * renderRadius;
    //particleSystem->setRestOffset(restOffset);
    //particleSystem->setContactOffset(restOffset + 0.01f);
    //particleSystem->setParticleContactOffset(PxMax(solidRestOffset + 0.01f, fluidRestOffset / 0.6f));
    //particleSystem->setSolidRestOffset(solidRestOffset);
    //particleSystem->setFluidRestOffset(fluidRestOffset);

    //scene->addParticleSystem(*particleSystem);
}

void PhysicsManager::Cleanup() {
    physics->release();
    foundation->release();
}

void PhysicsManager::SetPlayer(PxRigidActor* playerA)
{
    pActor = playerA;
}

PxControllerManager* PhysicsManager::GetControllerManager()
{
    return manager;
}

void PhysicsManager::Update()
{
    // Step the physics simulation
    scene->simulate(1.0f / 60.0f);
    scene->fetchResults(true);

    // Render the debug lines
    const PxRenderBuffer& rb = scene->getRenderBuffer();
    for (PxU32 i = 0; i < rb.getNbLines(); i++)
    {
        const PxDebugLine& line = rb.getLines()[i];
        DrawLine3D(
            { line.pos0.x, line.pos0.y, line.pos0.z },
            { line.pos1.x, line.pos1.y, line.pos1.z },
            RED);
    }
}


PxShape* PhysicsManager::CreateBoxShape(const PxVec3& dimensions, PxMaterial* material) {
    return physics->createShape(PxBoxGeometry(dimensions), *material);
}

PxRigidDynamic* PhysicsManager::CreateRigidDynamic(PxShape* shape, const PxTransform& transform) {
    PxRigidDynamic* actor = physics->createRigidDynamic(transform);
    actor->attachShape(*shape);
    PxRigidBodyExt::updateMassAndInertia(*actor, 1.0f);
    scene->addActor(*actor);
    return actor;
}

PxRigidStatic* PhysicsManager::CreateRigidStatic(PxShape* shape, const PxTransform& transform)
{
    PxRigidStatic* actor = PxCreateStatic(*physics, transform, *shape);
    if (actor)
    {
        scene->addActor(*actor);
        return actor;
    }

    return nullptr;
}

std::vector<PxShape*> PhysicsManager::CreateMeshShapes(Model model, const PxMaterial* material)
{
    std::vector<PxShape*> shapes;

    for (int i = 0; i < model.meshCount; ++i)
    {
        Mesh mesh = model.meshes[i];

        float* vertices = static_cast<float*>(mesh.vertices);
        unsigned short* indices = mesh.indices;

        PxTriangleMeshDesc meshDesc;
        meshDesc.points.count = mesh.vertexCount;
        meshDesc.points.stride = sizeof(float) * 3;
        meshDesc.points.data = vertices;
        meshDesc.triangles.count = mesh.triangleCount;
        meshDesc.triangles.stride = 3 * sizeof(unsigned short);
        meshDesc.triangles.data = indices;

        PxDefaultMemoryOutputStream writeBuffer;
        PxTriangleMeshCookingResult::Enum result;
        bool status = cooking->cookTriangleMesh(meshDesc, writeBuffer, &result);
        if (!status)
        {
            printf("PHYS : STATUS ERROR \n");
            continue;
        }

        PxDefaultMemoryInputData readBuffer(writeBuffer.getData(), writeBuffer.getSize());
        PxTriangleMesh* triangleMesh = physics->createTriangleMesh(readBuffer);
        if (!triangleMesh)
        {
            continue;
        }

        PxShape* shape = physics->createShape(PxTriangleMeshGeometry(triangleMesh), *material);
        if (!shape)
        {
            printf("PHYS : SHAPE ERROR \n");
            continue;
        }

        shapes.push_back(shape);
    }

    return shapes;
}


PxPhysics* PhysicsManager::GetPhysics() const {
    return physics;
}

PxVec3 PhysicsManager::GetGravity()
{
    return m_gravity;
}