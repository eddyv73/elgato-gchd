/**
 * Copyright (c) 2016 Scott Dossey <seveirein@yahoo.com>
 *
 * This source file is part of Game Capture HD Linux driver and is distributed
 * under the MIT License. For more information, see LICENSE file.
 */

/* This file is for the encoding and decoding of the syntax sections
 * of program specific information as specified by ISO/IEC 13818-1
 */
#include <span>

#include "psi_syntax.hpp"
#include "psi_exceptions.hpp"
#include "../utility.hpp"

int PSI_Syntax::calculateSize() {
	return 5; //5=table header size.
}

void PSI_Syntax::pack(std::vector<uint8_t> &data, std::vector<uint8_t>::iterator &offset) {
	//La validacion se hace antes de escribir. La version anterior emitia
	//extension_ y solo despues comprobaba version_, de modo que al lanzar
	//dejaba el buffer a medio escribir.
	if( version_ >= (1<<5) ) {
		throw PSI_ValueException("A version number value is set too high in a PSI syntax section.");
	}

	std::span<uint8_t> salida=Utility::remaining(data, offset);
	if( salida.size() < 5 ) {
		throw PSI_ValueException("Not enough room to pack a PSI syntax section.");
	}

	Utility::byteify<uint16_t>( salida, extension_ );

	//Reserved bits + version_ + currentIndicator_
	salida[2] = (0x3 << 6) | (version_ <<1) | currentIndicator_;
	salida[3] = sectionNumber_;
	salida[4] = lastSectionNumber_;

	offset+=5;
}

void PSI_Syntax::unpack(const std::vector<uint8_t> &data,
			std::vector<uint8_t>::const_iterator &offset,
			int sectionLength)
{
	int overhead=5+4; //5 byte header, 4 byte crc.
	if (sectionLength < overhead) {
		throw PSI_FormatException("The length of a PSI Syntax section needs to be at least 9 bytes.");
	}



	std::span<const uint8_t> entrada=Utility::remaining(data, offset);
	if( entrada.size() < 5 ) {
		throw PSI_FormatException("Not enough data to unpack a PSI syntax section.");
	}

	extension_=Utility::debyteify<uint16_t>( entrada );
	uint8_t value=entrada[2];

	currentIndicator_ = value & 1;
	version_ = (value >> 1) & ((1<<5)-1);

	//Check reserved, this is strict checker that validates
	//we are doing it right.
	if((value>>6) != 3) {
		throw PSI_FormatException("Reserved bits not set in table syntax section.");
	}
	sectionNumber_=entrada[3];
	lastSectionNumber_=entrada[4];

	offset+=5;
}


