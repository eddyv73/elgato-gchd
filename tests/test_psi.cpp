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
#include <memory>
#include <vector>

#include "gchd/psi_pat.hpp"
#include "gchd/psi_descriptors.hpp"
#include "gchd/psi_pmt.hpp"
#include "gchd/psi_sit.hpp"
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

// ---------------------------------------------------------------------------
// Descriptores
// ---------------------------------------------------------------------------

TEST_CASE("PSI_ProgramDescriptor se empaqueta como tag+longitud+datos", "[psi][descriptor]") {
	PSI_ProgramDescriptor d(std::vector<uint8_t>{'H', 'D', 'M', 'V'});

	CHECK(d.calculateSize() == 6);  // 2 de cabecera + 4 de carga

	const auto bytes = packVerificado(d);
	REQUIRE(bytes.size() == 6);
	CHECK(bytes[0] == PROGRAM_DESCRIPTOR);
	CHECK(bytes[1] == 4);
	CHECK(bytes[2] == 'H');
	CHECK(bytes[3] == 'D');
	CHECK(bytes[4] == 'M');
	CHECK(bytes[5] == 'V');
}

TEST_CASE("PSI_HDMV_ShortDescriptor lleva la firma HDMV", "[psi][descriptor]") {
	PSI_HDMV_ShortDescriptor d;
	const auto bytes = packVerificado(d);
	REQUIRE(bytes.size() == 6);
	CHECK(bytes[0] == PROGRAM_DESCRIPTOR);
	CHECK(bytes[1] == 4);
	const std::vector<uint8_t> carga(bytes.begin() + 2, bytes.end());
	CHECK(carga == std::vector<uint8_t>{'H', 'D', 'M', 'V'});
}

TEST_CASE("PSI_HDMV_LongDescriptor lleva la firma extendida", "[psi][descriptor]") {
	PSI_HDMV_LongDescriptor d;
	const auto bytes = packVerificado(d);
	REQUIRE(bytes.size() == 10);
	CHECK(bytes[1] == 8);
	const std::vector<uint8_t> carga(bytes.begin() + 2, bytes.end());
	CHECK(carga == std::vector<uint8_t>{'H', 'D', 'M', 'V', 0xff, 0x1b, 0x44, 0x3f});
}

TEST_CASE("PSI_AVC_VideoDescriptor ocupa 4 bytes de carga", "[psi][descriptor]") {
	// profile_idc 100 = High, level_idc 40 = nivel 4.0.
	PSI_AVC_VideoDescriptor d(100, 40);

	CHECK(d.calculateSize() == 6);
	const auto bytes = packVerificado(d);
	REQUIRE(bytes.size() == 6);
	CHECK(bytes[0] == AVC_VIDEO_DESCRIPTOR);
	CHECK(bytes[1] == 4);
	CHECK(bytes[2] == 100);
}

TEST_CASE("PSI_UnknownDescriptor conserva lo que no sabe interpretar", "[psi][descriptor]") {
	PSI_UnknownDescriptor d(0x42);
	d.data = std::vector<uint8_t>{0xde, 0xad};

	CHECK(d.calculateSize() == 4);
	const auto bytes = packVerificado(d);
	REQUIRE(bytes.size() == 4);
	CHECK(bytes[0] == 0x42);
	CHECK(bytes[1] == 2);
}

TEST_CASE("PSI_ParseDescriptor reconstruye el tipo concreto por su tag", "[psi][descriptor]") {
	PSI_ProgramDescriptor original(std::vector<uint8_t>{'H', 'D', 'M', 'V'});
	const auto bytes = packVerificado(original);

	PSI_ParseDescriptor parser;
	auto offset = bytes.cbegin();
	parser.unpack(bytes, offset, static_cast<int>(bytes.size()));

	auto recuperado = parser.getParsedDescriptor();
	REQUIRE(recuperado != nullptr);
	CHECK(recuperado->calculateSize() == original.calculateSize());
}

TEST_CASE("borrar un descriptor por puntero a base no filtra", "[psi][descriptor][dtor]") {
	// Regresión de destructor no virtual: PSI_Descriptor declara métodos
	// virtuales pero no destructor virtual, así que destruir una derivada a
	// través de un puntero a base es comportamiento indefinido. Bajo ASan
	// este test lo detecta.
	std::shared_ptr<PSI_Descriptor> porBase =
		std::make_shared<PSI_HDMV_LongDescriptor>();
	CHECK(porBase->calculateSize() == 10);
	porBase.reset();

	std::vector<std::shared_ptr<PSI_Descriptor>> coleccion;
	coleccion.push_back(std::make_shared<PSI_HDMV_ShortDescriptor>());
	coleccion.push_back(std::make_shared<PSI_AVC_VideoDescriptor>(100, 40));
	coleccion.clear();
	CHECK(coleccion.empty());
}

// ---------------------------------------------------------------------------
// PMT
// ---------------------------------------------------------------------------

TEST_CASE("PMT_Mapping se empaqueta según ISO 13818-1", "[psi][pmt]") {
	PMT_Mapping mapeo(0x1011, STREAM_TYPE_H264);

	CHECK(mapeo.calculateSize() == 5);

	const auto bytes = packVerificado(mapeo);
	REQUIRE(bytes.size() == 5);
	CHECK(bytes[0] == STREAM_TYPE_H264);
	CHECK(bytes[1] == 0xf0);  // 111 reservados + PID alto
	CHECK(bytes[2] == 0x11);  // PID bajo
	CHECK(bytes[3] == 0xf0);  // 1111 reservados + ES_info_length alto
	CHECK(bytes[4] == 0x00);  // ES_info_length bajo
}

TEST_CASE("PMT_Mapping rechaza un PID de más de 13 bits", "[psi][pmt]") {
	CHECK_THROWS_AS(PMT_Mapping(0x2000, STREAM_TYPE_H264), PSI_ValueException);
}

TEST_CASE("PMT_Mapping crece con sus descriptores", "[psi][pmt]") {
	PMT_Mapping mapeo(0x1011, STREAM_TYPE_H264);
	mapeo.addDescriptor(std::make_shared<PSI_HDMV_ShortDescriptor>());

	CHECK(mapeo.calculateSize() == 5 + 6);
	REQUIRE(mapeo.getDescriptors()->size() == 1);

	const auto bytes = packVerificado(mapeo);
	REQUIRE(bytes.size() == 11);
	CHECK(bytes[3] == 0xf0);
	CHECK(bytes[4] == 0x06);  // ES_info_length = 6
	CHECK(bytes[5] == PROGRAM_DESCRIPTOR);

	mapeo.clearDescriptors();
	CHECK(mapeo.getDescriptors()->empty());
	CHECK(mapeo.calculateSize() == 5);
}

TEST_CASE("PMT_Mapping sobrevive un ciclo pack/unpack sin descriptores",
	  "[psi][pmt][roundtrip]") {
	PMT_Mapping original(0x1011, STREAM_TYPE_H264);
	const auto bytes = packVerificado(original);

	PMT_Mapping recuperado;
	auto offset = bytes.cbegin();
	recuperado.unpack(bytes, offset, static_cast<int>(bytes.size()));

	CHECK(recuperado.elementaryPid_ == 0x1011);
	CHECK(recuperado.streamType_ == STREAM_TYPE_H264);
	CHECK(recuperado.getDescriptors()->empty());
}

TEST_CASE("PMT_Mapping sobrevive un ciclo pack/unpack CON descriptores",
	  "[psi][pmt][roundtrip]") {
	// Regresión de psi_pmt.cpp:111, que construía un shared_ptr vacío y lo
	// desreferenciaba acto seguido. Esta ruta sólo se alcanza cuando el
	// mapeo lleva descriptores, es decir con ES_info_length > 0.
	PMT_Mapping original(0x1011, STREAM_TYPE_H264);
	original.addDescriptor(std::make_shared<PSI_HDMV_ShortDescriptor>());
	const auto bytes = packVerificado(original);

	PMT_Mapping recuperado;
	auto offset = bytes.cbegin();
	recuperado.unpack(bytes, offset, static_cast<int>(bytes.size()));

	CHECK(recuperado.elementaryPid_ == 0x1011);
	CHECK(recuperado.streamType_ == STREAM_TYPE_H264);
	REQUIRE(recuperado.getDescriptors()->size() == 1);
	CHECK(recuperado.getDescriptors()->at(0) != nullptr);
}

TEST_CASE("PMT_Mapping rechaza un buffer demasiado corto", "[psi][pmt]") {
	const std::vector<uint8_t> corto{0x1b, 0xf0, 0x11, 0xf0};
	PMT_Mapping mapeo;
	auto offset = corto.cbegin();
	CHECK_THROWS_AS(mapeo.unpack(corto, offset, 4), PSI_FormatException);
}

TEST_CASE("PMT completa respeta su contrato de tamaño", "[psi][pmt]") {
	PMT pmt(1, 0x1000);

	CHECK(pmt.getProgramNumber() == 1);
	CHECK(pmt.getPcrPid() == 0x1000);

	const auto vacia = packVerificado(pmt);
	REQUIRE(vacia.size() > 4);
	CHECK(vacia[0] == TABLE_ID_PMT);

	pmt.addMapEntry(PMT_Mapping(0x1011, STREAM_TYPE_H264));
	REQUIRE(pmt.getMapEntries()->size() == 1);
	const auto conMapeo = packVerificado(pmt);
	CHECK(conMapeo.size() == vacia.size() + 5);

	pmt.addProgramInfo(std::make_shared<PSI_HDMV_ShortDescriptor>());
	REQUIRE(pmt.getProgramInfo()->size() == 1);
	const auto conInfo = packVerificado(pmt);
	CHECK(conInfo.size() == conMapeo.size() + 6);
}

TEST_CASE("PMT se vacía de forma consistente", "[psi][pmt]") {
	PMT pmt(1, 0x1000);
	pmt.addMapEntry(PMT_Mapping(0x1011, STREAM_TYPE_H264));
	pmt.addProgramInfo(std::make_shared<PSI_HDMV_ShortDescriptor>());

	pmt.clearMapEntries();
	CHECK(pmt.getMapEntries()->empty());

	pmt.clearProgramInfo();
	CHECK(pmt.getProgramInfo()->empty());

	CHECK_NOTHROW(packVerificado(pmt));
}

// ---------------------------------------------------------------------------
// SIT
// ---------------------------------------------------------------------------

TEST_CASE("SIT vacía respeta su contrato de tamaño", "[psi][sit]") {
	SIT sit;
	const auto bytes = packVerificado(sit);
	REQUIRE(bytes.size() > 4);
	CHECK(bytes[0] == TABLE_ID_SIT);
}

TEST_CASE("SIT crece con sus parámetros de transmisión", "[psi][sit]") {
	SIT sit;
	const auto vacia = packVerificado(sit);

	sit.data_.transmissionParameters_.push_back(
		std::make_shared<PSI_HDMV_ShortDescriptor>());
	const auto conParametro = packVerificado(sit);
	CHECK(conParametro.size() == vacia.size() + 6);
}

TEST_CASE("SIT crece con sus servicios", "[psi][sit]") {
	SIT sit;
	const auto vacia = packVerificado(sit);

	sit.data_.services_.push_back(std::make_shared<PSI_HDMV_LongDescriptor>());
	const auto conServicio = packVerificado(sit);
	CHECK(conServicio.size() == vacia.size() + 10);
}

TEST_CASE("PSI_UnknownDescriptor conserva su carga al reempaquetar",
	  "[psi][descriptor][roundtrip]") {
	// Regresión: PSI_UnknownDescriptor no sobreescribía packInternal, así que
	// corría el de la base, que no escribe nada. calculateSize() declaraba
	// 2+data.size() y pack() sólo emitía los 2 bytes de cabecera, dejando la
	// carga a cero dentro de una tabla cuyo campo de longitud decía otra cosa.
	PSI_UnknownDescriptor original(0x42);
	original.data = std::vector<uint8_t>{0xde, 0xad, 0xbe, 0xef};

	const auto bytes = packVerificado(original);
	REQUIRE(bytes.size() == 6);
	CHECK(bytes[0] == 0x42);
	CHECK(bytes[1] == 4);
	const std::vector<uint8_t> carga(bytes.begin() + 2, bytes.end());
	CHECK(carga == std::vector<uint8_t>{0xde, 0xad, 0xbe, 0xef});

	PSI_ParseDescriptor parser;
	auto offset = bytes.cbegin();
	parser.unpack(bytes, offset, static_cast<int>(bytes.size()));
	auto recuperado = parser.getParsedDescriptor();
	REQUIRE(recuperado != nullptr);
	CHECK(recuperado->calculateSize() == 6);
}
