cmake_minimum_required(VERSION 3.16)

# The game rules live outside the firmware so the tests and the browser
# version share them.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../game")

# Fixed version: keeps the build independent of the git checkout.
set(PROJECT_VER "0.1.0")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(mix-n-smash)
