/**
 * Tests de settings: conversión de resoluciones y aritmética de bitrate. Es
 * lógica pura de configuración, verificable sin dispositivo.
 */

#include <catch2/catch_test_macros.hpp>

#include "gchd/settings.hpp"

TEST_CASE("convertResolution: enum a dimensiones", "[settings][resolution]") {
	unsigned h = 0;
	unsigned v = 0;

	convertResolution(h, v, Resolution::NTSC);
	CHECK(h == 720);
	CHECK(v == 480);

	convertResolution(h, v, Resolution::PAL);
	CHECK(h == 720);
	CHECK(v == 576);

	convertResolution(h, v, Resolution::HD720);
	CHECK(h == 1280);
	CHECK(v == 720);

	convertResolution(h, v, Resolution::HD1080);
	CHECK(h == 1920);
	CHECK(v == 1080);

	convertResolution(h, v, Resolution::Unknown);
	CHECK(h == 0);
	CHECK(v == 0);
}

TEST_CASE("convertResolution: dimensiones a enum", "[settings][resolution]") {
	CHECK(convertResolution(1920, 1080) == Resolution::HD1080);
	CHECK(convertResolution(1280, 720) == Resolution::HD720);
	CHECK(convertResolution(720, 576) == Resolution::PAL);
	CHECK(convertResolution(720, 480) == Resolution::NTSC);
}

TEST_CASE("convertResolution rechaza resoluciones no soportadas", "[settings][resolution]") {
	CHECK_THROWS_AS(convertResolution(1024, 768), setting_error);
	CHECK_THROWS_AS(convertResolution(3840, 2160), setting_error);
	CHECK_THROWS_AS(convertResolution(0, 0), setting_error);
}

TEST_CASE("convertResolution es inversa de sí misma", "[settings][resolution]") {
	for (const auto r : {Resolution::NTSC, Resolution::PAL,
			     Resolution::HD720, Resolution::HD1080}) {
		unsigned h = 0;
		unsigned v = 0;
		convertResolution(h, v, r);
		CHECK(convertResolution(h, v) == r);
	}
}

TEST_CASE("InputSettings arranca en estado desconocido", "[settings][input]") {
	InputSettings s;
	CHECK(s.getSource() == InputSource::Unknown);
	CHECK(s.getResolution() == Resolution::Unknown);
	CHECK(s.getScanMode() == ScanMode::Unknown);
	CHECK(s.getColorSpace() == ColorSpace::Unknown);
	CHECK(s.getRefreshRate() == 0.0);  // 0.0 significa autodetectar
	CHECK_FALSE(s.getSDStretch());
}

TEST_CASE("InputSettings conserva lo que se le asigna", "[settings][input]") {
	InputSettings s;

	s.setSource(InputSource::HDMI);
	CHECK(s.getSource() == InputSource::HDMI);

	s.setResolution(Resolution::HD1080);
	CHECK(s.getResolution() == Resolution::HD1080);

	unsigned h = 0;
	unsigned v = 0;
	s.getResolution(h, v);
	CHECK(h == 1920);
	CHECK(v == 1080);

	s.setScanMode(ScanMode::Progressive);
	CHECK(s.getScanMode() == ScanMode::Progressive);

	s.setColorSpace(ColorSpace::YUV);
	CHECK(s.getColorSpace() == ColorSpace::YUV);

	s.setSDStretch(true);
	CHECK(s.getSDStretch());
}

TEST_CASE("TranscoderSettings: bitrate constante mbps a kbps", "[settings][bitrate]") {
	TranscoderSettings t;

	t.setConstantBitRateMbps(10.0f);
	CHECK(t.getBitRateMode() == BitRateMode::Constant);
	CHECK(t.getConstantBitRateMbps() == 10.0f);
	CHECK(t.getConstantBitRateKbps() == 10000u);

	t.setConstantBitRateMbps(1.5f);
	CHECK(t.getConstantBitRateKbps() == 1500u);
}

TEST_CASE("TranscoderSettings: bitrate variable conserva las tres cotas", "[settings][bitrate]") {
	TranscoderSettings t;

	// setVariableBitRateMbps NO cambia el modo: hay que fijarlo aparte, igual
	// que hace main.cpp:546-549. Es un filo del API (fijar las cotas sin
	// cambiar el modo las deja inertes), pero es el contrato actual.
	t.setVariableBitRateMbps(20.0f, 10.0f, 5.0f);
	CHECK(t.getBitRateMode() == BitRateMode::Constant);
	t.setBitRateMode(BitRateMode::Variable);
	CHECK(t.getBitRateMode() == BitRateMode::Variable);

	float max = 0.0f;
	float avg = 0.0f;
	float min = 0.0f;
	t.getVariableBitRateMbps(max, avg, min);
	CHECK(max == 20.0f);
	CHECK(avg == 10.0f);
	CHECK(min == 5.0f);

	unsigned maxK = 0;
	unsigned avgK = 0;
	unsigned minK = 0;
	t.getVariableBitRateKbps(maxK, avgK, minK);
	CHECK(maxK == 20000u);
	CHECK(avgK == 10000u);
	CHECK(minK == 5000u);
}

TEST_CASE("getRealMaxBitRateKbps usa el máximo del modo activo", "[settings][bitrate]") {
	SECTION("en modo constante es el bitrate constante") {
		TranscoderSettings t;
		t.setConstantBitRateMbps(8.0f);
		CHECK(t.getRealMaxBitRateKbps() == 8000u);
	}

	SECTION("en modo variable es la cota superior") {
		TranscoderSettings t;
		t.setVariableBitRateMbps(20.0f, 10.0f, 5.0f);
		t.setBitRateMode(BitRateMode::Variable);
		CHECK(t.getRealMaxBitRateKbps() == 20000u);
	}
}

TEST_CASE("TranscoderSettings: resolución y frame rate", "[settings][transcoder]") {
	TranscoderSettings t;

	t.setResolution(Resolution::HD720);
	unsigned x = 0;
	unsigned y = 0;
	t.getResolution(x, y);
	CHECK(x == 1280);
	CHECK(y == 720);

	t.setResolution(640, 480);
	t.getResolution(x, y);
	CHECK(x == 640);
	CHECK(y == 480);

	CHECK(t.getFrameRate() == 0.0);  // 0.0 significa autodetectar
	t.setFrameRate(59.94);
	CHECK(t.getFrameRate() == 59.94);
}

TEST_CASE("unsignedH264Level codifica el nivel en decimal", "[settings][h264]") {
	// La codificacion es entero*10 + decimal: el nivel 4.0 es 40, no 0x40.
	CHECK(TranscoderSettings::unsignedH264Level(4.0f) == 40u);
	CHECK(TranscoderSettings::unsignedH264Level(2.0f) == 20u);
	CHECK(TranscoderSettings::unsignedH264Level(1.0f) == 10u);
}

// DEFECTO CONOCIDO (settings.cpp:538). std::modf(x, &p) devuelve la parte
// fraccionaria y escribe la entera en p; el codigo asigna el retorno a
// "integerF" y la salida a "fractionF", con los nombres invertidos. Para
// niveles enteros el resultado coincide por casualidad, pero cualquier nivel
// con decimal se degrada en silencio al .0: 3.1 -> 30, 4.1 -> 40, 2.2 -> 20.
// Niveles como 2.1, 2.2 y 3.1 estan en la lista de validos de setH264Level,
// asi que se aceptan y se aplican mal.
//
// El test esta etiquetado !shouldfail a proposito: documenta el defecto sin
// romper la CI. Al arreglar settings.cpp este test pasara, Catch2 lo marcara
// como fallo por pasar, y ahi se quita la etiqueta.
TEST_CASE("unsignedH264Level deberia conservar el decimal del nivel",
	  "[settings][h264][!shouldfail]") {
	CHECK(TranscoderSettings::unsignedH264Level(3.1f) == 31u);
	CHECK(TranscoderSettings::unsignedH264Level(2.1f) == 21u);
	CHECK(TranscoderSettings::unsignedH264Level(2.2f) == 22u);
}

TEST_CASE("setH264Level y getH264Level son consistentes", "[settings][h264]") {
	TranscoderSettings t;
	CHECK(t.getH264Level() == 0.0f);  // 0.0 significa derivarlo del bitrate

	t.setH264Level(4.0f);
	CHECK(t.getH264Level() == 4.0f);
}

TEST_CASE("perfil H.264: High y Main se aceptan", "[settings][h264]") {
	TranscoderSettings t;

	t.setH264Profile(H264Profile::High);
	CHECK(t.getH264Profile() == H264Profile::High);

	t.setH264Profile(H264Profile::Main);
	CHECK(t.getH264Profile() == H264Profile::Main);
}

TEST_CASE("perfil H.264: Baseline se rechaza explicitamente", "[settings][h264]") {
	// settings.cpp:525. No esta implementado, y el rechazo es deliberado:
	// mejor un error que una configuracion que el hardware no aplica.
	TranscoderSettings t;
	t.setH264Profile(H264Profile::High);

	CHECK_THROWS_AS(t.setH264Profile(H264Profile::Baseline), setting_error);
	// Y el perfil previo debe seguir intacto tras el rechazo.
	CHECK(t.getH264Profile() == H264Profile::High);
}
