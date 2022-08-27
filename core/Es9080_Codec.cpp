#include "../include/Es9080_Codec.h"

constexpr AudioCodecParams::TdmMode kTdmModeTdm = AudioCodecParams::kTdmModeTdm;
constexpr AudioCodecParams::ClockSource kClockSourceMcasp = AudioCodecParams::kClockSourceMcasp;
constexpr AudioCodecParams::ClockSource kClockSourceCodec = AudioCodecParams::kClockSourceCodec;
constexpr AudioCodecParams::ClockSource kClockSourceExternal = AudioCodecParams::kClockSourceExternal;

// parameters for TDM mode
const unsigned int kNumBits = 256;
const AudioCodecParams::TdmMode kTdmMode = kTdmModeTdm;
const unsigned int kSlotSize = 32;
const unsigned int kDataSize = 16;
const unsigned int kStartingSlot = 0;
const unsigned int kBitDelay = 0;
const unsigned int kNumSlots = kNumBits / kSlotSize;

Es9080_Codec::Es9080_Codec(int i2cBus, int i2cAddress, bool isVerbose)
	: running(false)
	, verbose(isVerbose)
{
	params.slotSize = kSlotSize;
	params.startingSlot = kStartingSlot;
	params.bitDelay = kBitDelay;
	params.dualRate = false;
	params.tdmMode = kTdmMode;
	params.bclk = kClockSourceCodec;
	params.wclk = kClockSourceCodec;
	params.mclk = mcaspConfig.getValidAhclk(24000000);
	params.samplingRate = params.mclk / double(kNumBits);
	initI2C_RW(i2cBus, i2cAddress, -1);
}

Es9080_Codec::~Es9080_Codec()
{
	//disable(); // TODO :disable
}

McaspConfig& Es9080_Codec::getMcaspConfig()
{
	mcaspConfig.params.inChannels = getNumIns();
	mcaspConfig.params.outChannels = getNumOuts();;
	mcaspConfig.params.inSerializers = {};
	mcaspConfig.params.outSerializers = {2};
	mcaspConfig.params.numSlots = kNumSlots;
	mcaspConfig.params.slotSize = params.slotSize;
	mcaspConfig.params.dataSize = kDataSize;
	mcaspConfig.params.bitDelay = params.bitDelay;
	mcaspConfig.params.ahclkIsInternal = true;
	mcaspConfig.params.ahclkFreq = params.mclk;
	mcaspConfig.params.wclkIsInternal = (kClockSourceMcasp == params.wclk);
	mcaspConfig.params.wclkIsWord = false; // could be
	mcaspConfig.params.wclkFalling = true;
	mcaspConfig.params.externalSamplesRisingEdge = false;

	return mcaspConfig;
}

int Es9080_Codec::initCodec(){
	// check it's alive. Write a register to the write-only address and
	// make sure it succeeds
	return disable();
}

#include <MiscUtilities.h>
int Es9080_Codec::startAudio(int dummy){
	//std::string program = R"HEREDOC(
	//bla
//)HEREDOC";
	std::string program = IoUtils::readTextFile("/root/Bela/es9080-program.txt");
	using StringUtils::parseAsInt;
	using StringUtils::split;
	using StringUtils::trim;
	std::vector<std::string> lines = split(program, '\n', true);
	for(auto& line : lines)
	{
		line = trim(line);
		std::vector<std::string> tokens = split(line, ' ', true, 5); // true auto trims it
		unsigned int cmdLen = 4;
		// some lines don't have the address, so they are 'w reg val'
		if(tokens.size() >= 3)
		{
			if(';' == tokens[2].back())
				cmdLen = 3;
		} else  {
			if(tokens.size() < 4)
				continue;
		}
		if(tokens[0] != "w")
			continue;
		unsigned int i = 1;
		// we retrieve the address, used for error checking below
		unsigned int addr = 0xff;
		if(3 == cmdLen)
		{
			// the comment may have been split. Re-assemble it
			if(tokens.size() >= 5)
				tokens[3] = tokens[3] + ' ' + tokens[4];
		} else {
			addr = parseAsInt(tokens[i++].c_str());
		}
		unsigned int reg = parseAsInt(tokens[i++].c_str());
		unsigned int val = parseAsInt(tokens[i++].c_str());
		const std::string& comment = tokens[i++];
		printf("w 0x%x %u 0x%02x // %s\n", addr, reg, val, comment.c_str());
		if(writeRegister(reg, val))
		{
			fprintf(stderr, "Error writing\n");
			return 1;
		}
		if(addr != 0xff && currentAddress != (addr >> 1))
		{
			fprintf(stderr, "Wrote to wrong address\n");
			return 1;
		}
	}


	// initialise all gains to 0
	for(unsigned int n = 0; n < getNumIns(); ++n)
		setInputGain(n, 0);
	for(unsigned int n = 0; n < getNumOuts(); ++n)
		setLineOutVolume(n, 0);
	return 0;
}

int Es9080_Codec::stopAudio()
{
	// TODO: mute
	//return disable();
	return 0; // TODO: disable
}

int Es9080_Codec::setLineOutVolume(int channel, float gain)
{
	if(channel >= int(getNumOuts()))
		return -1;
	if(channel < 0)
	{
		int ret = 0;
		for(unsigned int n = 0; n < getNumOuts(); ++n)
			ret |= setLineOutVolume(n, gain);
		return ret;
	}
	// TODO
	return 0;
}

int Es9080_Codec::setInputGain(int channel, float gain)
{
	if(channel >= int(getNumIns()))
		return -1;
	if(channel < 0)
	{
		int ret = 0;
		for(unsigned int n = 0; n < getNumIns(); ++n)
			ret |= setInputGain(n, gain);
		return ret;
	}
	// TODO
	return 0;
}

int Es9080_Codec::setAddressForReg(unsigned int reg, bool write)
{
	// the ES9080 can be contacted at two addresses. The one we got in the
	// constructor is the write-only address. The read/write is 4 less than that
	const int writeOnly = i2C_address;
	const int readWrite = writeOnly - 4;
	const int err = -1;
	int addr = err;
	// At the write-only address:
	// Write-only Register
	//	 Registers 192 – 203 (0xC0 – 0xCB) are write only registers.
	if(reg >= 192 && reg <= 203 && write)
		addr = writeOnly;
	// At the read/write address:
	// Read/Write Registers
	// 	Registers 0–164 (0x00 – 0xB3) are read/write registers
	if(reg <= 164)
		addr = readWrite;
	// At the read/write address (again):
	// Read-only Register
	// 	Registers 224 – 255 (0xE0 – 0xFF) are read only registers.
	if(reg >= 224 && reg <= 255 && !write)
		addr = readWrite;

	if(err == addr)
		return 1;
	if(ioctl(i2C_file, I2C_SLAVE, addr) < 0)
	{
		fprintf(stderr, "Failed to set i2c address\n");
		return  1;
	}
	currentAddress = addr;
	return 0;
}

// Write a specific register on the codec
int Es9080_Codec::writeRegister(unsigned int reg, unsigned int value)
{
	int addr = setAddressForReg(reg, true);
	if(addr < 0)
		return 1;

	char buf[2] = { static_cast<char>(reg & 0xFF), static_cast<char>(value & 0xFF) };
	if(write(i2C_file, buf, sizeof(buf)) != sizeof(buf))
	{
		verbose && fprintf(stderr, "Failed to write register %d on Es9080 codec\n", reg);
		return 1;
	}
	return 0;
}

// Read a specific register from the codec
int Es9080_Codec::readRegister(unsigned char reg)
{
	if(setAddressForReg(reg, true))
		return 1;

	// first, write the address of the register you want to read
	int ret = write(i2C_file, &reg, sizeof(reg));
	if(sizeof(reg) != ret)
	{
		verbose && fprintf(stderr, "Failed to write register %d on Es9080 codec\n", reg);
		return -1;
	}
	// then read the content of the address you specified
	unsigned char value;
	ret = read(i2C_file, &value, sizeof(value));
	if(sizeof(value) != ret)
	{
		
		verbose && fprintf(stderr, "Failed to read register %d on Es9080 codec\n", reg);
		return -1;
	}
	return value;
}

int Es9080_Codec::setHpVolume(int channel, float gain)
{
	return 0;
}

float Es9080_Codec::getSampleRate(){
	return params.samplingRate;
}

unsigned int Es9080_Codec::getNumIns(){
	return 0;
}

unsigned int Es9080_Codec::getNumOuts(){
	return 8;
}

int Es9080_Codec::disable()
{
	// TODO: should we mute instead of reset?
	return reset();
}

int Es9080_Codec::reset()
{
	//  RESET & PLL REGISTER1: AO_SOFT_RESET | PLL_SOFT_RESET
	if(writeRegister(192, 0xC0))
		return 1;
	//  RESET & PLL REGISTER1: clear AO_SOFT_RESET | PLL_SOFT_RESET
	return writeRegister(192, 0x0);
}
