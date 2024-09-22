import random
import os

def initPlugin():
    print("Hello, Plugins!")

def updatePlugin():
    initWindow("Plugin", 200, 200, True)

    if IsKeyDown(Keys.KEY_A):
        drawText("Hello, World!", -1, -1, Vector3(200, 255, 0))

    if IsKeyDown(Keys.KEY_S):
        drawText("Key S is now pressed!", -1, -1, Vector3(255, 0, 0))

    if (drawButton("Change Skybox", -1, -1, -1, -1, Vector3(-1, -1, -1), Vector3(255, 255, 0))):
        filesList = os.listdir(os.path.dirname(os.path.realpath(__file__)) + "/skybox/")
        nFiles = len(filesList)

        if (nFiles > 0):
            setSkybox("skybox/" + filesList[random.randint(0, nFiles - 1)])

    closeWindow()