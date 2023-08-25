#include <iostream>

// Define a class called Entity
class Entity {
public:
    // Public member variables
    int anIntExternalThingsToTheClassCanModify;

    // Constructor with a default parameter
    Entity(int initialValue = 0) {
        anIntExternalThingsToTheClassCanModify = initialValue;
    }

    // Public member function
    void PrintValue() {
        std::cout << "anIntExternalThingsToTheClassCanModify: " << anIntExternalThingsToTheClassCanModify << std::endl;
    }


private:
    // Private member variable
    int anIntOnlyThisObjectCanModify = 12;

public:
    // Public member function to modify private member
    void SetPrivateValue(int value) {
        anIntOnlyThisObjectCanModify = value;
    }

    void PrintPrivateValue() {
        std::cout << "Private Value is: " << anIntOnlyThisObjectCanModify << std::endl;
    }


};

int main() {
    // Create an instance of the Entity class
    Entity entity;

    // Access and modify public member variables
    entity.anIntExternalThingsToTheClassCanModify = 42;

    // Call public member functions
    entity.PrintValue(); // Output: anIntExternalThingsToTheClassCanModify: 42

    // Attempt to access private member variables (this will result in an error)
    // entity.anIntOnlyThisObjectCanModify = 99; // This line will cause a compilation error

    // Call a public member function to modify private member
    entity.SetPrivateValue(99);
    entity.PrintPrivateValue(); // Output: Private Value is: 99

    // Let's change the global variable
    entity.anIntExternalThingsToTheClassCanModify = 99999999;
    // Access and print the modified private member variable
    entity.PrintValue(); // Output: anIntExternalThingsToTheClassCanModify: 42

    return 0;
}
