#ifndef BC7215AC_H
#define BC7215AC_H

#include <Arduino.h>
#include <bc7215.h>
#include <bc7215_ac_lib.h>

class BC7215AC
{
public:
    BC7215AC(BC7215& bc7215Driver);
	bool				initOK;					// is the library is currently initialized
	uint8_t				sampleCount;			// how many samples have been captured
	uint8_t				sampleStatus[4];		// Storage for captured data status
	bc7215DataMaxPkt_t  sampleData[4];          // Storage for captured IR data
	bc7215FormatPkt_t   sampleFormat[4];        // Storage for captured IR format

	// Set system temperature unit
	void					  setFahrenheit();
	void					  setCelsius();
	
	// Start IR signal capturing (enter RX mode)
    void                      startCapture();

	// Stop capturing, exit RX mode
    void                      stopCapture();

	// Check if IR signal has been successfully captured
    bool                      signalCaptured();

	// Initialize(pair) A/C library with last captured data & format
    bool                      init();

	// Initialize(pair) A/C library with 'data' and 'format'
	bool					  init(const bc7215DataMaxPkt_t& data, const bc7215FormatPkt_t& format);

	// Try to find next matched protocol
    bool                      matchNext();
	
	// Get the count of pre-defined(built-in) protocols
    uint8_t                   cntPredef();

	// Get the reference name of a pre-defined protocol
    const char*               getPredefName(uint8_t index);

	// Using pre-defined(built-in) protocol to initialize the library
    bool                      initPredef(uint8_t index);

	// Transmit IR to set A/C to particular settings
    const bc7215DataVarPkt_t* setTo(int temp, int mode = -1, int fan = -1, int key = 0);

	// Transmit IR to turn On/Off the A/C
    const bc7215DataVarPkt_t* on();
    const bc7215DataVarPkt_t* off();

	// Parsing the last captured IR signal
	bool					  parse(int& temp, int& mode, int& fan, int& power);
    
	// Check if the BC7215A is busing receiving or transmitting
	bool                      isBusy();

	// Check if library is current in Celsius mode
	bool					  isCelsius();

	// Get the base data packet
    const bc7215DataVarPkt_t* getDataPkt();

	// Get the base format packet
    const bc7215FormatPkt_t*  getFormatPkt();

	// Get the version of the A/C Control Library
	const char*				  getLibVer();

private:
    BC7215&             bc7215;
    bc7215CombinedMsg_t rcvdMessage[4];
	unsigned long		timerStartTime;
	bool				isCapturing;
	bool				useFahrenheit;			// is system temperature Fahrenheit
    void                sendAcCmd(const bc7215DataVarPkt_t* dataPkt);
};

#endif
