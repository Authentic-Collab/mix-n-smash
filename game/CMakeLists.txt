# The game rules as an ESP-IDF component, so the firmware can pull it in with
# EXTRA_COMPONENT_DIRS. The same two files compile on a PC for the tests.
idf_component_register(
    SRCS "game.c"
    INCLUDE_DIRS "."
)
