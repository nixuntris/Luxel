# Introduction
This is my voxel engine I call "luxel".
It's meant to be a sandbox game, somewhat akin to what John Lin aimed for. The most up to date explanation of the technical details are up on: https://jankysoftware.org/ under voxel rasterizer. 
The general idea is that it's an engine to learn and experiment with different techniques, it's a very messy as of now, and incomplete.
Currently there are 2 primary renderers, raytracing and rasterization. The rasterizer is much more complete and more optimized. While the raytracer is much earlier in development.
(Technically 3, since the water renderer is seperate)

# Installation

On the release tab there is a list of builds, they are prepared bi-weekly(Unless a major change happens which might cause more releases).
Just download the compressed folder, and there is a pre-compiled binary to launch.
All the executables are for windows.

# Usage
First screen will be a small menu that allows you to choose different options for world size from 10x10 to 300x300, they are in chunks not voxel size so (32^3)
The second option is resolution, you can pick from 640x480 to 3840x2160p
Then just press the "Play" button and the world will generate, just wait a bit.

# Known issues
It will not launch on nvidia cards, bindless textures have an odd behavior of having different syntax between AMD and Nvidia. This will be fixed in the future but it requires a pre-processor, which I haven't written yet.
It should launch on intel GPUs, but it's not thoroughly tested so bugs or crashes may occur, I develop on AMD hardware.

# Requirements
An x86 CPU with AVX2 instructions <br>
A modern GPU, must support opengl 4.6 with extensions (For now, an AMD GPU) with at least 4 gb of vram <br>
8GB of ram <br>
Installed Visual C++ Redistributable for Visual Studio 2015 <br>

# Dependencies

I use vcpkg for dependencies. <br>
Raylib 5.5 <br>
Opengl <br>
GLFW <br>
ImGui <br>
OpenMP <br>
Pugixml <br>

It's currently being developed by only me using opengl 4.6 with extensions.


# Building
Install Visual Studio 2022 or later, and make sure to install the C++ development tools.
Install VCPKG, and use it to install the dependencies listed above(keep in mind that raylib has had some major changes recently so newer version might break, but also may not as this project doesn't rely on it that much)
VCPKG by default sets it up for x86 not x86-64


# Controls
WASD - for movement <br>
Mouse for controlling camera rotation <br>
Left and Right button for building and destruction <br>
K to enable the debug GUI, it allows to play around with different options <br>
Scroll wheel spawns physics entities <br>