#include "bc7215ac.h"

// Constructor: Initializes the BC7215 driver reference and sets the chip to Tx mode by default.
BC7215AC::BC7215AC(BC7215& bc7215Chip)
    : bc7215(bc7215Chip)
{
    bc7215.setTx();
    initOK = false;
    useFahrenheit = false;        // Default to Celsius
}

// Sets the temperature unit to Fahrenheit.
void BC7215AC::setFahrenheit()
{
    // Reset the initialization state if the temperature unit changes,
    // as underlying protocol data may differ.
    if (!useFahrenheit)
    {
        initOK = false;
    }
    useFahrenheit = true;
}

// Sets the temperature unit to Celsius.
void BC7215AC::setCelsius()
{
    // Reset the initialization state if the temperature unit changes.
    if (useFahrenheit)
    {
        initOK = false;
    }
    useFahrenheit = false;
}

// Starts IR signal capture (enters Rx mode).
void BC7215AC::startCapture(uint8_t rx_mode)
{
    bc7215.setRx();
    delay(kModeSwitchDelayMs);        // Delay to allow the mode switch to stabilize
    bc7215.setRxMode(rx_mode);

    // Check if complex mode is enabled (LSB is 1).
    // In complex mode, both data and format packets are captured.
    if (rx_mode & 0x01)
    {
        captureFmtPkt = true;
    }
    else
    {
        captureFmtPkt = false;
    }

    // Clear old buffers and reset counters/timers.
    bc7215.clrData();
    bc7215.clrFormat();
    fmtPktCnt = 0;
    datPktCnt = 0;
    timerStartTime = millis();
}

// Stops capturing, exits Rx mode, and switches back to Tx mode.
void BC7215AC::stopCapture()
{
    bc7215.setTx();
    delay(kModeSwitchDelayMs);        // Delay to allow the mode switch to complete
}

// Polls to check if a complete IR signal has been successfully captured.
bool BC7215AC::signalCaptured()
{
    // Check if a data packet is ready
    if (bc7215.dataReady())
    {
        // Ensure we do not exceed the maximum allowed samples
        if (datPktCnt < kMaxSamples)
        {
            // Read the data and store the returned status byte based on the capture mode
            if (captureFmtPkt)
            {
                sampleStatus[fmtPktCnt] = bc7215.getData(sampleData[fmtPktCnt]);
            }
            else
            {
                sampleStatus[datPktCnt] = bc7215.getData(sampleData[datPktCnt]);
            }
            datPktCnt++;

            // Check if a format packet is also ready
            if (bc7215.formatReady())
            {
                if (captureFmtPkt)
                {
                    bc7215.getFormat(sampleFormat[fmtPktCnt]);
                }
                else
                {
                    bc7215.getFormat(sampleFormat[datPktCnt]);
                }
                fmtPktCnt++;

                // Synchronization: In complex mode, the data packet arrives before the format packet.
                // dataReady() may return 'true' twice (once for data, once for format).
                // We synchronize the data packet counter with the format packet counter here.
                datPktCnt = fmtPktCnt;
            }
        }
        timerStartTime = millis();        // Reset idle timer on new data arrival
    }

    // Reset idle timer if the chip is busy receiving data
    if (bc7215.isBusy())
    {
        timerStartTime = millis();
    }

    // If at least one packet is received and the idle time exceeds the threshold (e.g., 200ms),
    // consider the current IR command capture sequence complete.
    if ((datPktCnt > 0) && (millis() - timerStartTime > kCaptureIdleMs))
    {
        return true;
    }
    return false;
}

// Transmits the low-level AC IR command.
void BC7215AC::sendAcCmd(const bc7215DataVarPkt_t* dataPkt)
{
    // bitLen == 0 acts as a special flag indicating a combined message packet
    if (dataPkt->bitLen == 0)
    {
        // Extract and load both format and data packets from the combined structure
        bc7215.loadFormat(*(reinterpret_cast<const bc7215CombinedMsg_t*>(dataPkt)->body.msg.fmt));
        bc7215.irTx(reinterpret_cast<const bc7215CombinedMsg_t*>(dataPkt)->body.msg.datPkt);
    }
    else
    {
        // For standard data packets, load the base format and transmit
        bc7215.loadFormat(*bc7215_ac_get_base_fmt());
        bc7215.irTx(dataPkt);
    }
}

// Initializes (pairs) the AC library using the last captured signal data and format.
bool BC7215AC::init()
{
    uint8_t pkt0_rev;
    initOK = false;

    // Single-frame protocol initialization
    if (fmtPktCnt == 1)
    {
        // Abort if the status contains an error flag
        if (sampleStatus[0] & kErrStatusBit)
        {
            return false;
        }
        // Call the appropriate lower-level init function based on temperature unit
        if (useFahrenheit)
        {
            initOK = bc7215_ac_init_f(sampleStatus[0], reinterpret_cast<const bc7215DataVarPkt_t*>(&rcvdMessage[0]));
        }
        else
        {
            initOK = bc7215_ac_init(sampleStatus[0], reinterpret_cast<const bc7215DataVarPkt_t*>(&rcvdMessage[0]));
        }
    }
    // Multi-frame protocol initialization (some ACs require 2 or more frames for identification)
    else if ((fmtPktCnt > 1) && (fmtPktCnt <= kMaxSamples))
    {
        pkt0_rev = sampleStatus[0] & kReverseStatusBit;
        // Handle inverted data bits if necessary
        if (!reverseMarkedSamples())
        {
            return false;
        }
        if (useFahrenheit)
        {
            initOK = bc7215_ac_init2_f(fmtPktCnt | pkt0_rev, rcvdMessage, 0);
        }
        else
        {
            initOK = bc7215_ac_init2(fmtPktCnt | pkt0_rev, rcvdMessage, 0);
        }
    }
    return initOK;
}

// Initializes (pairs) the AC library with explicitly provided status, data, and format.
bool BC7215AC::init(uint8_t status, const bc7215DataMaxPkt_t& data, const bc7215FormatPkt_t& format)
{
    sampleData[0] = data;
    sampleFormat[0] = format;
    sampleStatus[0] = status;
    fmtPktCnt = 1;

    if (useFahrenheit)
    {
        initOK = bc7215_ac_init_f(sampleStatus[0], reinterpret_cast<const bc7215DataVarPkt_t*>(&rcvdMessage[0]));
    }
    else
    {
        initOK = bc7215_ac_init(sampleStatus[0], reinterpret_cast<const bc7215DataVarPkt_t*>(&rcvdMessage[0]));
    }
    return initOK;
}

// Attempts to find the next matching protocol in the underlying library database.
bool BC7215AC::matchNext()
{
    if (initOK)
    {
        initOK = bc7215_ac_find_next();
    }
    return initOK;
}

// Returns the total count of predefined (built-in) protocols.
uint8_t BC7215AC::cntPredef() const { return bc7215_ac_predefined_cnt(); }

// Gets the reference name of a predefined protocol by its index.
const char* BC7215AC::getPredefName(uint8_t index) const
{
    if (index < bc7215_ac_predefined_cnt())
    {
        return bc7215_ac_predefined_name(index);
    }
    return NULL;
}

// Initializes the library using a specific predefined protocol index.
bool BC7215AC::initPredef(uint8_t index)
{
    bool result;
    initOK = false;
    if (index < cntPredef())
    {
        // Copy the built-in protocol data into the capture buffers
        if (!copyPredefined(index))
        {
            return false;
        }
        // Re-use standard initialization logic
        if (useFahrenheit)
        {
            initOK = bc7215_ac_init_f(sampleStatus[0], reinterpret_cast<const bc7215DataVarPkt_t*>(&rcvdMessage[0]));
        }
        else
        {
            initOK = bc7215_ac_init(sampleStatus[0], reinterpret_cast<const bc7215DataVarPkt_t*>(&rcvdMessage[0]));
        }
    }
    return initOK;
}

// Transmits IR command to set AC temperature, mode, fan speed, and key state.
const bc7215DataVarPkt_t* BC7215AC::setTo(int temp, int mode, int fan, int key)
{
    const bc7215DataVarPkt_t* dataPkt = nullptr;
    if (initOK)
    {
        // Cache current state
        current_temp = static_cast<int8_t>(temp);
        current_mode = static_cast<int8_t>(mode);
        current_fan = static_cast<int8_t>(fan);
        current_key = static_cast<int8_t>(key);

        // The underlying library requires a temperature offset:
        // Fahrenheit offset is 60, Celsius offset is 16.
        if (useFahrenheit)
        {
            dataPkt = bc7215_ac_set_f(current_temp - 60, current_mode, current_fan, current_key);
        }
        else
        {
            dataPkt = bc7215_ac_set(current_temp - 16, current_mode, current_fan, current_key);
        }
        sendAcCmd(dataPkt);
        return dataPkt;
    }
    return nullptr;
}

// Transmits an IR command to turn on the AC.
const bc7215DataVarPkt_t* BC7215AC::on()
{
    const bc7215DataVarPkt_t* dataPkt = nullptr;
    if (initOK)
    {
        dataPkt = bc7215_ac_on();
        // Some protocols lack a dedicated "ON" command packet.
        // In this case, fallback to sending a standard parameter setup packet to turn it on.
        if (dataPkt == nullptr)
        {
            dataPkt = setTo(current_temp, current_mode, current_fan, current_key);
        }
        sendAcCmd(dataPkt);
    }
    return dataPkt;
}

// Transmits an IR command to turn off the AC.
const bc7215DataVarPkt_t* BC7215AC::off()
{
    const bc7215DataVarPkt_t* dataPkt = nullptr;
    if (initOK);
    {
        dataPkt = bc7215_ac_off();
        sendAcCmd(dataPkt);
    }
    return dataPkt;
}

// Parses the last captured IR signal to extract temperature, mode, fan speed, and power status.
bool BC7215AC::parse(int& temp, int& mode, int& fan, int& power)
{
    bool                      result = false;
    bool                      rep_result;
    uint8_t                   tempStatus;
    bc7215DataMaxPkt_t        tempPkt;
    const bc7215DataVarPkt_t* basePkt;

    if (initOK)
    {
        // Backup current base status and data before attempting replacement,
        // to prevent breaking the existing state if parsing fails.
        tempStatus = bc7215_ac_get_base_status();
        basePkt = bc7215_ac_get_base_data();
        tempPkt.bitLen = basePkt->bitLen;
        for (int i = 0; i < (tempPkt.bitLen + 7) / 8; i++)
        {
            tempPkt.data[i] = basePkt->data[i];
        }

        // Attempt to replace the current base protocol with the captured signal
        if (datPktCnt == 1)
        {
            rep_result
                = bc7215_ac_replace_base(sampleStatus[0], reinterpret_cast<const bc7215DataVarPkt_t*>(&sampleData[0]));
        }
        else if ((datPktCnt > 1) && (datPktCnt <= kMaxSamples))
        {
            uint8_t pkt0_rev = sampleStatus[0] & kReverseStatusBit;
            if (!reverseMarkedSamples())
            {
                return false;
            }
            rep_result = bc7215_ac_replace_base(
                datPktCnt | pkt0_rev, reinterpret_cast<const bc7215DataVarPkt_t*>(rcvdMessage));
        }
        else
        {
            return false;
        }

        // Parse the parameters
        int8_t t = 0, m = 0, f = 0, p = 0;
        if (useFahrenheit)
        {
            result = bc7215_ac_parse_f(&t, &m, &f, &p);
            t += 60;        // Restore Fahrenheit offset
        }
        else
        {
            result = bc7215_ac_parse(&t, &m, &f, &p);
            t += 16;        // Restore Celsius offset
        }

        // If parsed successfully, update local cached state
        if (result)
        {
            current_temp = t;
            current_mode = m;
            current_fan = f;
        }

        // If the replacement was unsuccessful (signal doesn't match protocol),
        // restore the previously backed-up base data.
        if (!rep_result)
        {
            bc7215_ac_replace_base(tempStatus, reinterpret_cast<const bc7215DataVarPkt_t*>(&tempPkt));
        }

        // Assign values to reference variables
        temp = static_cast<int>(t);
        mode = static_cast<int>(m);
        fan = static_cast<int>(f);
        power = static_cast<int>(p);
    }
    return result;
}

// Forcibly replaces the current base protocol data with the latest captured signal.
bool BC7215AC::replaceBase()
{
    uint8_t pkt0_rev;
    bool    rep_result;

    if (!initOK)
    {
        return false;
    }

    // Perform base replacement for single or multi-frame captures
    if (datPktCnt == 1)
    {
        if (!(sampleStatus[0] & kErrStatusBit))
        {
            rep_result
                = bc7215_ac_replace_base(sampleStatus[0], reinterpret_cast<const bc7215DataVarPkt_t*>(&sampleData[0]));
        }
        else
        {
            return false;
        }
    }
    else if ((datPktCnt > 1) && (datPktCnt <= kMaxSamples))
    {
        // Clean up multi-frame captures by handling potential bit reversal statuses
        pkt0_rev = sampleStatus[0] & kReverseStatusBit;
        if (!reverseMarkedSamples())
        {
            return false;
        }
        rep_result
            = bc7215_ac_replace_base(datPktCnt | pkt0_rev, reinterpret_cast<const bc7215DataVarPkt_t*>(rcvdMessage));
    }
    else
    {
        return false;        // Invalid packet count
    }
    return rep_result;
}

// Inverts bits for samples marked with the reverse status bit (kReverseStatusBit)
// to ensure consistency during parsing.
bool BC7215AC::reverseMarkedSamples()
{
    uint8_t pkt0_rev = sampleStatus[0] & kReverseStatusBit;
    uint8_t pkt_cnt;

    if (captureFmtPkt)
    {
        pkt_cnt = fmtPktCnt;
    }
    else
    {
        pkt_cnt = datPktCnt;
    }

    for (uint8_t j = 0; j < pkt_cnt && j < kMaxSamples; ++j)
    {
        // Abort if an error status is found
        if (sampleStatus[j] & kErrStatusBit)
        {
            return false;
        }
        // Skip if this is the first frame or if its polarity relative to the first frame hasn't changed
        if ((j == 0) || ((sampleStatus[j] & kReverseStatusBit) == pkt0_rev))
        {
            continue;
        }

        // Perform bitwise NOT on the data
        uint16_t bytes = (sampleData[j].bitLen + 7) / 8;
		if (bytes > BC7215_MAX_RX_DATA_SIZE)
		{
			bytes = BC7215_MAX_RX_DATA_SIZE;
		}
        for (uint16_t i = 0; i < bytes; ++i)
        {
            sampleData[j].data[i] = ~sampleData[j].data[i];
        }
        // Clear the reverse status flag after inversion
        sampleStatus[j] = sampleStatus[j] ^ kReverseStatusBit;
    }
    return true;
}

// Internal helper: Copies predefined protocol data into the capture buffer based on the provided index.
bool BC7215AC::copyPredefined(uint8_t index)
{
    const bc7215DataVarPkt_t* src_data
        = useFahrenheit ? bc7215_ac_predefined_data_f(index) : bc7215_ac_predefined_data(index);
    const bc7215FormatPkt_t* src_format = bc7215_ac_predefined_fmt(index);

    if (src_data == nullptr || src_format == nullptr)
    {
        return false;
    }

    const uint16_t copy_bytes = (src_data->bitLen + 7) / 8;
    if (copy_bytes == 0 || copy_bytes > sizeof(sampleData[0]))
    {
        return false;
    }

    sampleData[0].bitLen = src_data->bitLen;
    memcpy(&sampleData[0].data, src_data->data, copy_bytes);
    sampleFormat[0] = *src_format;
    sampleStatus[0] = sampleFormat[0].signature.bits.sig;        // Extract initial status from format signature
    fmtPktCnt = 1;
    return true;
}

// Checks if the underlying chip is busy receiving or transmitting data.
bool BC7215AC::isBusy() const { return bc7215.isBusy(); }

// Checks if the library is currently operating in Celsius mode.
bool BC7215AC::isCelsius() const { return !useFahrenheit; }

// Returns the status byte of the base data packet.
uint8_t BC7215AC::statusByte() const { return bc7215_ac_get_base_status(); }

// Returns the base data packet used for communication.
const bc7215DataVarPkt_t* BC7215AC::getDataPkt() const { return bc7215_ac_get_base_data(); }

// Returns the base format packet used for communication.
const bc7215FormatPkt_t* BC7215AC::getFormatPkt() const { return bc7215_ac_get_base_fmt(); }

// Returns the version string of the underlying AC Control Library.
const char* BC7215AC::getLibVer() const { return bc7215_ac_get_ver(); }
