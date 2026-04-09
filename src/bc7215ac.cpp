#include "bc7215ac.h"

BC7215AC::BC7215AC(BC7215& bc7215Chip)
    : bc7215(bc7215Chip)
{
	for (int i=0; i<4; i++)
	{
		rcvdMessage[i].bitLen = 0;		// bitLen in rcvdMessages are always 0
	}
    bc7215.setTx();
    initOK = false;
	useFahrenheit = false;
}

void BC7215AC::setFahrenheit()
{
	if (!useFahrenheit)		// if temp unit changed, reset init. state.
	{
		initOK = false;
	}
	useFahrenheit = true;
}

void BC7215AC::setCelsius()
{
	if (useFahrenheit)
	{
		initOK = false;
	}
	useFahrenheit = false;
}

void BC7215AC::startCapture()
{
	sampleCount = 0;
    bc7215.setRx();
    delay(50);
    bc7215.setRxMode(1);
    bc7215.clrData();
    bc7215.clrFormat();
}

void BC7215AC::stopCapture()
{
    bc7215.setTx();
    delay(50);
}

bool BC7215AC::signalCaptured()
{
    if (bc7215.formatReady())
    {
		if (sampleCount < 4)
		{
        	bc7215.getFormat(sampleFormat[sampleCount]);
        	sampleStatus[sampleCount] = bc7215.getData(sampleData[sampleCount]);
			rcvdMessage[sampleCount].body.msg.fmt = &sampleFormat[sampleCount];
			rcvdMessage[sampleCount].body.msg.datPkt = reinterpret_cast<const bc7215DataVarPkt_t*>(&sampleData[sampleCount]);
			sampleCount++;
		}
		isCapturing = true;
		timerStartTime = millis();
    }
    else if (bc7215.dataReady())        // if not receiving Format but only data packet, may need to resend resend Rx
                                        // mode command
    {
        bc7215.setRxMode(1);
        bc7215.clrData();
        bc7215.clrFormat();
		timerStartTime = millis();
    }
	if (isCapturing)
	{
		if(bc7215.isBusy())
		{
			timerStartTime = millis();		// if BC7215 is still busy, reset timer
		}
		if (millis() - timerStartTime > 200)	// if idle time is more than 200ms
		{
			isCapturing = false;
			return true;
		}
	}
    return false;
}

void BC7215AC::sendAcCmd(const bc7215DataVarPkt_t* dataPkt)
{
    if (dataPkt->bitLen == 0)
    {
        bc7215.loadFormat(*(reinterpret_cast<const bc7215CombinedMsg_t*>(dataPkt)->body.msg.fmt));
        bc7215.irTx(reinterpret_cast<const bc7215CombinedMsg_t*>(dataPkt)->body.msg.datPkt);
    }
    else
    {
        bc7215.loadFormat(*bc7215_ac_get_base_fmt());
        bc7215.irTx(dataPkt);
    }
}

bool BC7215AC::init()
{
	initOK = false;
    if (sampleCount == 1)
    {
		if (useFahrenheit)
		{
        	initOK = bc7215_ac_init_f(sampleStatus[0], reinterpret_cast<const bc7215DataVarPkt_t*>(&rcvdMessage[0]));
		}
		else
		{
        	initOK = bc7215_ac_init(sampleStatus[0], reinterpret_cast<const bc7215DataVarPkt_t*>(&rcvdMessage[0]));
		}
    }
	else if (sampleCount > 1)
	{
		for (int j=0; j<sampleCount; j++)
		{
        	if (sampleStatus[j] & 0x40)        // if receiving status has "REV" bit set, reverse every byte of data
        	{
        	    for (int i = 0; i < (sampleData[j].bitLen + 7) / 8; i++)
        	    {
        	        sampleData[j].data[i] = ~sampleData[j].data[i];
        	    }
				sampleStatus[j] &= 0xbf;
        	}
		}
		if (useFahrenheit)
		{
			initOK = bc7215_ac_init2_f(sampleCount, rcvdMessage, 0);
		}
		else
		{
			initOK = bc7215_ac_init2(sampleCount, rcvdMessage, 0);
		}
	}
    return initOK;
}

bool BC7215AC::init(const bc7215DataMaxPkt_t& data, const bc7215FormatPkt_t& format)
{
    rcvdMessage[0].body.msg.datPkt = reinterpret_cast<const bc7215DataVarPkt_t*>(&data);
    rcvdMessage[0].body.msg.fmt = &format;
	if (useFahrenheit)
	{
		initOK = bc7215_ac_init_f(format.signature.inByte, reinterpret_cast<const bc7215DataVarPkt_t*>(&rcvdMessage[0]));
	}
	else
	{
		initOK = bc7215_ac_init(format.signature.inByte, reinterpret_cast<const bc7215DataVarPkt_t*>(&rcvdMessage[0]));
	}
	return initOK;
}

bool BC7215AC::matchNext() 
{
	if (initOK)
	{
		initOK = bc7215_ac_find_next(); 
	}
	return initOK;
}

uint8_t BC7215AC::cntPredef() { return bc7215_ac_predefined_cnt(); }

const char* BC7215AC::getPredefName(uint8_t index)
{
    if (index < bc7215_ac_predefined_cnt())
    {
        return bc7215_ac_predefined_name(index);
    }
    return NULL;
}

bool BC7215AC::initPredef(uint8_t index)
{
    bool result;
    initOK = false;
    if (index < cntPredef())
    {
		if (useFahrenheit)
		{
			memcpy(&sampleData[0], bc7215_ac_predefined_data_f(index), (bc7215_ac_predefined_data_f(index)->bitLen+7)/8+2);
		}
		else
		{
			memcpy(&sampleData[0], bc7215_ac_predefined_data(index), (bc7215_ac_predefined_data(index)->bitLen+7)/8+2);
		}
		memcpy(&sampleFormat[0], bc7215_ac_predefined_fmt(index), 33);
		
       	initOK = init(sampleData[0], sampleFormat[0]);
    }
    return initOK;
}

const bc7215DataVarPkt_t* BC7215AC::setTo(int temp, int mode, int fan, int key)
{
    const bc7215DataVarPkt_t* dataPkt;
    if (initOK)
    {
		if (useFahrenheit)
		{
        	dataPkt = bc7215_ac_set_f(temp - 60, mode, fan, key);
		}
		else
		{
        	dataPkt = bc7215_ac_set(temp - 16, mode, fan, key);
		}
        sendAcCmd(dataPkt);
        return dataPkt;
    }
    return NULL;
}

const bc7215DataVarPkt_t* BC7215AC::on()
{
    const bc7215DataVarPkt_t* dataPkt;
    if (initOK)
    {
        dataPkt = bc7215_ac_on();
        if (dataPkt == NULL)
        {
            dataPkt = bc7215_ac_get_base_data();
        }
        sendAcCmd(dataPkt);
        return dataPkt;
    }
    return NULL;
}

const bc7215DataVarPkt_t* BC7215AC::off()
{
    const bc7215DataVarPkt_t* dataPkt;
    if (initOK)
    {
        dataPkt = bc7215_ac_off();
        sendAcCmd(dataPkt);
        return dataPkt;
    }
    return NULL;
}

bool BC7215AC::parse(int& temp, int& mode, int& fan, int& power)
{
	int8_t t, m, f, p;
	bool result = false;
	if (initOK)
	{
    	if (sampleCount == 1)
    	{
    	    bc7215_ac_replace_base(sampleStatus[0], reinterpret_cast<const bc7215DataVarPkt_t*>(&sampleData[0]));
    	}
		else if (sampleCount > 1)
		{
			for (int j=0; j<sampleCount; j++)
			{
    	    	if (sampleStatus[j] & 0x40)        // if receiving status has "REV" bit set, reverse every byte of data
    	    	{
    	    	    for (int i = 0; i < (sampleData[j].bitLen + 7) / 8; i++)
    	    	    {
    	    	        sampleData[j].data[i] = ~sampleData[j].data[i];
    	    	    }
					sampleStatus[j] &= 0xbf;
    	    	}
			}
			bc7215_ac_replace_base(sampleCount, reinterpret_cast<const bc7215DataVarPkt_t*>(rcvdMessage));
		}
		if (useFahrenheit)
		{
			result = bc7215_ac_parse_f(&t, &m, &f, &p);
			temp = t+60;
		}
		else
		{
			result = bc7215_ac_parse(&t, &m, &f, &p);
			temp = t+16;
		}
		mode = m;
		fan = f;
		power = p;
	}
	return result;
}

bool BC7215AC::isBusy() { return bc7215.isBusy(); }

bool BC7215AC::isCelsius() { return !useFahrenheit; }

const bc7215DataVarPkt_t* BC7215AC::getDataPkt() { return bc7215_ac_get_base_data(); }

const bc7215FormatPkt_t* BC7215AC::getFormatPkt() { return bc7215_ac_get_base_fmt(); }

const char* BC7215AC::getLibVer() { return bc7215_ac_get_ver(); }
