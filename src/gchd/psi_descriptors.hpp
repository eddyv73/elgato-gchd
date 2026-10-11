/**
 * Copyright (c) 2016 Scott Dossey <seveirein@yahoo.com>
 *
 * This source file is part of Game Capture HD Linux driver and is distributed
 * under the MIT License. For more information, see LICENSE file.
 */

/* Abstract class used for reading and
 * writing data in program specific information (PSI) as specified by
 * ISO/IEC 13818-1
 *
 * This file determines the data format of descriptors used in program
 * specific information.
 */

#ifndef PSI_DESCRIPTOR_H
#define PSI_DESCRIPTOR_H

#include <vector>
#include <cstdint>
#include <memory>
#include "psi_data.hpp"
#include "../utility.hpp"

#define PROGRAM_DESCRIPTOR 0x5
#define AVC_VIDEO_DESCRIPTOR 0x28
#define AVC_TIMING_AND_HRD_DESCRIPTOR 0x2a
#define HDMV_COPY_CONTROL_DESCRIPTOR 0x88

class PSI_Descriptor: public PSI_Data
{
	public:
		PSI_Descriptor( uint8_t tag ) : tag_(tag) {};
		PSI_Descriptor() {};
		uint8_t tag_ = 0; //El constructor por defecto lo dejaba indefinido.

		virtual int calculateSize();
		virtual int calculateSizeInternal();

		virtual void pack(std::vector<uint8_t> &outputData,
				  std::vector<uint8_t>::iterator &offset);

		virtual void packInternal(std::vector<uint8_t> &outputData,
					  std::vector<uint8_t>::iterator &offset);

		virtual void unpack(const std::vector<uint8_t> &inputData,
				    std::vector<uint8_t>::const_iterator &offset,
				    int size );

		virtual void unpackInternal(const std::vector<uint8_t> &inputData,
					    std::vector<uint8_t>::const_iterator &offset,
					    int size );

};

class PSI_ParseDescriptor: public PSI_Descriptor
{
	protected:
		std::shared_ptr<PSI_Descriptor> parsedDescriptor_;

	public:
		PSI_ParseDescriptor() {}; //Doesn't initialize anything.
		virtual std::shared_ptr<PSI_Descriptor> getParsedDescriptor();
		virtual void unpackInternal(const std::vector<uint8_t> &inputData,
					    std::vector<uint8_t>::const_iterator &offset,
					    int size );
};

class PSI_UnknownDescriptor: public PSI_Descriptor
{
	public:
		PSI_UnknownDescriptor( uint8_t tag );
		std::vector<uint8_t> data;

		virtual int calculateSizeInternal();

		//Faltaba. Sin ella corria el packInternal vacio de la base, asi
		//que calculateSize() declaraba 2+data.size() bytes y pack() solo
		//escribia los 2 de cabecera: la carga del descriptor se perdia
		//en silencio al reempaquetar, dejando ceros donde el campo de
		//longitud decia que habia datos.
		virtual void packInternal(std::vector<uint8_t> &outputData,
					  std::vector<uint8_t>::iterator &offset);

		virtual void unpackInternal(const std::vector<uint8_t> &inputData,
					    std::vector<uint8_t>::const_iterator &offset,
					    int size );

};

class PSI_ProgramDescriptor : public PSI_Descriptor
{
	public:
		PSI_ProgramDescriptor(): PSI_Descriptor(PROGRAM_DESCRIPTOR) {};
		PSI_ProgramDescriptor(const std::vector<uint8_t> &data): PSI_Descriptor(PROGRAM_DESCRIPTOR), data_(data) {};

		std::vector<uint8_t> data_;
		virtual int calculateSizeInternal();
		virtual void packInternal(std::vector<uint8_t> &outputData,
					  std::vector<uint8_t>::iterator &offset);

		virtual void unpackInternal(const std::vector<uint8_t> &inputData,
					    std::vector<uint8_t>::const_iterator &offset,
					    int size );
};

class PSI_HDMV_ShortDescriptor: public PSI_ProgramDescriptor
{
	public:
		PSI_HDMV_ShortDescriptor(): PSI_ProgramDescriptor( VC{ 'H', 'D', 'M', 'V' } ) {};
};

class PSI_HDMV_LongDescriptor: public PSI_ProgramDescriptor
{
	public:
		PSI_HDMV_LongDescriptor(): PSI_ProgramDescriptor( VC{ 'H', 'D', 'M', 'V', 0xff, 0x1b, 0x44, 0x3f} ) {};
};

class PSI_AVC_VideoDescriptor: public PSI_Descriptor
{
	public:
		PSI_AVC_VideoDescriptor(): PSI_Descriptor(AVC_VIDEO_DESCRIPTOR) {};

		PSI_AVC_VideoDescriptor( uint8_t profile_idc,
					 uint8_t level_idc,
					 bool constraint_set0_flag=false,
					 bool constraint_set1_flag=false,
					 bool constraint_set2_flag=false,
					 bool constraint_set3_flag=false,
					 bool constraint_set4_flag=false,
					 bool constraint_set5_flag=false,
					 bool AVC_still_present=false,
					 bool AVC_24_hour_picture_flag=false,
					 bool frame_packing_SEI_not_present_flag=true );

		//Inicializados en clase: el constructor por defecto los dejaba
		//indefinidos, y PSI_ParseDescriptor construye estos descriptores
		//asi al desempaquetar.
		uint8_t profile_idc_ = 0;
		bool constraint_set0_flag_ = false;
		bool constraint_set1_flag_ = false;
		bool constraint_set2_flag_ = false;
		bool constraint_set3_flag_ = false;
		bool constraint_set4_flag_ = false;
		bool constraint_set5_flag_ = false;
		uint8_t AVC_compatible_flags_ = 0;
		uint8_t level_idc_ = 0;
		bool AVC_still_present_ = false;
		bool AVC_24_hour_picture_flag_ = false;
		bool frame_packing_SEI_not_present_flag_ = true;

		virtual int calculateSizeInternal();
		virtual void packInternal(std::vector<uint8_t> &outputData,
					  std::vector<uint8_t>::iterator &offset);

		virtual void unpackInternal(const std::vector<uint8_t> &inputData,
					    std::vector<uint8_t>::const_iterator &offset,
					    int size );

};


//Found critical information is ITU-T Rec H 222.0 Amendment 3 (03/2004)
class PSI_AVC_TimingAndHRDDescriptor: public PSI_Descriptor
{
	public:
		PSI_AVC_TimingAndHRDDescriptor(): PSI_Descriptor(AVC_TIMING_AND_HRD_DESCRIPTOR) {};

		//picture_and_timing_info_not present
		PSI_AVC_TimingAndHRDDescriptor( bool hrd_management_valid_flag,
						bool fixed_frame_rate_flag,
						bool temporal_poc_flag,
						bool picture_to_display_conversion_flag );


		//Mismo caso que en PSI_AVC_VideoDescriptor.
		bool hrd_management_valid_flag_ = false;
		bool picture_and_timing_info_present_ = false;

		//FOLLOWING only avaliable if picture_and_timing_info_present_
		//START
		bool kHz90_flag_ = false;
		uint32_t N_ = 0; //Only pressent if 90kHz_flag_ is false.
		uint32_t K_ = 0; //Only pressent if 90kHz_flag_ is false.
		uint32_t num_units_in_tick_ = 0;
		//END

		bool fixed_frame_rate_flag_ = false;
		bool temporal_poc_flag_ = false;
		bool picture_to_display_conversion_flag_ = false;

		virtual int calculateSizeInternal();
		virtual void packInternal(std::vector<uint8_t> &outputData,
					  std::vector<uint8_t>::iterator &offset);

		virtual void unpackInternal(const std::vector<uint8_t> &inputData,
					    std::vector<uint8_t>::const_iterator &offset,
					    int size );

};

class PSI_HDMV_CopyControlDescriptor : public PSI_Descriptor
{
	public:
		PSI_HDMV_CopyControlDescriptor(): PSI_Descriptor(HDMV_COPY_CONTROL_DESCRIPTOR) {};

		virtual int calculateSizeInternal();
		virtual void packInternal(std::vector<uint8_t> &outputData,
					  std::vector<uint8_t>::iterator &offset);

		virtual void unpackInternal(const std::vector<uint8_t> &inputData,
					    std::vector<uint8_t>::const_iterator &offset,
					    int size );

};
#endif


