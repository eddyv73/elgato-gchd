/**
 * Tests de Utility: conversión big-endian, troceado de cadenas y parseo de
 * direcciones. Esta capa no toca hardware, así que es verificable por completo
 * sin dispositivo.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "utility.hpp"

TEST_CASE("byteify escribe en big-endian", "[utility][endian]") {
	SECTION("uint16_t") {
		std::array<uint8_t, 2> buffer{};
		Utility::byteify<uint16_t>(buffer.data(), 0x1234);
		CHECK(buffer[0] == 0x12);
		CHECK(buffer[1] == 0x34);
	}

	SECTION("uint32_t") {
		std::array<uint8_t, 4> buffer{};
		Utility::byteify<uint32_t>(buffer.data(), 0xdeadbeef);
		CHECK(buffer[0] == 0xde);
		CHECK(buffer[1] == 0xad);
		CHECK(buffer[2] == 0xbe);
		CHECK(buffer[3] == 0xef);
	}

	SECTION("un límite menor que el tipo descarta los bytes altos") {
		// El contrato documentado es "cut off high bits if limit is too small".
		std::array<uint8_t, 4> buffer{};
		buffer.fill(0xaa);
		Utility::byteify<uint32_t>(buffer.data(), 0xdeadbeef, 2);
		CHECK(buffer[0] == 0xbe);
		CHECK(buffer[1] == 0xef);
		// Fuera del límite no debe escribir nada.
		CHECK(buffer[2] == 0xaa);
		CHECK(buffer[3] == 0xaa);
	}
}

TEST_CASE("debyteify lee en big-endian", "[utility][endian]") {
	SECTION("uint16_t") {
		const std::array<uint8_t, 2> buffer{0x12, 0x34};
		CHECK(Utility::debyteify<uint16_t>(buffer.data()) == 0x1234);
	}

	SECTION("uint32_t") {
		const std::array<uint8_t, 4> buffer{0xde, 0xad, 0xbe, 0xef};
		CHECK(Utility::debyteify<uint32_t>(buffer.data()) == 0xdeadbeef);
	}

	SECTION("un límite menor que el tipo lee sólo esos bytes") {
		// Usado de verdad en configure.cpp y commands.cpp con límite 3.
		const std::array<uint8_t, 4> buffer{0x01, 0x02, 0x03, 0xff};
		CHECK(Utility::debyteify<uint32_t>(buffer.data(), 3) == 0x00010203);
	}
}

TEST_CASE("byteify y debyteify son inversas", "[utility][endian]") {
	// Los valores frontera son los que rompen las conversiones con signo.
	const std::vector<uint32_t> valores{
		0x00000000, 0x00000001, 0x0000ffff, 0x7fffffff,
		0x80000000, 0xfffffffe, 0xffffffff};

	for (const auto esperado : valores) {
		std::array<uint8_t, 4> buffer{};
		Utility::byteify<uint32_t>(buffer.data(), esperado);
		CHECK(Utility::debyteify<uint32_t>(buffer.data()) == esperado);
	}
}

TEST_CASE("findFraction aproxima racionales", "[utility][fraction]") {
	SECTION("valores exactos salen exactos") {
		const auto mitad = Utility::findFraction(0.5, 1000);
		CHECK(static_cast<double>(mitad.num) / mitad.denom == 0.5);

		const auto dos = Utility::findFraction(2.0, 1000);
		CHECK(static_cast<double>(dos.num) / dos.denom == 2.0);
	}

	SECTION("29.97 fps dentro del denominador máximo") {
		// El caso real: NTSC son 30000/1001.
		const auto f = Utility::findFraction(30000.0 / 1001.0, 1001);
		CHECK(f.denom <= 1001u);
		CHECK(f.denom > 0u);
		CHECK_THAT(static_cast<double>(f.num) / f.denom,
			   Catch::Matchers::WithinAbs(30000.0 / 1001.0, 1e-6));
	}

	SECTION("el denominador nunca excede el máximo pedido") {
		for (const double v : {1.0 / 3.0, 23.976, 59.94, 24.0}) {
			const auto f = Utility::findFraction(v, 300);
			CHECK(f.denom <= 300u);
			CHECK(f.denom > 0u);
		}
	}
}

TEST_CASE("split trocea por delimitador", "[utility][string]") {
	CHECK(Utility::split("a:b:c", ':') == std::vector<std::string>{"a", "b", "c"});
	CHECK(Utility::split("solo", ':') == std::vector<std::string>{"solo"});
	// Formato real de bitrate variable: max:average:min, con partes omitibles.
	CHECK(Utility::split("10::2", ':') == std::vector<std::string>{"10", "", "2"});
}

TEST_CASE("trim recorta espacios", "[utility][string]") {
	std::string ambos = "  hola  ";
	CHECK(Utility::trim(ambos) == "hola");

	std::string izquierda = "\t\n hola";
	CHECK(Utility::ltrim(izquierda) == "hola");

	std::string derecha = "hola \t\n";
	CHECK(Utility::rtrim(derecha) == "hola");

	std::string vacia = "   ";
	CHECK(Utility::trim(vacia).empty());
}

TEST_CASE("splitIPAddressAndPort cubre los casos documentados", "[utility][net]") {
	std::string address;
	std::string port;

	SECTION("IPv4 con puerto") {
		REQUIRE(Utility::splitIPAddressAndPort(address, port, "127.0.0.1:1"));
		CHECK(address == "127.0.0.1");
		CHECK(port == "1");
	}

	SECTION("IPv4 entre corchetes, forma opcional") {
		REQUIRE(Utility::splitIPAddressAndPort(address, port, "[127.0.0.1]:1"));
		CHECK(address == "127.0.0.1");
		CHECK(port == "1");
	}

	SECTION("sólo puerto") {
		REQUIRE(Utility::splitIPAddressAndPort(address, port, ":1"));
		CHECK(address.empty());
		CHECK(port == "1");
	}

	SECTION("IPv6 suelto se interpreta como dirección, no como puerto") {
		REQUIRE(Utility::splitIPAddressAndPort(address, port, "::1"));
		CHECK(address == "::1");
		CHECK(port.empty());
	}

	SECTION("IPv6 entre corchetes con puerto") {
		REQUIRE(Utility::splitIPAddressAndPort(address, port, "[::1]:1"));
		CHECK(address == "::1");
		CHECK(port == "1");
	}

	SECTION("basura tras los corchetes es inválida") {
		CHECK_FALSE(Utility::splitIPAddressAndPort(address, port, "[::1]3:1"));
	}

	SECTION("cadena vacía no revienta") {
		// Regresión: la implementación indexa input[0] antes de comprobar
		// si la cadena está vacía.
		CHECK_NOTHROW(Utility::splitIPAddressAndPort(address, port, ""));
	}
}
