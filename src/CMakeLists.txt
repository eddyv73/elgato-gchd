find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)

find_package(Threads REQUIRED)
include(GNUInstallDirs)

# Lista explícita a propósito: el AUX_SOURCE_DIRECTORY anterior resolvía por
# globbing y no reconfiguraba al añadir archivos, rompiendo builds incrementales.
add_executable(${PROJECT_NAME}
	disk.cpp
	fifo.cpp
	gchd.cpp
	main.cpp
	process.cpp
	socket.cpp
	streamer.cpp
	utility.cpp
	gchd/commands.cpp
	gchd/configure.cpp
	gchd/configure_component.cpp
	gchd/configure_composite.cpp
	gchd/configure_hdmi.cpp
	gchd/psi_data.cpp
	gchd/psi_descriptors.cpp
	gchd/psi_pat.cpp
	gchd/psi_pmt.cpp
	gchd/psi_sit.cpp
	gchd/psi_syntax.cpp
	gchd/psi_table_header.cpp
	gchd/settings.cpp
	gchd/transcoder.cpp)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)

# Varias cabeceras se incluyen con <> (p.ej. <gchd.hpp> desde streamer.hpp),
# así que src/ tiene que estar en la ruta de inclusión.
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# PkgConfig::LIBUSB aporta también los include dirs: el árbol anterior sólo
# funcionaba porque /usr/include/libusb-1.0 está en la ruta por defecto en
# Linux, y fallaba con libusb instalado en un prefijo distinto (p.ej. Homebrew).
# El enlace explícito a stdc++ se elimina: lo aporta el driver de C++.
target_link_libraries(${PROJECT_NAME}
	PRIVATE
		PkgConfig::LIBUSB
		Threads::Threads
		gchd_warnings)

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
