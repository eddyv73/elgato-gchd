/**
 * Copyright (c) 2016 Scott Dossey <seveirein@yahoo.com>
 *
 * This source file is part of Game Capture HD Linux driver and is distributed
 * under the MIT License. For more information, see LICENSE file.
 */

#ifndef UTILITY_H
#define UTILITY_H

#include <algorithm>
#include <cstdint>
#include <climits>
#include <iterator>
#include <span>
#include <stdexcept>
#include <vector>

namespace Utility
{

	//Devuelve lo que queda del contenedor desde offset, como span.
	//
	//byteify y debyteify recibian un puntero crudo sin cota: 25 de sus 36
	//puntos de llamada no pasaban limite, asi que escribian o leian
	//sizeof(T) bytes sin que nada lo acotara. En las clases PSI eso es
	//especialmente delicado, porque calculateSize() y pack() se
	//implementan por separado y una desincronizacion de un byte era un
	//desbordamiento de heap silencioso. Pasando el resto del contenedor
	//como span, el acceso queda acotado por construccion.
	inline std::span<uint8_t> remaining(std::vector<uint8_t> &data,
					    std::vector<uint8_t>::iterator offset)
	{
		if (offset >= data.end()) {
			return {};
		}
		return std::span<uint8_t>(&(*offset),
					  static_cast<size_t>(std::distance(offset, data.end())));
	}

	inline std::span<const uint8_t> remaining(const std::vector<uint8_t> &data,
						  std::vector<uint8_t>::const_iterator offset)
	{
		if (offset >= data.end()) {
			return {};
		}
		return std::span<const uint8_t>(&(*offset),
						static_cast<size_t>(std::distance(offset, data.end())));
	}

	//El span acota; NO trunca. El truncado deliberado sigue expresandose con
	//'limit', igual que antes, de modo que los bytes producidos son
	//identicos a los de la version con puntero. La diferencia es que un
	//buffer demasiado corto ahora lanza en vez de corromper memoria.
	template<typename T>
	T debyteify(std::span<const uint8_t> input, unsigned limit=sizeof(T))
	{
		unsigned bytes=std::min(sizeof(T), (size_t)limit);
		if (bytes > input.size()) {
			throw std::out_of_range( "debyteify: buffer de entrada demasiado corto." );
		}

		T value=0;
		for(unsigned i=0; i<bytes; ++i)
		{
			value <<= 8;
			value |= input[i];
		}
		return value;
	};

	template<typename T>
	void byteify(std::span<uint8_t> output, T value, unsigned limit=sizeof(T))
	{
		unsigned bytes=std::min(sizeof(T), (size_t)limit);
		if (bytes > output.size()) {
			throw std::out_of_range( "byteify: buffer de salida demasiado corto." );
		}

		for(unsigned i=0; i<bytes; ++i)
		{
			output[i] = (value >> ((bytes - 1 - i) * 8)) & 0xff;
		}
	};

	typedef struct {
			int32_t num;
			uint32_t denom;
	} fraction_t;

	//Finds a fractional approximation, iven a float, and a max denominator.
	//MAKE sure the the maximum possible value * maxDenom < INT32_MAX
	//before using.
	fraction_t findFraction( double value, uint32_t maxDenom );

	//These are used for string splitting
	void split(const std::string &input,
		   char delimiter,
		   std::vector<std::string> &elements);

	std::vector<std::string> split(const std::string &input, char delimiter);

	// trim from start. Modifies original string
	std::string &ltrim(std::string &s);

	// trim from end
	std::string &rtrim(std::string &s);

	// trim from both ends
	std::string &trim(std::string &s);

	//This works with IPV4 and IPV6 address to get port and address
	//IE:
	//   ::1 is an IPV6 loopback
	//    :1 is port 1
	// [::1]:1 is IPV6 loopback address port 1.
	// 127.0.0.1:1 IPV4 example
	// [127.0.0.1]:1 optional IPV4 usage.
	// returns false if clearly invalid, but otherwise validity is up to
	// passing parts to getaddrinfo.
	bool splitIPAddressAndPort( std::string &address, std::string &port, const std::string &passedInput);
};

//Some commands take a std::vector<unsigned char>
//
//With this macro you can pass a literal array definition
//into such a function, without having to type out all the boilerplate.
//IE:
//   foo( VC{ 1, 2, 'a'} );
//This will only compile on c++11 and up.
#define VC std::vector<unsigned char>

#endif
