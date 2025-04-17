#include <Engine/Core/UUID.hpp>
#include <stduuid/include/uuid.h>
#include <string>

const std::string GenUUID() {
    std::random_device rd;
    auto seed_data = std::array<int, 6> {};
    std::generate(std::begin(seed_data), std::end(seed_data), std::ref(rd));
    std::seed_seq seq(std::begin(seed_data), std::end(seed_data));
    std::ranlux48_base generator(seq);

    // Create a UUID generator using the provided random number generator.
    uuids::basic_uuid_random_generator<std::ranlux48_base> gen(&generator);

    // Generate a new UUID.
    uuids::uuid id = gen();

    // Return the string representation of the generated UUID.
    return uuids::to_string(id);
}