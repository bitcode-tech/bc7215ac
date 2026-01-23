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
        initOK = bc7215_ac_init(sampleStatus[0], reinterpret_cast<const bc7215DataVarPkt_t*>(&rcvdMessage[0]));
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
		initOK = bc7215_ac_init2(sampleCount, rcvdMessage, 0);
	}
    return initOK;
}

bool BC7215AC::init(const bc7215DataMaxPkt_t& data, const bc7215FormatPkt_t& format)
{
    rcvdMessage[0].body.msg.datPkt = reinterpret_cast<const bc7215DataVarPkt_t*>(&data);
    rcvdMessage[0].body.msg.fmt = &format;
	initOK = bc7215_ac_init(format.signature.inByte, reinterpret_cast<const bc7215DataVarPkt_t*>(&rcvdMessage[0]));
	return initOK;
}

bool BC7215AC::matchNext() 
{
	initOK = bc7215_ac_find_next(); 
	return initOK;
}

uint8_t BC7215AC::extraSample() { return bc7215_ac_need_extra_sample(); }

bool BC7215AC::saveExtra()
{
    rcvdMessage[0].bitLen = 0;
	rcvdMessage[0].body.msg.fmt = &sampleFormat[0];
    rcvdMessage[0].body.msg.datPkt = reinterpret_cast<const bc7215DataVarPkt_t*>(&sampleData[0]);
    return bc7215_ac_save_2nd_base(sampleStatus[0], &rcvdMessage[0]);
}

bool BC7215AC::saveExtra(const bc7215DataMaxPkt_t& data, const bc7215FormatPkt_t& format)
{
	rcvdMessage[0].bitLen = 0;
    rcvdMessage[0].body.msg.fmt = &format;
    rcvdMessage[0].body.msg.datPkt = reinterpret_cast<const bc7215DataVarPkt_t*>(&data);
    return bc7215_ac_save_2nd_base(format.signature.bits.sig, &rcvdMessage[0]);
}

bc7215CombinedMsg_t BC7215AC::getExtra()
{
	return bc7215_ac_get_2nd_base();
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
		memcpy(&sampleData[0], bc7215_ac_predefined_data(index), (bc7215_ac_predefined_data(index)->bitLen+7)/8+2);
		memcpy(&sampleFormat[0], bc7215_ac_predefined_fmt(index), 33);
		
        initOK = init(sampleData[0], sampleFormat[0]);
    }
    return initOK;
}

const bc7215DataVarPkt_t* BC7215AC::setTo(int tempC, int mode, int fan, int key)
{
    const bc7215DataVarPkt_t* dataPkt;
    if (initOK)
    {
        dataPkt = bc7215_ac_set(tempC - 16, mode, fan, key);
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
	bool result;
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
	result = bc7215_ac_parse(&t, &m, &f, &p);
	temp = t+16;
	mode = m;
	fan = f;
	power = p;
	return result;
}

bool BC7215AC::isBusy() { return bc7215.isBusy(); }

const bc7215DataVarPkt_t* BC7215AC::getDataPkt() { return bc7215_ac_get_base_data(); }

const bc7215FormatPkt_t* BC7215AC::getFormatPkt() { return bc7215_ac_get_base_fmt(); }

const char* BC7215AC::getLibVer() { return bc7215_ac_get_ver(); }
