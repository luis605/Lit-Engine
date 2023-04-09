#!/usr/bin/env python3

import re

# Read the header file and extract the enum values
with open('my_enum.h') as f:
    header_contents = f.read()

pattern = r"enum MyEnum \{(.*?)\};"
match = re.search(pattern, header_contents, re.DOTALL)
enum_values = match.group(1).strip().split("\n")

# Generate the map definition
with open('my_enum_map.cpp', 'w') as f:
    f.write("#include <unordered_map>\n")
    f.write("#include <string>\n\n")
    f.write("enum MyEnum {\n")
    f.write(",\n".join(enum_values))
    f.write("\n};\n\n")
    f.write("const std::unordered_map<std::string, MyEnum> MyEnumMap {\n")
    f.write(",\n".join([f"{{\"{value}\", {value}}}" for value in enum_values]))
    f.write("\n};\n")
