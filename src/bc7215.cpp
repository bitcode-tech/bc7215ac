
#include "bc7215.h"

BC7215::BC7215(Stream& SerialPort, int ModPin, int BusyPin) : uart(SerialPort), modPin(ModPin), busyPin(BusyPin)
{
	pinMode(busyPin, INPUT);
	pinMode(modPin, OUTPUT);
	digitalWrite(modPin, HIGH);
}

BC7215::BC7215(Stream& SerialPort, MODConnect ModStat, BUSYConnect BusyStat) : uart(SerialPort), modPin(ModStat), busyPin(BusyStat)
{
}

BC7215::BC7215(Stream& SerialPort, int ModPin, BUSYConnect BusyStat) : uart(SerialPort), modPin(ModPin), busyPin(BusyStat)
{
	pinMode(modPin, OUTPUT);
	digitalWrite(modPin, HIGH);
}

BC7215::BC7215(Stream& SerialPort, MODConnect ModStat, int BusyPin) : uart(SerialPort), modPin(ModStat), busyPin(BusyPin)
{
	pinMode(busyPin, INPUT);
}

void BC7215::setTx()
{
	if (modPin >= 0)
	{
		digitalWrite(modPin, LOW);
	}
    bc7215Status.dataPktReady = 0;
    bc7215Status.formatPktReady = 0;
    bc7215Status.pktStarted = 0;
    bc7215Status.cmdComplete = 1;
}

void BC7215::setRx()
{
	if (modPin >= 0)
	{
		digitalWrite(modPin, HIGH);
	}
}

void BC7215::setShutDown()
{
	if ((modPin == -2) || ((modPin >= 0) && (digitalRead(modPin) == LOW)))	// if MOD is LOW
	{
		sendOneByte(0xf7);
		sendOneByte(0x00);
	}
	bc7215Status.cmdComplete = 0;
}

void BC7215::setRxMode(uint8_t mode)
{
	if ((modPin == -1) || ((modPin >= 0) && (digitalRead(modPin) == HIGH)))	// if MOD is HIGH
	{
		sendOneByte(mode);
	}
}

bool BC7215::isBusy()
{
	statusUpdate();
	return ((modPin == -2) || ((modPin >= 0) && (digitalRead(modPin) == LOW))) ? !bc7215Status.cmdComplete : bc7215Status.pktStarted;
}


#if ENABLE_RECEIVING == 1

bool BC7215::dataReady()
{
	statusUpdate();
	return bc7215Status.dataPktReady;
}

void BC7215::clrData()
{
	bc7215Status.dataPktReady = 0;
}

uint16_t BC7215::getLen()
{
	statusUpdate();
    if (bc7215Status.dataPktReady)
    {
        return bitLen;
    }
    else
    {
        return 0;
    }
}

uint16_t BC7215::dpketSize()
{
	statusUpdate();
    if (bc7215Status.dataPktReady)
    {
        return (bitLen + 7) / 8 + 2;
    }
    else
    {
        return 0;
    }
}

uint8_t BC7215::getData(bc7215DataVarPkt_t* target)
{
#	if BC7215_BUFFER_SIZE > 255
	uint16_t i;
#	else
	uint8_t i;
#	endif
	statusUpdate();
    uint8_t status = 0xff;
    if (bc7215Status.dataPktReady)
    {
        status = bufBackRead(datEndPos, 2);
        target->bitLen = bitLen;
        for (i = 0; i < datCount - 3; i++)
        {
            target->data[i] = bufRead(datStartPos, i);
        }
        bc7215Status.dataPktReady = 0;
    }
	return status;
}

uint8_t BC7215::getData(bc7215DataMaxPkt_t& target)
{
	return getData(reinterpret_cast<bc7215DataVarPkt_t*>(&target));
}


uint16_t BC7215::getRaw(void* addr, uint16_t size)
{
	uint16_t i;
	statusUpdate();
	if (bc7215Status.dataPktReady)
	{
	    bc7215Status.dataPktReady = 0;
	    if (size > (bitLen + 7) / 8)
	    {
	        size = (bitLen + 7) / 8;
	    }

	    for (i = 0; i < size; i++)
	    {
	        *((uint8_t*)addr + i) = bufRead(datStartPos, i);
	    }
	}
	else
	{
	    size = 0;
	}
	return size;
}

#	if ENABLE_FORMAT == 1
	
bool BC7215::formatReady()
{
	statusUpdate();
	return bc7215Status.formatPktReady;
}

void BC7215::clrFormat()
{
	bc7215Status.formatPktReady = 0;
}

uint8_t BC7215::getFormat(bc7215FormatPkt_t& target)
{
    uint8_t i;
    uint8_t signature;

	statusUpdate();
    signature = 0xff;
    if (bc7215Status.formatPktReady)
    {
        signature = bufRead(startPos, 0);
        target.signature.inByte = signature;
        for (i = 0; i < 32; i++)
        {
            target.format[i] = bufRead(startPos, i + 1);
        }
        bc7215Status.formatPktReady = 0;
    }
    return signature;
}

#	endif

#if BC7215_BUFFER_SIZE > 255
	uint8_t BC7215::bufBackRead(uint16_t pos, uint16_t n)
#else
	uint8_t BC7215::bufBackRead(uint8_t pos, uint8_t n)
#endif
{
    if (pos >= n)
    {
        return circularBuffer[pos - n];
    }
    else
    {
        return circularBuffer[BC7215_BUFFER_SIZE + pos - n];
    }
}

#if BC7215_BUFFER_SIZE > 255
	uint8_t BC7215::bufRead(uint16_t pos, uint16_t n)
#else
	uint8_t BC7215::bufRead(uint8_t pos, uint8_t n)
#endif
{
    if (pos + n >= BC7215_BUFFER_SIZE)
    {
        return circularBuffer[pos + n - BC7215_BUFFER_SIZE];
    }
    else
    {
        return circularBuffer[pos + n];
    }
}

#endif

#if ENABLE_TRANSMITTING == 1

void BC7215::loadFormat(const bc7215FormatPkt_t& source)
{
    uint8_t i;
    if ((modPin == -2) || ((modPin >= 0) && (digitalRead(modPin) == LOW)))	// if MOD is LOW (bc7215 is in transmit mode)
    {
        sendOneByte(0xf6);
        sendOneByte(0x01);
        for (i = 0; i < sizeof(bc7215FormatPkt_t); i++)
        {
            byteStuffingSend(*(reinterpret_cast<const uint8_t*>(&source) + i));
        }
    }
}

void BC7215::irTx(const bc7215DataVarPkt_t* source)
{
    uint16_t i;
    uint16_t bytes;
    if ((modPin == -2) || ((modPin >= 0) && (digitalRead(modPin) == LOW)))       // check if bc7215 is in trasmitting mode
    {
    	if ((source->bitLen >= 8) && (source->bitLen < 0x1000)) 
		{
		    bc7215Status.cmdComplete = 0;
		    sendOneByte(0xf5);
		    sendOneByte(0x02);
		    bytes = sizeof(source->bitLen) + (source->bitLen + 7) / 8;        // set bytes = total number of data bytes +2
		    for (i = 0; i < bytes; i++)                                       // send from 2nd byte of the data packet
		    {
		        byteStuffingSend(*((uint8_t*)source + i));
		    }
		}
    }
}

void BC7215::irTx(const bc7215DataMaxPkt_t& source)
{
	irTx(reinterpret_cast<const bc7215DataVarPkt_t*>(&source));
}

void BC7215::sendRaw(const void* source, uint16_t size)
{
    uint16_t i;
    if ((modPin == -2) || ((modPin >= 0) && (digitalRead(modPin) == LOW)))
    {
    	if (size < 0x200)
    	{
		    bc7215Status.cmdComplete = 0;
		    sendOneByte(0xf5);
		    sendOneByte(0x02);
		    byteStuffingSend((size * 8) & 0xff);        // send bit length of the data
		    byteStuffingSend((size * 8) >> 8);
		    for (i = 0; i < size; i++)
		    {
		        byteStuffingSend(*((uint8_t*)source + i));
		    }
		}
    }
}

#endif

bool BC7215::cmdCompleted()
{
	statusUpdate();
	return bc7215Status.cmdComplete;
}



void BC7215::setC56K(bc7215FormatPkt_t& formatPkt)
{
	formatPkt.signature.bits.c56k = 1;
}

void BC7215::clrC56K(bc7215FormatPkt_t& formatPkt)
{
	formatPkt.signature.bits.c56k = 0;
}

void BC7215::setNOCA(bc7215FormatPkt_t& formatPkt)
{
	formatPkt.signature.bits.noCA = 1;
}

void BC7215::clrNOCA(bc7215FormatPkt_t& formatPkt)
{
	formatPkt.signature.bits.noCA = 0;
}

uint8_t BC7215::crc8(const void* data, uint16_t len)
{
    uint16_t i;
    uint8_t j;
    uint8_t crc = 0;
    for (i = 0; i < len; ++i)
    {
        crc ^= *(reinterpret_cast<const unsigned char*>(data) +i);
        for (j = 0; j < 8; ++j)
        {
            if (crc & 0x80)
            {
                crc = (crc << 1) ^ BC7215_CRC8_POLY;
            }
            else
            {
                crc <<= 1;
            }
        }
    }
    return crc;
}

uint16_t BC7215::calSize(const bc7215DataVarPkt_t* dataPkt)
{
    if (dataPkt->bitLen < 0x1000)
    {
        return (dataPkt->bitLen + 7) / 8 + 2;
    }
    else
    {
        return 0;
    }
}

uint16_t BC7215::calSize(const bc7215DataMaxPkt_t& dataPkt)
{
	return calSize(reinterpret_cast<const bc7215DataVarPkt_t*>(&dataPkt));
}


void BC7215::copyDpkt(void* target, bc7215DataVarPkt_t* source)
{
    uint16_t i, totalLen;
    totalLen = calSize(source);
    if ((void*)source > target)        // if source is located behind target in memory, copy from front to end
    {
        for (i = 0; i < totalLen; i++)
        {
            *((uint8_t*)target + i) = *((uint8_t*)source + i);
        }
    }
    else if ((void*)source < target)        // if source is located before target in memory, copy from end to front
    {
        for (i = totalLen; i > 0; i--)
        {
            *((uint8_t*)target + i - 1) = *((uint8_t*)source + i - 1);
        }
    }
}

void BC7215::copyDpkt(void* target, bc7215DataMaxPkt_t& source)
{
	BC7215::copyDpkt(target, reinterpret_cast<bc7215DataVarPkt_t*>(&source));
}


bool BC7215::compareDpkt(uint8_t sig, const bc7215DataVarPkt_t* pkt1, const bc7215DataVarPkt_t* pkt2)
{
    uint8_t  i, bits, dat1, dat2;
    uint16_t i16, len;

    if (pkt1->bitLen != pkt2->bitLen)
    {
        return 0;
    }
    len = pkt1->bitLen / 8;
    bits = pkt1->bitLen & 0x07;
    for (i16 = 0; i16 < len; ++i16)
    {
        if (pkt1->data[i16] != pkt2->data[i16])
        {
            return 0;
        }
    }
    if (bits != 0)        // if the end of data is not a complete byte
    {
        dat1 = pkt1->data[len];
        dat2 = pkt2->data[len];
        if (dat1 == dat2)
        {
            return 1;
        }
        if ((sig & 0x30) == 0x30)        // TP0:TP1 = 11, if data is MSB first (PWM)
        {
            for (i = 0; i < bits; ++i)
            {
                if ((dat1 & 0x01) != (dat2 & 0x01))
                {
                    return 0;
                }
                dat1 >>= 1;
                dat2 >>= 1;
            }
        }
        else        // if data is LSB first (PPM)
        {
            for (i = 0; i < bits; ++i)
            {
                if ((dat1 & 0x80) != (dat2 & 0x80))
                {
                    return 0;
                }
                dat1 <<= 1;
                dat2 <<= 1;
            }
        }
    }
    return 1;
}

bool BC7215::compareDpkt(uint8_t sig, const bc7215DataMaxPkt_t& pkt1, const bc7215DataMaxPkt_t& pkt2)
{
	return BC7215::compareDpkt(sig, reinterpret_cast<const bc7215DataVarPkt_t*>(&pkt1), reinterpret_cast<const bc7215DataVarPkt_t*>(&pkt2));
}


void BC7215::byteStuffingSend(uint8_t data)
{
    if ((data == 0x7a) || (data == 0x7b))
    {
        sendOneByte(0x7b);
        sendOneByte(data | 0x80);
    }
    else
    {
        sendOneByte(data);
    }
}


void BC7215::sendOneByte(uint8_t data)
{
	if (busyPin != -3)	// if BUSY is connected to arduino
	{
		while (digitalRead(busyPin) == HIGH);	// wait until BUSY is LOW
	}
	uart.write(data);
	uart.flush();
}

void BC7215::statusUpdate()
{
	while (uart.available() > 0)
	{
		processData(uart.read());
	}
}

void BC7215::processData(uint8_t data)
{
#if ENABLE_RECEIVING == 1
    static uint8_t previousData = 0;
    uint8_t        temp;
    uint16_t       temp16;
#endif

    if ((modPin == -2) || ((modPin >= 0) && (digitalRead(modPin) == LOW)))        // MOD=LOW means bc7215 is in transmit mode
    {
        if (data == 0x7a)
        {
            bc7215Status.cmdComplete = 1;
        }
    }

#if ENABLE_RECEIVING == 1
    else        // bc7215 is in receive mode
    {
        if (data == 0x7a)
        {
            if (!bc7215Status.overLap)
            {
                if (previousData == 0x7a)        // if 0x7a 0x7a is the mark of format packet
                {
                    bc7215Status.dataPktReady = 0;
#    if ENABLE_FORMAT == 1
                    if (byteCount == sizeof(bc7215FormatPkt_t))        // check the packet size
                    {
                        bc7215Status.formatPktReady = 1;
                    }
#    endif
                    if ((byteCount + datCount <= BC7215_BUFFER_SIZE) && (!(bufBackRead(datEndPos, 2) & 0x80)))
                    // if the format packet is not over writting the data packet and there is no error
                    {
                        bitLen = ((uint16_t)bufBackRead(datEndPos, 0) << 8) | bufBackRead(datEndPos, 1);
                        bc7215Status.dataPktReady = 1;
                    }
                }
                else        // if this is the first 0x7a received
                {
                    if (!(bufBackRead(lastWritingPos, 2) & 0x80))
                    // if the buffer is not overflew and 'bit7' of the status byte the is not set (no error)
                    {
                        temp16 = ((uint16_t)bufBackRead(lastWritingPos, 0) << 8) | bufBackRead(lastWritingPos, 1);
                        // get the bit count of the data packet

                        if ((temp16 + 7) / 8 + 3 == byteCount)        // if the byte count of received packet is correct
                        {
                            bitLen = temp16;        // save the bitLen
                            bc7215Status.dataPktReady = 1;
                            datStartPos = startPos;
                            datEndPos = lastWritingPos;
                            datCount = byteCount;
                        }
                    }
                }
            }
            previousData = 0x7a;
            bc7215Status.pktStarted = 0;
        }
        else        // incoming data is not 0x7a
        {
            if (!bc7215Status.pktStarted)        // if it's the start of a new packet
            {
                bc7215Status.pktStarted = 1;        // clear new packet indicator
                bc7215Status.overLap = 0;
                byteCount = 0;
                bitLen = 0;
                bc7215Status.dataPktReady = 0;        // new data is coming, clear dataPktReady and formatPktReady flags
                bc7215Status.formatPktReady = 0;
                startPos = lastWritingPos + 1;        // save start buffer position of the packet
                if (startPos >= BC7215_BUFFER_SIZE)
                {
                    startPos = 0;
                }
            }
            if (data == 0x7b)        // byte-destuffing, ignore 0x7b
            {
                previousData = 0x7b;
                return;
            }
            else
            {
                temp = data;
                if (previousData == 0x7b)
                {
                    data &= 0x7f;
                }
                previousData = temp;
            }        // end of byte-destuffing
            // save data to buffer
            lastWritingPos++;
            if (lastWritingPos >= BC7215_BUFFER_SIZE)
            {
                lastWritingPos = 0;
            }
            circularBuffer[lastWritingPos] = data;
            byteCount++;
            if (byteCount > BC7215_BUFFER_SIZE)
            {
                bc7215Status.overLap = 1;
            }
        }
    }
#endif
}




