cmake_minimum_required(VERSION 3.16)

project(gchd
	VERSION 0.2.0
	DESCRIPTION "Userspace driver for the Elgato Game Capture HD"
	HOMEPAGE_URL "https://github.com/eddyv73/elgato-gchd"
	LANGUAGES CXX)

# Warnings compartidos por todos los targets.
#
# Las supresiones heredadas (-Wno-unused-parameter, -Wno-reorder,
# -Wno-missing-field-initializers) se retiraron en la Fase 1. No añadir mas:
# un warning nuevo se arregla, no se silencia.
option(GCHD_WERROR "Tratar los warnings como errores" OFF)

add_library(gchd_warnings INTERFACE)
target_compile_options(gchd_warnings INTERFACE
	-Wall
	-Wextra)

if(GCHD_WERROR)
	target_compile_options(gchd_warnings INTERFACE -Werror)
endif()

option(GCHD_BUILD_TESTS "Construir los tests unitarios" ON)
option(GCHD_SANITIZE "Compilar con AddressSanitizer y UndefinedBehaviorSanitizer" OFF)

if(GCHD_SANITIZE)
	# Los sanitizers son el detector real de la clase de bug de serializacion
	# sin cota: una escritura fuera del vector aborta en vez de corromper.
	target_compile_options(gchd_warnings INTERFACE
		-fsanitize=address,undefined -fno-omit-frame-pointer)
	target_link_options(gchd_warnings INTERFACE
		-fsanitize=address,undefined)
endif()

add_subdirectory(src)

if(GCHD_BUILD_TESTS)
	enable_testing()
	add_subdirectory(tests)
endif()

# La GUI es un stub vacío de Qt Designer (47 LOC) y no está operativa.
# Pendiente de decisión: eliminar o retomar. No construir mientras tanto.
# add_subdirectory(src/gui)
