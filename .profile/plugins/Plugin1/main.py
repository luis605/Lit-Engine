import random
import os

entityAddedFlag   = False
entityRemovedFlag = False
sceneSavedFlag    = False
sceneLoadedFlag   = False
onPlayFlag        = False
onStopFlag        = False

def entityCreated():
    global entityAddedFlag
    print("Entity created!")
    entityAddedFlag = True

def entityRemoved():
    global entityRemovedFlag
    print("Entity removed!")
    entityRemovedFlag = True

def sceneSaved():
    global sceneSavedFlag
    print("Scene Saved!")
    sceneSavedFlag = True

def sceneLoaded():
    global sceneLoadedFlag
    print("Scene Loaded!")
    sceneLoadedFlag = True

def onPlay():
    global onPlayFlag
    print("Experience started!")
    onPlayFlag = True

def onStop():
    global onStopFlag
    print("Experience stopped!")
    onStopFlag = True

def initPlugin():
    print("Hello, Plugins!")
    onEntityCreation("pythonListener", entityCreated)
    onEntityDestruction("pythonListener", entityRemoved)
    onSceneSave("pythonListener", sceneSaved)
    onSceneLoad("pythonListener", sceneLoaded)
    onScenePlay("pythonListener", onPlay)
    onSceneStop("pythonListener", onStop)

def updatePlugin():
    initWindow("Plugin " + str(entityAddedFlag), 200, 200, True)

    if entityAddedFlag:
        drawText("An entity was added!", -1, -1, Vector3(0, 255, 0))

    if entityRemovedFlag:
        drawText("An entity was removed!", -1, -1, Vector3(255, 0, 0))

    if sceneSavedFlag:
        drawText("Scene was saved!", -1, -1, Vector3(0, 255, 0))

    if sceneLoadedFlag:
        drawText("Scene was loaded!", -1, -1, Vector3(0, 0, 255))

    if onPlayFlag:
        drawText("Experience started!", -1, -1, Vector3(0, 255, 0))

    if onStopFlag:
        drawText("Experience stopped!", -1, -1, Vector3(255, 0, 0))

    if (drawButton("Change Skybox", -1, -1, -1, -1, Vector3(-1, -1, -1), Vector3(255, 255, 0))):
        filesList = os.listdir(os.path.dirname(os.path.realpath(__file__)) + "/skybox/")
        nFiles = len(filesList)

        if (nFiles > 0):
            setSkybox("skybox/" + filesList[random.randint(0, nFiles - 1)])

    closeWindow()