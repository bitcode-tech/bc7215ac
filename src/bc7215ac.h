#ifndef BC7215AC_H
#define BC7215AC_H

#include <Arduino.h>
#include <bc7215.h>
#include <bc7215_ac_lib/bc7215_ac_lib.h>

class BC7215AC
{
private:
    // Maximum number of IR frames retained during pairing/parsing.
    static constexpr uint8_t kMaxSamples = 4;
    // Once at least one frame is received, this idle time marks the end of the
    // current capture sequence.
    static constexpr uint32_t kCaptureIdleMs = 200;
    // Delay time for different situations.
    static constexpr uint32_t kModeSwitchDelayMs = 50;
    static constexpr uint32_t kCmdIntervalDelayMs = 200;
    static constexpr uint32_t kTxCheckIntervalMs = 50;
    static constexpr uint32_t kTransmitTimoutLoops = 60;
    // Status bits returned by the low-level library. Reverse means the received
    // data bits need to be inverted before passing them to the AC protocol library.
    static constexpr uint8_t kReverseStatusBit = 0x40;
    static constexpr uint8_t kErrStatusBit = 0x80;

public:
    BC7215AC(BC7215& bc7215Driver);
    // True only after the AC protocol library has been successfully paired or
    // initialized from saved/predefined protocol data. Command functions return
    // nullptr while this flag is false.
    bool initOK = false;
    // Captured sample buffers. Some AC protocols require more than one IR frame
    // for reliable identification, so up to kMaxSamples frames are stored.
    uint8_t            fmtPktCnt = 0;
    uint8_t            datPktCnt = 0;
    uint8_t            sampleStatus[kMaxSamples] = {};
    bc7215DataMaxPkt_t sampleData[kMaxSamples] = {};
    bc7215FormatPkt_t  sampleFormat[kMaxSamples] = {};

    // Set system temperature unit
    void setFahrenheit();
    void setCelsius();

    // Start IR signal capturing (enter RX mode)
    void startCapture(uint8_t rx_mode);

    // Stop capturing, exit RX mode
    void stopCapture();

    // Check if IR signal has been successfully captured
    bool signalCaptured();

    // Initialize(pair) A/C library with last captured data & format
    bool init();

    // Initialize(pair) A/C library with 'data' and 'format'
    bool init(uint8_t status, const bc7215DataMaxPkt_t& data, const bc7215FormatPkt_t& format);

    // Try to find next matched protocol
    bool matchNext();

    // Get the count of pre-defined(built-in) protocols
    uint8_t cntPredef() const;

    // Get the reference name of a pre-defined protocol
    const char* getPredefName(uint8_t index) const;

    // Using pre-defined(built-in) protocol to initialize the library
    bool initPredef(uint8_t index);

    // Transmit IR to set A/C to particular settings
    const bc7215DataVarPkt_t* setTo(int temp, int mode = -1, int fan = -1, int key = 0);

    // Transmit IR to turn On/Off the A/C
    const bc7215DataVarPkt_t* on();
    const bc7215DataVarPkt_t* off();

    // Parsing the last captured IR signal
    bool parse(int& temp, int& mode, int& fan, int& power);

    // Use the last captured IR signal to replace the current data base
    // Returns false if the replacement was not successful.
    bool replaceBase();

    // Check if the BC7215A is busing receiving or transmitting
    bool isBusy() const;

    // Check if library is current in Celsius mode
    bool isCelsius() const;

    // Get the status byte of base data packet
    uint8_t statusByte() const;

    // Get the base data packet
    const bc7215DataVarPkt_t* getDataPkt() const;

    // Get the base format packet
    const bc7215FormatPkt_t* getFormatPkt() const;

    // Get the version of the A/C Control Library
    const char* getLibVer() const;

private:
    BC7215& bc7215;
    // Combined-message descriptors link each captured format packet to its
    // corresponding data packet without copying both into another large buffer.
    const bc7215CombinedMsg_t rcvdMessage[kMaxSamples] = {
        { .bitLen = 0,
            .body
            = { .msg = { .fmt = &sampleFormat[0], .datPkt = reinterpret_cast<bc7215DataVarPkt_t*>(&sampleData[0]) } } },
        { .bitLen = 0,
            .body
            = { .msg = { .fmt = &sampleFormat[1], .datPkt = reinterpret_cast<bc7215DataVarPkt_t*>(&sampleData[1]) } } },
        { .bitLen = 0,
            .body
            = { .msg = { .fmt = &sampleFormat[2], .datPkt = reinterpret_cast<bc7215DataVarPkt_t*>(&sampleData[2]) } } },
        { .bitLen = 0,
            .body
            = { .msg = { .fmt = &sampleFormat[3], .datPkt = reinterpret_cast<bc7215DataVarPkt_t*>(&sampleData[3]) } } }
    };

    unsigned long timerStartTime;
    int8_t        current_temp = 25;
    int8_t        current_mode = 1;
    int8_t        current_fan = 1;
    int8_t        current_key = 3;
    bool          useFahrenheit;        // is system temperature Fahrenheit
    bool          captureFmtPkt;        // if format packet is expected
    void          sendAcCmd(const bc7215DataVarPkt_t* dataPkt);
    bool          reverseMarkedSamples();
    bool          copyPredefined(uint8_t index);
};

#endif
