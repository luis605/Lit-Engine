#!/bin/bash

# Function to install Python 3.11 using apt package manager
install_python_with_apt() {
    sudo apt update && sudo apt install -y python3.11
}

# Function to install Python 3.11 using yum package manager
install_python_with_yum() {
    sudo yum update && sudo yum install -y python3.11
}

# Function to install Python 3.11 using dnf package manager (Fedora 22+)
install_python_with_dnf() {
    sudo dnf install -y python3.11
}

# Determine the package manager
if command -v apt &> /dev/null; then
    echo "Using apt package manager"
    install_python_with_apt
elif command -v yum &> /dev/null; then
    echo "Using yum package manager"
    install_python_with_yum
elif command -v dnf &> /dev/null; then
    echo "Using dnf package manager"
    install_python_with_dnf
else
    echo "Unsupported package manager"
    exit 1
fi

echo "Python 3.11 has been installed on your system."

