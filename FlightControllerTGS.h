
#include <Arduino.h>

#ifdef __AVR__
#error "Sorry, FlightControllerTGS does not work on Teensy 2.0 and other AVR-based boards"
#elif defined(__IMXRT1062__)
#include "FlightControllerTGSIMXRT.h"
#else 

#define FlightControllerTGS_MAXCHANNELS 16

struct ftm_channel_struct {
	uint32_t csc;
	uint32_t cv;
};

class FlightControllerTGSOutput
{
public:
	FlightControllerTGSOutput(void);
	FlightControllerTGSOutput(int polarity);
	bool begin(uint8_t txPin); // txPin can be 5,6,9,10,20,21,22,23
	bool begin(uint8_t txPin, uint8_t framePin);
	bool write(uint8_t channel, float microseconds);
	friend void ftm0_isr(void);
private:
	void isr(void);
	uint32_t pulse_width[FlightControllerTGS_MAXCHANNELS+1];
	uint32_t pulse_buffer[FlightControllerTGS_MAXCHANNELS+1];
	uint32_t pulse_remaining;
	volatile uint8_t *framePinReg;
	volatile uint8_t framePinMask;
	struct ftm_channel_struct *ftm;
	uint8_t state;
	uint8_t current_channel;
	uint8_t total_channels;
	uint8_t total_channels_buffer;
	uint8_t cscSet;
	uint8_t cscClear;
	static FlightControllerTGSOutput *list[8];
	static uint8_t channelmask;
};


class FlightControllerTGSInput
{
public:
	FlightControllerTGSInput(void);
	FlightControllerTGSInput(int polarity);
	bool begin(uint8_t rxPin); // rxPin can be 5,6,9,10,20,21,22,23
	int available(void);
	float read(uint8_t channel);
	friend void ftm0_isr(void);
private:
	void isr(void);
	struct ftm_channel_struct *ftm;
	uint32_t pulse_width[FlightControllerTGS_MAXCHANNELS];
	uint32_t pulse_buffer[FlightControllerTGS_MAXCHANNELS];
	uint32_t prev;
	uint8_t write_index;
	uint8_t total_channels;
	uint8_t cscEdge;
	bool available_flag;
	static bool overflow_inc;
	static uint16_t overflow_count;
	static FlightControllerTGSInput *list[8];
	static uint8_t channelmask;
};

#endif
