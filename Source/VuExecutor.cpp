#include "VuExecutor.h"
#include "VuBasicBlock.h"
#include <zlib.h>

CVuExecutor::CVuExecutor(CMIPS& context) :
CMipsExecutor(context)
{

}

CVuExecutor::~CVuExecutor()
{

}

void CVuExecutor::Reset()
{
	m_cachedBlocks.clear();
	CMipsExecutor::Reset();
}

BasicBlockPtr CVuExecutor::BlockFactory(CMIPS& context, uint32 begin, uint32 end)
{
	uint32 blockSize = ((end - begin) + 4) / 4;
	uint32 blockSizeByte = blockSize * 4;
	uint32* blockMemory = reinterpret_cast<uint32*>(alloca(blockSizeByte));
	for(uint32 address = begin; address <= end; address += 8)
	{
		uint32 index = (address - begin) / 4;

		uint32 addressLo = address + 0;
		uint32 addressHi = address + 4;

		uint32 opcodeLo = m_context.m_pMemoryMap->GetInstruction(addressLo);
		uint32 opcodeHi = m_context.m_pMemoryMap->GetInstruction(addressHi);

		assert((index + 0) < blockSize);
		blockMemory[index + 0] = opcodeLo;
		assert((index + 1) < blockSize);
		blockMemory[index + 1] = opcodeHi;
	}

	uint32 checksum = crc32(0, reinterpret_cast<Bytef*>(blockMemory), blockSizeByte);

	std::pair<CachedBlockMap::iterator, CachedBlockMap::iterator> equalRange = m_cachedBlocks.equal_range(checksum);
	for(; equalRange.first != equalRange.second; ++equalRange.first)
	{
		const BasicBlockPtr& basicBlock(equalRange.first->second);
		if(basicBlock->GetBeginAddress() == begin)
		{
			if(basicBlock->GetEndAddress() == end)
			{
				return basicBlock;
			}
		}
	}

	BasicBlockPtr result(new CVuBasicBlock(context, begin, end));
	m_cachedBlocks.insert(CachedBlockMap::value_type(checksum, result));
	return result;
}

void CVuExecutor::PartitionFunction(uint32 functionAddress)
{
    const uint32 vuMaxAddress = 0x4000;
    typedef std::set<uint32> PartitionPointSet;
    uint32 endAddress = 0;
    PartitionPointSet partitionPoints;

    //Insert begin point
    partitionPoints.insert(functionAddress);

    //Find the end
    for(uint32 address = functionAddress; ; address += 4)
    {
        //Probably going too far...
        if(address >= vuMaxAddress)
        {
            endAddress = address;
            partitionPoints.insert(endAddress);
            break;
        }

        uint32 opcode = m_context.m_pMemoryMap->GetInstruction(address);
        //If we find the E bit in an upper instruction
        if((address & 0x04) && (opcode & 0x40000000))
        {
            endAddress = address + 8;
            partitionPoints.insert(endAddress + 4);
            break;
        }
    }

    //Find partition points within the function
    for(uint32 address = functionAddress; address <= endAddress; address += 4)
    {
        uint32 opcode = m_context.m_pMemoryMap->GetInstruction(address);
        bool isBranch = m_context.m_pArch->IsInstructionBranch(&m_context, address, opcode);
        if(isBranch)
        {
            assert((address & 0x07) == 0x00);
            partitionPoints.insert(address + 0x10);
            uint32 target = m_context.m_pArch->GetInstructionEffectiveAddress(&m_context, address, opcode);
            if(target > functionAddress && target < endAddress)
            {
                assert((target & 0x07) == 0x00);
                partitionPoints.insert(target);
            }
        }
        //-- Meaningless in VU
        //SYSCALL or ERET
        //if(opcode == 0x0000000C || opcode == 0x42000018)
        //{
        //    partitionPoints.insert(address + 4);
        //}
        //Check if there's a block already exising that this address
        if(address != endAddress)
        {
            BasicBlockPtr possibleBlock = FindBlockStartingAt(address);
            if(possibleBlock != NULL)
            {
                assert(possibleBlock->GetEndAddress() <= endAddress);
                //Add its beginning and end in the partition points
                partitionPoints.insert(possibleBlock->GetBeginAddress());
                partitionPoints.insert(possibleBlock->GetEndAddress() + 4);
            }
        }
    }

    uint32 currentPoint = MIPS_INVALID_PC;
    for(PartitionPointSet::const_iterator pointIterator(partitionPoints.begin());
        pointIterator != partitionPoints.end(); pointIterator++)
    {
        if(currentPoint != MIPS_INVALID_PC)
        {
            uint32 beginAddress = currentPoint;
            uint32 endAddress = *pointIterator - 4;
            //Sanity checks
            assert((beginAddress & 0x07) == 0x00);
            assert((endAddress & 0x07) == 0x04);
            CreateBlock(beginAddress, endAddress);
        }
        currentPoint = *pointIterator;
    }

	//Convenient cutting for debugging purposes
    //for(uint32 address = functionAddress; address <= endAddress; address += 8)
    //{
    //    uint32 beginAddress = address;
    //    uint32 endAddress = address + 4;
    //    //Sanity checks
    //    assert((beginAddress & 0x07) == 0x00);
    //    assert((endAddress & 0x07) == 0x04);
    //    CreateBlock(beginAddress, endAddress);
    //}
}
