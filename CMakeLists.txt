cmake_minimum_required(VERSION 3.16)

project(gchd
	VERSION 0.2.0
	DESCRIPTION "Userspace driver for the Elgato Game Capture HD"
	HOMEPAGE_URL "https://github.com/eddyv73/elgato-gchd"
	LANGUAGES CXX)

# Warnings compartidos por todos los targets.
#
# Las tres supresiones son deuda heredada, no una decisión: silencian 27
# unused-parameter, 27 reorder y los missing-field-initializers del árbol
# original. Se retiran en la Fase 1, junto con -Werror. No añadir más.
add_library(gchd_warnings INTERFACE)
target_compile_options(gchd_warnings INTERFACE
	-Wall
	-Wextra
	-Wno-unused-parameter
	-Wno-reorder
	-Wno-missing-field-initializers)

add_subdirectory(src)

# La GUI es un stub vacío de Qt Designer (47 LOC) y no está operativa.
# Pendiente de decisión: eliminar o retomar. No construir mientras tanto.
# add_subdirectory(src/gui)
