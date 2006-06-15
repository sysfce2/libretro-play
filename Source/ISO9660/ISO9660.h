#ifndef _ISO9660_H_
#define _ISO9660_H_

#include "Stream.h"
#include "Types.h"
#include "VolumeDescriptor.h"
#include "PathTable.h"
#include "DirectoryRecord.h"

class CISO9660
{
public:
								CISO9660(Framework::CStream*);
								~CISO9660();

	void						ReadBlock(uint32, void*);

	Framework::CStream*			Open(const char*);
	bool						GetFileRecord(ISO9660::CDirectoryRecord*, const char*);

	enum BLOCKSIZE
	{
		BLOCKSIZE = 0x800
	};

private:
	bool						GetFileRecordFromDirectory(ISO9660::CDirectoryRecord*, uint32, const char*);

	ISO9660::CVolumeDescriptor*	m_pVolumeDescriptor;
	ISO9660::CPathTable*		m_pPathTable;
	Framework::CStream*			m_pStream;

};

#endif
