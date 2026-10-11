include(FetchContent)

FetchContent_Declare(Catch2
	GIT_REPOSITORY https://github.com/catchorg/Catch2.git
	GIT_TAG v3.9.1
	GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(Catch2)

add_executable(gchd_tests
	test_utility.cpp
	test_settings.cpp
	test_psi.cpp)

target_link_libraries(gchd_tests
	PRIVATE
		gchd_core
		Catch2::Catch2WithMain
		gchd_warnings)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(gchd_tests)
