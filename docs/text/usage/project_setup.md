# Sample project setup

Refer to the [sample project](https://github.com/jakildev/irreden-engine/templates/project) for project template.


*this should log to the terminal when someone successfully runs the default project, and then they can go change stuff.*
1. Rename the "your-project-here" directory to fit the name of your project.
2. Edit the top-level CMakeLists.txt file to use your new directory name.
3. Make your game in world.hpp, world.cpp, and (potentially) main.cpp
    a.  Add more files as necessary.


### File structure
- /project-directory
    - CMakeLists.txt
    - /irreden-engine
        - CMakeLists.txt
        - ...
    - /your-project
        - CMakeLists.txt
        - ...
    - /build (created during build process)