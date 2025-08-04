FROM mcr.microsoft.com/devcontainers/base:ubuntu

RUN apt-get update && apt-get install -y \
    build-essential \
    flex \
    swig \
    bison \
    ninja-build \
    device-tree-compiler \
    libyaml-dev \
    cmake \
    pkg-config \
    python3 \
    python3-pip \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

RUN pip3 install meson

ARG USERNAME=vscode
ARG GROUPNAME=vscode

# If you are on macOS using Colima with virtiofs or 9p, you may need to update the UID/GID values to match your host system.
# ARG USER_UID=501
# ARG USER_GID=20
# 
# RUN groupmod --gid $USER_GID -o $GROUPNAME \
#     && usermod --uid $USER_UID --gid $USER_GID $USERNAME \
#     && chown -R $USER_UID:$USER_GID /home/$USERNAME
