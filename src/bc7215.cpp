
#include "bc7215.h"

BC7215* BC7215::instance = nullptr;

BC7215::BC7215(Stream& SerialPort, int ModPin, int BusyPin) : uart(SerialPort), modPin(ModPin), busyPin(BusyPin)
{
	driverLibConfig();
}

BC7215::BC7215(Stream& SerialPort, MODConnect ModStat, BUSYConnect BusyStat) : uart(SerialPort), modPin(ModStat), busyPin(BusyStat)
{
	driverLibConfig();
}

BC7215::BC7215(Stream& SerialPort, int ModPin, BUSYConnect BusyStat) : uart(SerialPort), modPin(ModPin), busyPin(BusyStat)
{
	driverLibConfig();
}

BC7215::BC7215(Stream& SerialPort, MODConnect ModStat, int BusyPin) : uart(SerialPort), modPin(ModStat), busyPin(BusyPin)
{
	driverLibConfig();
}

void BC7215::setTx()
{
	bc7215_set_tx();
}

void BC7215::setRx()
{
	bc7215_set_rx();
}

void BC7215::setShutDown()
{
	bc7215_set_shutdown();
}

void BC7215::setRxMode(uint8_t mode)
{
	bc7215_set_rx_mode(mode);
}

bool BC7215::isBusy()
{
	pollRx();
	return bc7215_is_busy();
}

void BC7215::pollRx()
{
	while (uart.available() > 0)
	{
		bc7215_process_uart_data(uart.read());
		if (bc7215_data_ready() || bc7215_format_ready())
		{
			return;
		}
	}
}

#if ENABLE_RECEIVING == 1

bool BC7215::dataReady()
{
	pollRx();
	return bc7215_data_ready();
}

void BC7215::clrData()
{
	bc7215_clr_data();
}

uint16_t BC7215::getLen()
{
	return bc7215_get_len();
}

uint16_t BC7215::dpketSize()
{
	return bc7215_dpkt_size();
}

uint8_t BC7215::getData(bc7215DataVarPkt_t* target)
{
	return bc7215_get_data(target);
}

uint8_t BC7215::getData(bc7215DataMaxPkt_t& target)
{
	return bc7215_get_data(reinterpret_cast<bc7215DataVarPkt_t*>(&target));
}


uint16_t BC7215::getRaw(void* addr, uint16_t size)
{
	return bc7215_get_raw(addr, size);
}

#	if ENABLE_FORMAT == 1
	
bool BC7215::formatReady()
{
	pollRx();
	return bc7215_format_ready();
}

void BC7215::clrFormat()
{
	bc7215_clr_format();
}

uint8_t BC7215::getFormat(bc7215FormatPkt_t& target)
{
	return bc7215_get_format(&target);
}

#	endif	// ENABLE_FORMAT

#endif	// ENABLE_RECEIVING

#if ENABLE_TRANSMITTING == 1

void BC7215::loadFormat(const bc7215FormatPkt_t& source)
{
	bc7215_load_format(&source);
}

void BC7215::irTx(const bc7215DataVarPkt_t* source)
{
	bc7215_IR_tx(source);
}

void BC7215::irTx(const bc7215DataMaxPkt_t& source)
{
	bc7215_IR_tx(reinterpret_cast<const bc7215DataVarPkt_t*>(&source));
}

void BC7215::sendRaw(const void* source, uint16_t size)
{
	bc7215_send_raw(source, size);
}

#endif	// ENABLE_TRANSMITTING

bool BC7215::cmdCompleted()
{
	pollRx();
	return bc7215_cmd_completed();
}



void BC7215::setC56K(bc7215FormatPkt_t& formatPkt)
{
	bc7215_set_C56K(&formatPkt);
}

void BC7215::clrC56K(bc7215FormatPkt_t& formatPkt)
{
	bc7215_clr_C56K(&formatPkt);
}

void BC7215::setNOCA(bc7215FormatPkt_t& formatPkt)
{
	bc7215_set_NOCA(&formatPkt);
}

void BC7215::clrNOCA(bc7215FormatPkt_t& formatPkt)
{
	bc7215_clr_NOCA(&formatPkt);
}

uint8_t BC7215::crc8(const void* data, uint16_t len)
{
#if BC7215_MAX_RX_DATA_SIZE < 256
    return bc7215_crc8(reinterpret_cast<const uint8_t*>(data), len%256);
#else
    uint8_t bc7215_crc8(reinterpret_cast<const uint8_t*>(data), len);
#endif	// BC7215_MAX_RX_DATA
}

uint16_t BC7215::calSize(const bc7215DataVarPkt_t* dataPkt)
{
	return bc7215_cal_size(dataPkt);
}

uint16_t BC7215::calSize(const bc7215DataMaxPkt_t& dataPkt)
{
	return bc7215_cal_size(reinterpret_cast<const bc7215DataVarPkt_t*>(&dataPkt));
}


void BC7215::copyDpkt(void* target, const bc7215DataVarPkt_t* source)
{
	bc7215_copy_dpkt(target, source);
}

void BC7215::copyDpkt(void* target, const bc7215DataMaxPkt_t& source)
{
	bc7215_copy_dpkt(target, reinterpret_cast<const bc7215DataVarPkt_t*>(&source));
}


bool BC7215::compareDpkt(uint8_t sig, const bc7215DataVarPkt_t* pkt1, const bc7215DataVarPkt_t* pkt2)
{
	return bc7215_compare_dpkt(sig, pkt1, pkt2);
}

bool BC7215::compareDpkt(uint8_t sig, const bc7215DataMaxPkt_t& pkt1, const bc7215DataMaxPkt_t& pkt2)
{
	return bc7215_compare_dpkt(sig, reinterpret_cast<const bc7215DataVarPkt_t*>(&pkt1), reinterpret_cast<const bc7215DataVarPkt_t*>(&pkt2));
}


void BC7215::sendOneByte(uint8_t data)
{
	if (instance) {
		instance->uart.write(data);
		instance->uart.flush();
	}
}

void BC7215::setModHigh()
{
	if (instance && instance->modPin >= 0)
	{
		digitalWrite(instance->modPin, HIGH);
	}
}

void BC7215::setModLow()
{
	if (instance && instance->modPin >= 0)
	{
		digitalWrite(instance->modPin, LOW);
	}
}

uint8_t BC7215::readMod()
{
	if (instance) {
		if (instance->modPin >= 0)
		{
			return digitalRead(instance->modPin);
		}
		else
		{
			return (instance->modPin == MODConnect::MOD_HIGH);
		}
	}
	return 0;
}

uint8_t BC7215::readBusy()
{
	if (instance) {
		if (instance->busyPin >= 0)
		{
			return digitalRead(instance->busyPin);
		}
		else
		{
			return !(instance->busyPin == BUSYConnect::BUSY_NC);
		}
	}
	return 0;
}

void BC7215::driverLibConfig()
{
	instance = this;
	if (modPin >= 0)
	{
		pinMode(modPin, OUTPUT);
	}
	if (busyPin >= 0)
	{
		pinMode(busyPin, INPUT);
	}
	bc7215_config_uart_send_byte(sendOneByte);
	bc7215_config_mod_set_high_funct(setModHigh);
	bc7215_config_mod_set_low_funct(setModLow);
	bc7215_config_read_mod_funct(readMod);
	bc7215_config_read_busy_funct(readBusy);
}



