#ifndef BC7215AC_H
#define BC7215AC_H

#include <Arduino.h>
#include <bc7215.h>
#include <bc7215_ac_lib.h>

class BC7215AC
{
public:
    BC7215AC(BC7215& bc7215Driver);
    void                      startCapture();
    void                      stopCapture();
    bool                      signalCaptured();
    bool                      signalCaptured(bc7215DataVarPkt_t* targetData,
                             bc7215FormatPkt_t*
                                 targetFormat);        // capture and store received data in designated target, return value is status
    bool                      init();
	bool					  init(const bc7215DataMaxPkt_t& data, const bc7215FormatPkt_t& format);
    bool                      init(const bc7215DataVarPkt_t* data, const bc7215FormatPkt_t* format);
    bool                      init(uint8_t cnt, bc7215DataMaxPkt_t const data[], bc7215FormatPkt_t const format[]);
    bool                      matchNext();
    uint8_t                   extraSample();
    bool                      saveExtra(const bc7215DataVarPkt_t* data, const bc7215FormatPkt_t* format);
    bool                      saveExtra(const bc7215DataMaxPkt_t& data, const bc7215FormatPkt_t& format);
	bc7215CombinedMsg_t		  getExtra();
    uint8_t                   cntPredef();
    const char*               getPredefName(uint8_t index);
    bool                      initPredef(uint8_t index);
    const bc7215DataVarPkt_t* setTo(int tempC, int mode = -1, int fan = -1, int key = 0);
    const bc7215DataVarPkt_t* on();
    const bc7215DataVarPkt_t* off();
    bool                      isBusy();
    const bc7215DataVarPkt_t* getDataPkt();
    const bc7215FormatPkt_t*  getFormatPkt();
	const char*				  getLibVer();

private:
    BC7215&             bc7215;
    bc7215FormatPkt_t   rcvdFmt;
    bc7215DataMaxPkt_t  rcvdData;
    bc7215CombinedMsg_t rcvdMessage[4];
    uint8_t             rcvdStatus;
    bool                sampleReady;
	bool				initOK;
    void                sendAcCmd(const bc7215DataVarPkt_t* dataPkt);
};

#endif
