#include <vector>
#include <variant>
#include <algorithm>
#include <iostream>
#include <variant>

class Entity {};
class Light {};
class Text {};
class LitButton {};

std::vector<std::variant<Entity*, Light*, Text*, LitButton*>> children;

void printChildren() {
    std::cout << "Children contents: ";
    for (const auto& variant : children) {
        if (std::holds_alternative<Entity*>(variant)) {
            std::cout << "Entity* ";
        } else if (std::holds_alternative<Light*>(variant)) {
            std::cout << "Light* ";
        } else if (std::holds_alternative<Text*>(variant)) {
            std::cout << "Text* ";
        } else if (std::holds_alternative<LitButton*>(variant)) {
            std::cout << "LitButton* ";
        } else {
            std::cout << "Unknown ";
        }
    }
    std::cout << std::endl;
}

void removeLightChild(Light* droppedLight) {
    std::cout << "Before removal, children size: " << children.size() << std::endl;
    printChildren();

    children.erase(
        std::remove_if(children.begin(), children.end(), [&](auto& variant) {
            if (auto ptr = std::get_if<Light*>(&variant)) {
                return *ptr == droppedLight;
            }
            return false;
        }),
        children.end()
    );

    std::cout << "After removal, children size: " << children.size() << std::endl;
    printChildren();
}

int main() {
    Entity* e = new Entity();
    Light* l1 = new Light();
    Light* l2 = new Light();
    Light* droppedLight = new Light();
    Text* t = new Text();
    LitButton* lb = new LitButton();

    children.push_back(e);
    children.push_back(l1);
    children.push_back(t);
    children.push_back(lb);
    children.push_back(droppedLight);
    removeLightChild(droppedLight);

    // Clean up
    delete e;
    delete l1;
    delete l2;  // Making sure the pointer for the droppedLight is cleaned correctly.
    delete t;
    delete lb;

    return 0;
}
