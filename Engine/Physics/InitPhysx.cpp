#include "../../include_all.h"

void InitPhysx()
{
    static PxDefaultErrorCallback gDefaultErrorCallback;
    static PxDefaultAllocator gDefaultAllocatorCallback;

    physx::PxFoundation *mFoundation = PxCreateFoundation(PX_PHYSICS_VERSION, gDefaultAllocatorCallback,
        gDefaultErrorCallback);
    if(!mFoundation)
        std::cout << "PxCreateFoundation failed" << std::endl;

}