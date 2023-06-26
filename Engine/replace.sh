#!/bin/bash

read -p "Enter the string to find: " find_string
read -p "Enter the string to replace with: " replace_string

find . -type f -exec sed -i "s/$find_string/$replace_string/g" {} +
