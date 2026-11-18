find_package(Qt${QT_DEFAULT_MAJOR_VERSION} COMPONENTS Core REQUIRED)
add_library(desktop_settings STATIC KeyboardSettings.cpp KeyboardSettings.h)
target_link_libraries(desktop_settings PUBLIC Qt${QT_DEFAULT_MAJOR_VERSION}::Core)
target_include_directories(desktop_settings PUBLIC ..)
