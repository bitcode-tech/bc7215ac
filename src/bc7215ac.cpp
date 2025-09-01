#include "bc7215ac.h"

BC7215AC::BC7215AC(BC7215& bc7215Chip)
    : bc7215(bc7215Chip)
{
    bc7215.setTx();
    initOK = false;
    sampleReady = false;
}

void BC7215AC::startCapture()
{
    bc7215.setRx();
    delay(50);
    bc7215.setRxMode(1);
    bc7215.clrData();
    bc7215.clrFormat();
    sampleReady = false;
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
        bc7215.getFormat(rcvdFmt);
        rcvdStatus = bc7215.getData(rcvdData);
        sampleReady = true;
        return true;
    }
    else if (bc7215.dataReady())        // if not receiving Format but only data packet, may need to resend resend Rx
                                        // mode command
    {
        bc7215.setRxMode(1);
        bc7215.clrData();
        bc7215.clrFormat();
    }
    return false;
}

bool BC7215AC::signalCaptured(bc7215DataVarPkt_t* data, bc7215FormatPkt_t* format)
{
    if (bc7215.formatReady())
    {
        bc7215.getFormat(*format);
        rcvdStatus = bc7215.getData(data);
        if (rcvdStatus & 0x40)        // if receiving status has "REV" bit set, reverse every byte of data
        {
            for (int i = 0; i < (data->bitLen + 7) / 8; i++)
            {
                data->data[i] = ~data->data[i];
            }
        }
        return true;
    }
    else if (bc7215.dataReady())        // if not receiving Format but only data packet, may need to resend resend Rx
                                        // mode command
    {
        bc7215.setRxMode(1);
        bc7215.clrData();
        bc7215.clrFormat();
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
    stopCapture();
    if (sampleReady)
    {
        rcvdMessage[0].bitLen = 0;
        rcvdMessage[0].body.msg.fmt = &rcvdFmt;
        rcvdMessage[0].body.msg.datPkt = reinterpret_cast<bc7215DataVarPkt_t*>(&rcvdData);
        initOK = bc7215_ac_init(rcvdStatus, reinterpret_cast<const bc7215DataVarPkt_t*>(&rcvdMessage[0]));
        return initOK;
    }
    return false;
}

bool BC7215AC::init(const bc7215DataMaxPkt_t& data, const bc7215FormatPkt_t& format)
{
	return init(reinterpret_cast<const bc7215DataVarPkt_t*>(&data), &format);
}

bool BC7215AC::init(const bc7215DataVarPkt_t* data, const bc7215FormatPkt_t* format)
{
    rcvdStatus = format->signature.bits.sig;
    memcpy(&rcvdData, data, sizeof(bc7215DataMaxPkt_t));
    memcpy(&rcvdFmt, format, sizeof(bc7215FormatPkt_t));
    sampleReady = true;
    return init();
}

bool BC7215AC::init(uint8_t cnt, const bc7215DataMaxPkt_t data[], const bc7215FormatPkt_t format[])
{
    initOK = false;
    stopCapture();
    if (cnt < 4)
    {
        for (int i = 0; i < cnt; i++)
        {
            rcvdMessage[i].bitLen = 0;
            rcvdMessage[i].body.msg.datPkt = reinterpret_cast<const bc7215DataVarPkt_t*>(&data[i]);
            rcvdMessage[i].body.msg.fmt = &format[i];
        }
        initOK = bc7215_ac_init2(cnt, rcvdMessage, 0);
    }
    return initOK;
}

bool BC7215AC::matchNext() 
{
	initOK = bc7215_ac_find_next(); 
	return initOK;
}

uint8_t BC7215AC::extraSample() { return bc7215_ac_need_extra_sample(); }

bool BC7215AC::saveExtra(const bc7215DataVarPkt_t* data, const bc7215FormatPkt_t* format)
{
    rcvdMessage[0].body.msg.fmt = format;
    rcvdMessage[0].body.msg.datPkt = data;
    return bc7215_ac_save_2nd_base(format->signature.bits.sig, &rcvdMessage[0]);
}

bool BC7215AC::saveExtra(const bc7215DataMaxPkt_t& data, const bc7215FormatPkt_t& format)
{
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
        initOK = init(bc7215_ac_predefined_data(index), bc7215_ac_predefined_fmt(index));
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

bool BC7215AC::isBusy() { return bc7215.isBusy(); }

const bc7215DataVarPkt_t* BC7215AC::getDataPkt() { return bc7215_ac_get_base_data(); }

const bc7215FormatPkt_t* BC7215AC::getFormatPkt() { return bc7215_ac_get_base_fmt(); }

const char* BC7215AC::getLibVer() { return bc7215_ac_get_ver(); }
