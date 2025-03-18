#!/bin/bash

selectedPackageManager="NONE"

# Check for common package managers
if which apt >/dev/null 2>&1; then
    echo "Detected package manager: apt"
    selectedPackageManager="apt"
elif which yum >/dev/null 2>&1; then
    echo "Detected package manager: yum"
    selectedPackageManager="yum"
elif which dnf >/dev/null 2>&1; then
    echo "Detected package manager: dnf"
    selectedPackageManager="dnf"
elif which pacman >/dev/null 2>&1; then
    echo "Detected package manager: pacman"
    selectedPackageManager="pacman"
else
    echo "Detected package manager: Unknown"
fi

if [ "$selectedPackageManager" = "apt" ]; then
    sudo apt install software-properties-common gnupg python3-launchpadlib -y
    sudo add-apt-repository ppa:deadsnakes/nightly -y
    sudo add-apt-repository universe -y
    sudo add-apt-repository multiverse -y
    sudo apt update
    sudo apt install python3.12-dev python3.12 gcc-11 g++-11 libglm-dev make cmake libwayland-dev libxkbcommon-dev -y
    sudo apt-get install libtbb-dev liblzma-dev libglfw3-dev libbullet-dev build-essential cmake xorg-dev libglu1-mesa-dev libavutil-dev libavcodec-dev libavformat-dev libswscale-dev -y
elif [ "$selectedPackageManager" = "yum" ]; then
    sudo yum install epel-release
    sudo yum update
    sudo yum install python3-devel gcc-c++ glm-devel make cmake wayland-devel libxkbcommon-devel
    sudo yum install tbb-devel bullet-devel @development-tools cmake mesa-libGLU-devel ffmpeg-devel
elif [ "$selectedPackageManager" = "dnf" ]; then
    sudo dnf install epel-release
    sudo dnf update
    sudo dnf install python3-devel gcc-c++ glm-devel make cmake wayland-devel libxkbcommon-devel
    sudo dnf install tbb-devel bullet-devel @development-tools cmake mesa-libGLU-devel ffmpeg-devel
elif [ "$selectedPackageManager" = "pacman" ]; then
    sudo pacman -Sy
    sudo pacman -S python python-pip gcc glm make cmake wayland xorg-server xorg-xinit libxkbcommon
    sudo pacman -S onetbb bullet mesa glu ffmpeg
else
    echo "No supported package manager found. Please install the dependencies manually."
fi