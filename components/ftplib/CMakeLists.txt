set_property(SOURCE "ftplib/src/ftplib.c" APPEND PROPERTY COMPILE_DEFINITIONS __unix__ _REENTRANT)
set(COMPONENT_ADD_INCLUDEDIRS ftplib/src)
set(COMPONENT_SRCS "ftplib/src/ftplib.c")
register_component()
