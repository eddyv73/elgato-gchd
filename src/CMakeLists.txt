find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)

find_package(Threads REQUIRED)
include(GNUInstallDirs)

# gchd_core: la capa que no toca hardware. Separada en su propia librería para
# que los tests puedan enlazarla sin libusb ni dispositivo. Ningún archivo de
# aquí incluye libusb.h ni gchd.hpp; si eso deja de ser cierto, el target de
# tests deja de compilar, que es exactamente el aviso que queremos.
add_library(gchd_core STATIC
	utility.cpp
	gchd/settings.cpp
	gchd/psi_data.cpp
	gchd/psi_descriptors.cpp
	gchd/psi_pat.cpp
	gchd/psi_pmt.cpp
	gchd/psi_sit.cpp
	gchd/psi_syntax.cpp
	gchd/psi_table_header.cpp)

target_compile_features(gchd_core PUBLIC cxx_std_17)
target_include_directories(gchd_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gchd_core PRIVATE gchd_warnings)

# Lista explícita a propósito: el AUX_SOURCE_DIRECTORY anterior resolvía por
# globbing y no reconfiguraba al añadir archivos, rompiendo builds incrementales.
add_executable(${PROJECT_NAME}
	main.cpp
	gchd.cpp
	disk.cpp
	fifo.cpp
	process.cpp
	socket.cpp
	streamer.cpp
	gchd/commands.cpp
	gchd/configure.cpp
	gchd/configure_component.cpp
	gchd/configure_composite.cpp
	gchd/configure_hdmi.cpp
	gchd/transcoder.cpp)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# PkgConfig::LIBUSB aporta también los include dirs: el árbol anterior sólo
# funcionaba porque /usr/include/libusb-1.0 está en la ruta por defecto en
# Linux, y fallaba con libusb instalado en un prefijo distinto (p.ej. Homebrew).
# El enlace explícito a stdc++ se elimina: lo aporta el driver de C++.
target_link_libraries(${PROJECT_NAME}
	PRIVATE
		gchd_core
		PkgConfig::LIBUSB
		Threads::Threads
		gchd_warnings)

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
