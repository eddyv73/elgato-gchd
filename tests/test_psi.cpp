/**
 * Tests de las tablas PSI (MPEG-TS, ISO/IEC 13818-1).
 *
 * Son el mejor objetivo de test del repositorio: salida binaria determinista,
 * verificable contra la norma, y sin ninguna dependencia de hardware.
 *
 * El test más importante de este archivo es el de contrato tamaño/empaquetado.
 * Cada clase PSI implementa calculateSize() y pack() por separado, y pack()
 * escribe avanzando un puntero a mano. Si ambos se desincronizan en un byte,
 * PSI_Data::bytes() dimensiona el vector con calculateSize() y pack() escribe
 * fuera: desbordamiento de heap. Verificar que pack() avanza exactamente
 * calculateSize() bytes detecta esa clase de forma determinista, sin depender
 * de que un sanitizer esté activo.
 */

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include "gchd/psi_pat.hpp"
#include "gchd/psi_pmt.hpp"
#include "gchd/psi_exceptions.hpp"
#include "gchd/psi_syntax.hpp"
#include "gchd/psi_table_header.hpp"

namespace {

// Empaqueta comprobando el contrato: pack() debe avanzar el iterador
// exactamente calculateSize() bytes, ni uno más ni uno menos.
std::vector<uint8_t> packVerificado(PSI_Data &dato) {
	const int tamanoDeclarado = dato.calculateSize();
	REQUIRE(tamanoDeclarado >= 0);

	std::vector<uint8_t> salida(static_cast<size_t>(tamanoDeclarado));
	auto offset = salida.begin();
	dato.pack(salida, offset);

	const auto avanzado = std::distance(salida.begin(), offset);
	INFO("calculateSize() declaró " << tamanoDeclarado
	     << " bytes y pack() avanzó " << avanzado);
	REQUIRE(avanzado == tamanoDeclarado);

	return salida;
}

}  // namespace

TEST_CASE("PAT_Entry se empaqueta según ISO 13818-1", "[psi][pat]") {
	// program_number (16 bits) + 3 bits reservados a 1 + PID (13 bits).
	PAT_Entry entrada(1, 0x1000);

	CHECK(entrada.calculateSize() == 4);

	const auto bytes = packVerificado(entrada);
	REQUIRE(bytes.size() == 4);
	CHECK(bytes[0] == 0x00);  // program_number alto
	CHECK(bytes[1] == 0x01);  // program_number bajo
	CHECK(bytes[2] == 0xf0);  // 111 reservados + PID alto (0x1000 >> 8)
	CHECK(bytes[3] == 0x00);  // PID bajo
}

TEST_CASE("PAT_Entry valida el rango del PID", "[psi][pat]") {
	SECTION("un PID de más de 13 bits es inválido") {
		CHECK_THROWS_AS(PAT_Entry(1, 0x2000), PSI_ValueException);
	}

	SECTION("el rango reservado bajo es inválido") {
		CHECK_THROWS_AS(PAT_Entry(1, 0x000f), PSI_ValueException);
	}

	SECTION("el rango reservado alto es inválido") {
		CHECK_THROWS_AS(PAT_Entry(1, 0x1fff), PSI_ValueException);
	}

	SECTION("los extremos válidos se aceptan") {
		CHECK_NOTHROW(PAT_Entry(1, 0x0010));
		CHECK_NOTHROW(PAT_Entry(1, 0x1ffe));
	}
}

TEST_CASE("PAT_Entry sobrevive un ciclo pack/unpack", "[psi][pat][roundtrip]") {
	PAT_Entry original(0x1234, 0x0abc);
	const auto bytes = packVerificado(original);

	PAT_Entry recuperada;
	auto offset = bytes.cbegin();
	recuperada.unpack(bytes, offset, static_cast<int>(bytes.size()));

	CHECK(recuperada.programNumber_ == 0x1234);
	CHECK(recuperada.pid_ == 0x0abc);
	CHECK(std::distance(bytes.cbegin(), offset) == 4);
}

TEST_CASE("PAT_Entry rechaza un buffer demasiado corto", "[psi][pat]") {
	const std::vector<uint8_t> corto{0x00, 0x01, 0xf0};
	PAT_Entry entrada;
	auto offset = corto.cbegin();
	CHECK_THROWS_AS(entrada.unpack(corto, offset, 3), PSI_FormatException);
}

TEST_CASE("PSI_TableHeader respeta su contrato de tamaño", "[psi][header]") {
	PSI_TableHeader cabecera(TABLE_ID_PAT, true, false);
	const auto bytes = packVerificado(cabecera);

	REQUIRE(bytes.size() >= 1);
	CHECK(bytes[0] == TABLE_ID_PAT);
}

TEST_CASE("PSI_Syntax respeta su contrato de tamaño", "[psi][syntax]") {
	PSI_Syntax sintaxis(0x0001);
	CHECK(sintaxis.calculateSize() == 5);
	const auto bytes = packVerificado(sintaxis);
	REQUIRE(bytes.size() == 5);
	CHECK(bytes[0] == 0x00);  // extension alta
	CHECK(bytes[1] == 0x01);  // extension baja
}

TEST_CASE("PSI_Syntax rechaza una versión que no cabe en 5 bits", "[psi][syntax]") {
	PSI_Syntax sintaxis(0x0001);
	sintaxis.version_ = 0x20;  // 1<<5, un bit de más

	std::vector<uint8_t> salida(static_cast<size_t>(sintaxis.calculateSize()));
	auto offset = salida.begin();
	CHECK_THROWS_AS(sintaxis.pack(salida, offset), PSI_ValueException);
}

TEST_CASE("PAT completa respeta su contrato de tamaño", "[psi][pat]") {
	PAT pat(0x0001);
	pat.addEntry(PAT_Entry(1, 0x1000));

	CHECK(pat.getTransportStreamIdentifier() == 0x0001);
	REQUIRE(pat.getEntries() != nullptr);
	CHECK(pat.getEntries()->size() == 1);

	const auto bytes = packVerificado(pat);
	REQUIRE(bytes.size() > 4);
	CHECK(bytes[0] == TABLE_ID_PAT);
}

TEST_CASE("PAT crece de forma consistente al añadir entradas", "[psi][pat]") {
	PAT pat(0x0001);
	const int base = pat.calculateSize();

	pat.addEntry(PAT_Entry(1, 0x1000));
	const int conUna = pat.calculateSize();
	CHECK(conUna == base + 4);

	pat.addEntry(PAT_Entry(2, 0x1001));
	const int conDos = pat.calculateSize();
	CHECK(conDos == conUna + 4);

	// Y el empaquetado real debe seguir cuadrando con lo declarado.
	const auto bytes = packVerificado(pat);
	CHECK(bytes.size() == static_cast<size_t>(conDos));
}

TEST_CASE("clearEntries vacía la PAT", "[psi][pat]") {
	PAT pat(0x0001);
	pat.addEntry(PAT_Entry(1, 0x1000));
	REQUIRE(pat.getEntries()->size() == 1);

	pat.clearEntries();
	CHECK(pat.getEntries()->empty());
	CHECK_NOTHROW(packVerificado(pat));
}

TEST_CASE("PSI_Data::bytes dimensiona con calculateSize", "[psi][contrato]") {
	// bytes() es la vía que usa el driver. Comprobamos que el vector que
	// produce tiene exactamente el tamaño declarado.
	PAT pat(0x0001);
	pat.addEntry(PAT_Entry(1, 0x1000));

	std::vector<uint8_t> salida;
	pat.bytes(salida);
	CHECK(salida.size() == static_cast<size_t>(pat.calculateSize()));
}
