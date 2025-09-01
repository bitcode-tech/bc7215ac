#ifndef BC7215_H
#define BC7215_H

/******************************************************************************
*  bc7215.h
*  BC7215 Universal IR Encoder/Decoder Chip Library
*  
*  This library provides an Arduino interface for the BC7215 chip, which can:
*  - Decode infrared signals from almost all remote controllers
*  - Output original remote control data
*  - Transmit data using any IR remote protocol
*  - Simulate any remote controller
*  - Enable IR-based data communication
*  
*  See BC7215 datasheets for detailed chip specifications.
*
*  Dependencies:
*     - Arduino Serial or SoftwareSerial library for UART communication
*     - bc7215_types.h for data structure definitions
*
*  Author:
*     Bitcode
*
*  Version:
*     V1.0 March 2024
*     V2.0 July 2025
*
*  License:
*     MIT License
******************************************************************************/

#include <Arduino.h>
#include <Stream.h>
#include <bc7215_types.h>

/**
 * Buffer size calculation: data packet size + format packet size
 * When format packets are enabled, buffer accommodates both data and format packets
 * Otherwise, only data packets are buffered
 */
#if ENABLE_FORMAT == 1
#	define BC7215_BUFFER_SIZE ((BC7215_MAX_RX_DATA_SIZE + 3) + (32 + 1))
#else
#	define BC7215_BUFFER_SIZE (BC7215_MAX_RX_DATA_SIZE + 3)
#endif

/**
 * BC7215 Class - Main interface for BC7215 IR encoder/decoder chip
 * 
 * This class handles all communication with the BC7215 chip including:
 * - Mode switching (TX/RX/Shutdown)
 * - Data reception and transmission
 * - Format packet handling
 * - Pin control for MOD and BUSY signals
 */
class BC7215
{
public:
	/**
	 * Enumeration for MOD pin connection states
	 * MOD_HIGH: MOD pin tied to VCC (always high)
	 * MOD_LOW: MOD pin tied to GND (always low)
	 */
	enum MODConnect {MOD_HIGH=-1, MOD_LOW=-2};
	
	/**
	 * Enumeration for BUSY pin connection state
	 * BUSY_NC: BUSY pin not connected (not used)
	 */
	enum BUSYConnect {BUSY_NC=-3};

	/**
	 * Constructor: Full pin control
	 * @param SerialPort Reference to Serial or SoftwareSerial object for UART communication
	 * @param ModPin Digital pin number connected to BC7215 MOD pin for mode control
	 * @param BusyPin Digital pin number connected to BC7215 BUSY pin for status monitoring
	 */
	BC7215(Stream& SerialPort, int ModPin, int BusyPin);
	
	/**
	 * Constructor: Fixed pin states
	 * @param SerialPort Reference to Serial or SoftwareSerial object
	 * @param ModStat MOD pin connection state (MOD_HIGH or MOD_LOW)
	 * @param BusyStat BUSY pin connection state (BUSY_NC)
	 */
	BC7215(Stream& SerialPort, MODConnect ModStat, BUSYConnect BusyStat);
	
	/**
	 * Constructor: MOD pin control, BUSY pin not connected
	 * @param SerialPort Reference to Serial or SoftwareSerial object
	 * @param ModPin Digital pin number for MOD pin control
	 * @param BusyStat BUSY pin connection state (BUSY_NC)
	 */
	BC7215(Stream& SerialPort, int ModPin, BUSYConnect BusyStat);
	
	/**
	 * Constructor: BUSY pin control, MOD pin fixed
	 * @param SerialPort Reference to Serial or SoftwareSerial object
	 * @param ModStat MOD pin connection state (MOD_HIGH or MOD_LOW)
	 * @param BusyPin Digital pin number for BUSY pin monitoring
	 */
	BC7215(Stream& SerialPort, MODConnect ModStat, int BusyPin);

	// === Mode Control Functions ===
	
	/**
	 * Set BC7215 to transmit mode
	 * Configures the chip to transmit IR signals
	 */
	void setTx();
	
	/**
	 * Set BC7215 to receive mode
	 * Configures the chip to receive and decode IR signals
	 */
	void setRx();
	
	/**
	 * Set receive mode with specific configuration
	 * @param mode Receive mode configuration byte
	 */
	void setRxMode(uint8_t mode);
	
	/**
	 * Put BC7215 into shutdown mode for power saving
	 * Minimizes power consumption when IR functionality not needed
	 */
	void setShutDown();

	/**
	 * Check if BC7215 in busy receiving or sending
	 * @return true if receiving started but 0x7a is not received yet, or sending is not completed
	 */
	bool isBusy();

#if ENABLE_RECEIVING == 1

	// === Data Reception Functions ===
	
	/**
	 * Check if received data is ready for reading
	 * @return true if complete data packet is available
	 */
	bool dataReady();
	
	/**
	 * Clear received data buffer
	 * Marks current data as read and prepares for next reception
	 */
	void clrData();
	
	/**
	 * Get length of received data in bits
	 * @return Number of bits in the received data packet
	 */
	uint16_t getLen();
	
	/**
	 * Get size of received data packet in bytes
	 * @return Total packet size including headers
	 */
	uint16_t dpketSize();
	
	/**
	 * Retrieve received data packet (variable size)
	 * @param target Pointer to variable-size data packet structure
	 * @return Status code (0 = success)
	 */
	uint8_t getData(bc7215DataVarPkt_t* target);
	
	/**
	 * Retrieve received data packet (into a max sized target container)
	 * @param target Reference to maximum-size data packet structure
	 * @return Status code (0 = success)
	 */
	uint8_t getData(bc7215DataMaxPkt_t& target);
	
	/**
	 * Get raw received data without packet structure
	 * @param addr Memory address to store raw data
	 * @param size Maximum number of bytes to retrieve
	 * @return Actual number of bytes retrieved
	 */
	uint16_t getRaw(void* addr, uint16_t size);

#	if ENABLE_FORMAT == 1

		// === Format Packet Functions ===
		
		/**
		 * Check if format packet is ready for reading
		 * Format packets contain timing and protocol information
		 * @return true if complete format packet is available
		 */
		bool formatReady();
		
		/**
		 * Clear format packet buffer
		 * Marks current format packet as read
		 */
		void clrFormat();
		
		/**
		 * Retrieve format packet containing protocol information
		 * @param target Reference to format packet structure
		 * @return Status code (0 = success)
		 */
		uint8_t getFormat(bc7215FormatPkt_t& target);

#	endif
#endif

#if ENABLE_TRANSMITTING == 1

	// === Data Transmission Functions ===
	
	/**
	 * Load format packet for transmission
	 * Sets up timing and protocol parameters for subsequent transmissions
	 * @param source Reference to format packet containing protocol settings
	 */
	void loadFormat(const bc7215FormatPkt_t& source);
	
	/**
	 * Transmit IR data packet (variable size)
	 * @param source Pointer to variable-size data packet to transmit
	 */
	void irTx(const bc7215DataVarPkt_t* source);
	
	/**
	 * Transmit IR data packet (passed by its maximum sized container, payload data may be less)
	 * @param source Reference to maximum-size data packet to transmit
	 */
	void irTx(const bc7215DataMaxPkt_t& source);
	
	/**
	 * Send raw data without packet structure
	 * @param source Pointer to raw data buffer
	 * @param size Number of bytes to transmit
	 */
	void sendRaw(const void* source, uint16_t size);

#endif

	// === Status Functions ===
	
	/**
	 * Check if the last command has completed execution
	 * @return true if no command is currently executing
	 */
	bool cmdCompleted();

	// === Static Utility Functions ===
	
	/**
	 * Set C56K flag in format packet
	 * Enables 56kHz carrier frequency mode
	 * @param formatPkt Reference to format packet to modify
	 */
	static void setC56K(bc7215FormatPkt_t& formatPkt);
	
	/**
	 * Clear C56K flag in format packet
	 * Disables 56kHz carrier frequency mode
	 * @param formatPkt Reference to format packet to modify
	 */
	static void clrC56K(bc7215FormatPkt_t& formatPkt);
	
	/**
	 * Set NOCA (No Carrier) flag in format packet
	 * Disables carrier signal for baseband transmission
	 * @param formatPkt Reference to format packet to modify
	 */
	static void setNOCA(bc7215FormatPkt_t& formatPkt);
	
	/**
	 * Clear NOCA flag in format packet
	 * Enables carrier signal for normal IR transmission
	 * @param formatPkt Reference to format packet to modify
	 */
	static void clrNOCA(bc7215FormatPkt_t& formatPkt);

	/**
	 * Calculate 8-bit CRC checksum
	 * @param data Pointer to data buffer
	 * @param len Length of data in bytes
	 * @return Calculated CRC8 checksum
	 */
	static uint8_t crc8(const void* data, uint16_t len);
	
	/**
	 * Calculate size of variable data packet
	 * @param dataPkt Pointer to variable data packet
	 * @return Total packet size in bytes
	 */
	static uint16_t calSize(const bc7215DataVarPkt_t* dataPkt);
	
	/**
	 * Calculate size of fixed maximum data packet
	 * @param dataPkt Reference to maximum data packet
	 * @return Total packet size in bytes
	 */
	static uint16_t calSize(const bc7215DataMaxPkt_t& dataPkt);
	
	/**
	 * Copy variable data packet to memory buffer
	 * @param target Destination memory address
	 * @param source Pointer to source variable data packet
	 */
	static void copyDpkt(void* target, bc7215DataVarPkt_t* source);
	
	/**
	 * Copy fixed maximum data packet to memory buffer
	 * @param target Destination memory address
	 * @param source Reference to source maximum data packet
	 */
	static void copyDpkt(void* target, bc7215DataMaxPkt_t& source);
	
	/**
	 * Compare two variable data packets
	 * @param sig Signature/identifier to compare
	 * @param pkt1 Pointer to first data packet
	 * @param pkt2 Pointer to second data packet
	 * @return true if packets match according to signature
	 */
	static bool compareDpkt(uint8_t sig, const bc7215DataVarPkt_t* pkt1, const bc7215DataVarPkt_t* pkt2);
	
	/**
	 * Compare two fixed maximum data packets
	 * @param sig Signature/identifier to compare
	 * @param pkt1 Reference to first data packet
	 * @param pkt2 Reference to second data packet
	 * @return true if packets match according to signature
	 */
	static bool compareDpkt(uint8_t sig, const bc7215DataMaxPkt_t& pkt1, const bc7215DataMaxPkt_t& pkt2);

private:
	// === Private Member Variables ===
	
	Stream&         uart;           ///< Reference to serial communication stream
	int             modPin;         ///< Digital pin number for MOD signal control
	int             busyPin;        ///< Digital pin number for BUSY signal monitoring
	
	/**
	 * Status flags structure for tracking BC7215 state
	 */
	struct
	{
		uint8_t formatPktReady : 1;  ///< Format packet ready for reading
		uint8_t dataPktReady : 1;    ///< Data packet ready for reading
		uint8_t pktStarted : 1;      ///< Packet reception in progress
		uint8_t overLap : 1;         ///< Buffer overlap condition detected
		uint8_t cmdComplete : 1;     ///< Last command execution completed
	} bc7215Status;

#if ENABLE_RECEIVING == 1

	uint16_t bitLen;                            ///< Length of received data in bits
	uint8_t circularBuffer[BC7215_BUFFER_SIZE]; ///< Circular buffer for received data

	// Buffer management variables (size depends on buffer size)
#if BC7215_BUFFER_SIZE > 255
	uint16_t       startPos;        ///< Start position in circular buffer
	uint16_t       datStartPos;     ///< Data start position in buffer
	uint16_t       lastWritingPos;  ///< Last write position in buffer
	uint16_t       datEndPos;       ///< Data end position in buffer
	uint16_t       byteCount;       ///< Total bytes in buffer
	uint16_t       datCount;        ///< Data bytes count
	
	/**
	 * Read byte from circular buffer (backward direction)
	 * @param pos Position to read from
	 * @param n Offset from position
	 * @return Byte value at specified position
	 */
	uint8_t bufBackRead(uint16_t pos, uint16_t n);
	
	/**
	 * Read byte from circular buffer (forward direction)
	 * @param pos Position to read from
	 * @param n Offset from position
	 * @return Byte value at specified position
	 */
	uint8_t bufRead(uint16_t pos, uint16_t n);
#else
	uint8_t        startPos;        ///< Start position in circular buffer
	uint8_t        datStartPos;     ///< Data start position in buffer
	uint8_t        lastWritingPos;  ///< Last write position in buffer
	uint8_t        datEndPos;       ///< Data end position in buffer
	uint8_t        byteCount;       ///< Total bytes in buffer
	uint8_t        datCount;        ///< Data bytes count
	
	/**
	 * Read byte from circular buffer (backward direction)
	 * @param pos Position to read from
	 * @param n Offset from position
	 * @return Byte value at specified position
	 */
	uint8_t bufBackRead(uint8_t pos, uint8_t n);
	
	/**
	 * Read byte from circular buffer (forward direction)
	 * @param pos Position to read from
	 * @param n Offset from position
	 * @return Byte value at specified position
	 */
	uint8_t bufRead(uint8_t pos, uint8_t n);
#endif

#endif

	// === Private Member Functions ===
	
	/**
	 * Process incoming data byte from BC7215
	 * Handles packet parsing and buffer management
	 * @param data Received data byte
	 */
	void processData(uint8_t data);
	
	/**
	 * Send byte with byte stuffing protocol
	 * Implements escape sequences for special control bytes
	 * @param data Byte to send with stuffing
	 */
	void byteStuffingSend(uint8_t data);
	
	/**
	 * Send single byte to BC7215 via UART
	 * @param data Byte to transmit
	 */
	void sendOneByte(uint8_t data);
	
	/**
	 * Update internal status flags based on current state
	 * Maintains synchronization between hardware and software status
	 */
	void statusUpdate();
};

#endif
