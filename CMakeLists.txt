cmake_minimum_required(VERSION 3.15)
project(SoundMapping LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Sources partagees (coeur audio + routage), reutilisees par plusieurs cibles.
set(CORE_SOURCES
    src/audio/AudioSessionManager.cpp
    src/audio/EndpointEnumerator.cpp
    src/routing/AudioRouter.cpp
)

# Sources "effets par appli" : capture par processus + EQ + routeur per-app.
set(EFFECT_SOURCES
    src/dsp/ChannelEq.cpp
    src/audio/ProcessCapture.cpp
    src/routing/AppAudioRouter.cpp
)

# --- Application principale unifiee (GUI) : routage + EQ par appli ---
add_executable(SoundMapping WIN32
    src/main.cpp
    src/gui/ControlPanel.cpp
    src/routing/MixEngine.cpp
    src/audio/DefaultDevice.cpp
    ${CORE_SOURCES}
    ${EFFECT_SOURCES}
)
target_compile_definitions(SoundMapping PRIVATE UNICODE _UNICODE)
target_link_libraries(SoundMapping PRIVATE Ole32 Comctl32 Mmdevapi Shell32)

# --- Outil console pour tester le moteur de routage ---
add_executable(RouterTest
    src/tools/router_test.cpp
    ${CORE_SOURCES}
)
target_compile_definitions(RouterTest PRIVATE UNICODE _UNICODE)
target_link_libraries(RouterTest PRIVATE Ole32)

# --- Outil console : route le peripherique VIRTUEL vers une sortie reelle ---
add_executable(VirtualRoute
    src/tools/virtual_route.cpp
    ${CORE_SOURCES}
)
target_compile_definitions(VirtualRoute PRIVATE UNICODE _UNICODE)
target_link_libraries(VirtualRoute PRIVATE Ole32)

# --- Outil console : EQ PAR APPLI (capture par processus, Windows 11) ---
add_executable(AppEq
    src/tools/app_eq.cpp
    src/audio/AudioSessionManager.cpp
    src/audio/EndpointEnumerator.cpp
    ${EFFECT_SOURCES}
)
target_compile_definitions(AppEq PRIVATE UNICODE _UNICODE)
target_link_libraries(AppEq PRIVATE Ole32 Mmdevapi)

if (MSVC)
    # /O2 : optimise vitesse ; binaires natifs legers, aucun runtime requis.
    target_compile_options(SoundMapping PRIVATE /O2 /W4 /permissive-)
    target_compile_options(RouterTest   PRIVATE /O2 /W4 /permissive-)
    target_compile_options(VirtualRoute PRIVATE /O2 /W4 /permissive-)
    target_compile_options(AppEq        PRIVATE /O2 /W4 /permissive-)
endif()
