
#if defined(__arm__) && defined(TEENSYDUINO) && (defined(__MKL26Z64__) || defined(__MK20DX128__) || defined(__MK20DX256__) || defined(__MK64FX512__) || defined(__MK66FX1M0__))


#include "FlightControllerTGS.h"


// Timing parameters, in microseconds.


// The shortest time allowed between any 2 rising edges.  This should be at
// least double TX_PULSE_WIDTH.
#define TX_MINIMUM_SIGNAL   300.0

// The longest time allowed between any 2 rising edges for a normal signal.
#define TX_MAXIMUM_SIGNAL  2500.0

// The default signal to send if nothing has been written.
#define TX_DEFAULT_SIGNAL  1500.0

// When transmitting with a single pin, the minimum space signal that marks
// the end of a frame.  Single wire receivers recognize the end of a frame
// by looking for a gap longer than the maximum data size.  When viewing the
// waveform on an oscilloscope, set the trigger "holdoff" time to slightly
// less than TX_MINIMUM_SPACE, for the most reliable display.  This parameter
// is not used when transmitting with 2 pins.
#define TX_MINIMUM_SPACE   5000.0

// The minimum total frame size.  Some servo motors or other devices may not
// work with pulses the repeat more often than 50 Hz.  To allow transmission
// as fast as possible, set this to the same as TX_MINIMUM_SIGNAL.
#define TX_MINIMUM_FRAME  20000.0

// The length of all transmitted pulses.  This must be longer than the worst
// case interrupt latency, which depends on how long any other library may
// disable interrupts.  This must also be no more than half TX_MINIMUM_SIGNAL.
// Most libraries disable interrupts for no more than a few microseconds.
// The OneWire library is a notable exception, so this may need to be lengthened
// if a library that imposes unusual interrupt latency is in use.
#define TX_PULSE_WIDTH      100.0

// When receiving, any time between rising edges longer than this will be
// treated as the end-of-frame marker.
#define RX_MINIMUM_SPACE   3500.0


// convert from microseconds to I/O clock ticks
#if defined(KINETISK)
#define CLOCKS_PER_MICROSECOND ((double)F_BUS / 1000000.0)
#elif defined(KINETISL)
#define CLOCKS_PER_MICROSECOND ((double)F_PLL / 2000000.0)
#endif
#define TX_MINIMUM_SIGNAL_CLOCKS  (uint32_t)(TX_MINIMUM_SIGNAL * CLOCKS_PER_MICROSECOND)
#define TX_MAXIMUM_SIGNAL_CLOCKS  (uint32_t)(TX_MAXIMUM_SIGNAL * CLOCKS_PER_MICROSECOND)
#define TX_DEFAULT_SIGNAL_CLOCKS  (uint32_t)(TX_DEFAULT_SIGNAL * CLOCKS_PER_MICROSECOND)
#define TX_MINIMUM_SPACE_CLOCKS   (uint32_t)(TX_MINIMUM_SPACE * CLOCKS_PER_MICROSECOND)
#define TX_MINIMUM_FRAME_CLOCKS   (uint32_t)(TX_MINIMUM_FRAME * CLOCKS_PER_MICROSECOND)
#define TX_PULSE_WIDTH_CLOCKS     (uint32_t)(TX_PULSE_WIDTH * CLOCKS_PER_MICROSECOND)
#define RX_MINIMUM_SPACE_CLOCKS   (uint32_t)(RX_MINIMUM_SPACE * CLOCKS_PER_MICROSECOND)


#define FTM0_SC_VALUE (FTM_SC_TOIE | FTM_SC_CLKS(1) | FTM_SC_PS(0))

#if defined(KINETISK)
#define CSC_CHANGE(reg, val)         ((reg)->csc = (val))
#define CSC_INTACK(reg, val)         ((reg)->csc = (val))
#define CSC_CHANGE_INTACK(reg, val)  ((reg)->csc = (val))
#define FRAME_PIN_SET()              *framePinReg = 1
#define FRAME_PIN_CLEAR()            *framePinReg = 0
#elif defined(KINETISL)
#define CSC_CHANGE(reg, val)         ({(reg)->csc = 0; while ((reg)->csc); (reg)->csc = (val);})
#define CSC_INTACK(reg, val)         ((reg)->csc = (val) | FTM_CSC_CHF)
#define CSC_CHANGE_INTACK(reg, val)  ({(reg)->csc = 0; while ((reg)->csc); (reg)->csc = (val) | FTM_CSC_CHF;})
#define FRAME_PIN_SET()              *(framePinReg + 4) = framePinMask
#define FRAME_PIN_CLEAR()            *(framePinReg + 8) = framePinMask
#endif

uint8_t FlightControllerTGSOutput::channelmask = 0;
FlightControllerTGSOutput * FlightControllerTGSOutput::list[8];

FlightControllerTGSOutput::FlightControllerTGSOutput(void)
{
	pulse_width[0] = TX_MINIMUM_FRAME_CLOCKS;
	for (int i=1; i <= FlightControllerTGS_MAXCHANNELS; i++) {
		pulse_width[i] = TX_DEFAULT_SIGNAL_CLOCKS;
	}
	cscSet = 0b01011100;
	cscClear = 0b01011000;
}

FlightControllerTGSOutput::FlightControllerTGSOutput(int polarity)
{
	pulse_width[0] = TX_MINIMUM_FRAME_CLOCKS;
	for (int i=1; i <= FlightControllerTGS_MAXCHANNELS; i++) {
		pulse_width[i] = TX_DEFAULT_SIGNAL_CLOCKS;
	}
	if (polarity == FALLING) {
		cscSet = 0b01011000;
		cscClear = 0b01011100;
	} else {
		cscSet = 0b01011100;
		cscClear = 0b01011000;
	}
}

bool FlightControllerTGSOutput::begin(uint8_t txPin)
{
	return begin(txPin, 255);
}

bool FlightControllerTGSOutput::begin(uint8_t txPin, uint8_t framePin)
{
	uint32_t channel;
	volatile void *reg;

	if (FTM0_MOD != 0xFFFF || (FTM0_SC & 0x7F) != FTM0_SC_VALUE) {
		FTM0_SC = 0;
		FTM0_CNT = 0;
		FTM0_MOD = 0xFFFF;
		FTM0_SC = FTM0_SC_VALUE;
		#if defined(KINETISK)
		FTM0_MODE = 0;
		#endif
	}
	switch (txPin) {
	  case  6: channel = 4; reg = &FTM0_C4SC; break;
	  case  9: channel = 2; reg = &FTM0_C2SC; break;
	  case 10: channel = 3; reg = &FTM0_C3SC; break;
	  case 20: channel = 5; reg = &FTM0_C5SC; break;
	  case 22: channel = 0; reg = &FTM0_C0SC; break;
	  case 23: channel = 1; reg = &FTM0_C1SC; break;
	  #if defined(KINETISK)
	  case  5: channel = 7; reg = &FTM0_C7SC; break;
	  case 21: channel = 6; reg = &FTM0_C6SC; break;
	  #endif
	  default:
		return false;
	}
	if (framePin < NUM_DIGITAL_PINS) {
		framePinReg = portOutputRegister(framePin);
		framePinMask = digitalPinToBitMask(framePin);
		pinMode(framePin, OUTPUT);
		FRAME_PIN_SET();
	} else {
		framePinReg = NULL;
	}
	state = 0;
	current_channel = 0;
	total_channels = 0;
	ftm = (struct ftm_channel_struct *)reg;
	ftm->cv = 200;
	CSC_CHANGE(ftm, cscSet); // set on compare match & interrupt
	list[channel] = this;
	channelmask |= (1<<channel);
	*portConfigRegister(txPin) = PORT_PCR_MUX(4) | PORT_PCR_DSE | PORT_PCR_SRE;
	NVIC_SET_PRIORITY(IRQ_FTM0, 32);
	NVIC_ENABLE_IRQ(IRQ_FTM0);
	return true;
}

bool FlightControllerTGSOutput::write(uint8_t channel, float microseconds)
{
	uint32_t i, sum, space, clocks, num_channels;

	if (channel < 1 || channel > FlightControllerTGS_MAXCHANNELS) return false;
	if (microseconds < TX_MINIMUM_SIGNAL || microseconds > TX_MAXIMUM_SIGNAL) return false;
	clocks = microseconds * CLOCKS_PER_MICROSECOND;
	num_channels = total_channels;
	if (channel > num_channels) num_channels = channel;
	sum = clocks;
	for (i=1; i < channel; i++) sum += pulse_width[i];
	for (i=channel+1; i <= num_channels; i++) sum += pulse_width[i];
	if (sum < TX_MINIMUM_FRAME_CLOCKS - TX_MINIMUM_SPACE_CLOCKS) {
		space = TX_MINIMUM_FRAME_CLOCKS - sum;
	} else {
		if (framePinReg) {
			space = TX_PULSE_WIDTH_CLOCKS;
		} else {
			space = TX_MINIMUM_SPACE_CLOCKS;
		}
	}
	__disable_irq();
	pulse_width[0] = space;
	pulse_width[channel] = clocks;
	total_channels = num_channels;
	__enable_irq();
	return true;
}

void FlightControllerTGSOutput::isr(void)
{
	#if defined(KINETISK)
	FTM0_MODE = 0;
	#endif
	if (state == 0) {
		// pin was just set high, schedule it to go low
		ftm->cv += TX_PULSE_WIDTH_CLOCKS;
		CSC_CHANGE_INTACK(ftm, cscClear); // clear on compare match & interrupt
		state = 1;
	} else {
		// pin just went low
		uint32_t width, channel;
		if (state == 1) {
			channel = current_channel;
			if (channel == 0) {
				total_channels_buffer = total_channels;
				for (uint32_t i=0; i <= total_channels_buffer; i++) {
					pulse_buffer[i] = pulse_width[i];
				}
			}
			width = pulse_buffer[channel] - TX_PULSE_WIDTH_CLOCKS;
			if (++channel > total_channels_buffer) {
				channel = 0;
			}
			if (framePinReg) {
				//if (channel == 0) {
				if (channel == 1) {
					FRAME_PIN_SET();
				} else {
					FRAME_PIN_CLEAR();
				}
			}
			current_channel = channel;
		} else {
			width = pulse_remaining;
		}
		if (width <= 60000) {
			ftm->cv += width;
			CSC_CHANGE_INTACK(ftm, cscSet); // set on compare match & interrupt
			state = 0;
		} else {
			ftm->cv += 58000;
			CSC_INTACK(ftm, cscClear); // clear on compare match & interrupt
			pulse_remaining = width - 58000;
			state = 2;
		}
	}
}

void ftm0_isr(void)
{
	if (FTM0_SC & 0x80) {
		#if defined(KINETISK)
		FTM0_SC = FTM0_SC_VALUE;
		#elif defined(KINETISL)
		FTM0_SC = FTM0_SC_VALUE | FTM_SC_TOF;
		#endif
		FlightControllerTGSInput::overflow_count++;
		FlightControllerTGSInput::overflow_inc = true;
	}
	// TODO: this could be efficient by reading FTM0_STATUS
	uint8_t maskin = FlightControllerTGSInput::channelmask;
	if ((maskin & 0x01) && (FTM0_C0SC & 0x80)) FlightControllerTGSInput::list[0]->isr();
	if ((maskin & 0x02) && (FTM0_C1SC & 0x80)) FlightControllerTGSInput::list[1]->isr();
	if ((maskin & 0x04) && (FTM0_C2SC & 0x80)) FlightControllerTGSInput::list[2]->isr();
	if ((maskin & 0x08) && (FTM0_C3SC & 0x80)) FlightControllerTGSInput::list[3]->isr();
	if ((maskin & 0x10) && (FTM0_C4SC & 0x80)) FlightControllerTGSInput::list[4]->isr();
	if ((maskin & 0x20) && (FTM0_C5SC & 0x80)) FlightControllerTGSInput::list[5]->isr();
	#if defined(KINETISK)
	if ((maskin & 0x40) && (FTM0_C6SC & 0x80)) FlightControllerTGSInput::list[6]->isr();
	if ((maskin & 0x80) && (FTM0_C7SC & 0x80)) FlightControllerTGSInput::list[7]->isr();
	#endif
	uint8_t maskout = FlightControllerTGSOutput::channelmask;
	if ((maskout & 0x01) && (FTM0_C0SC & 0x80)) FlightControllerTGSOutput::list[0]->isr();
	if ((maskout & 0x02) && (FTM0_C1SC & 0x80)) FlightControllerTGSOutput::list[1]->isr();
	if ((maskout & 0x04) && (FTM0_C2SC & 0x80)) FlightControllerTGSOutput::list[2]->isr();
	if ((maskout & 0x08) && (FTM0_C3SC & 0x80)) FlightControllerTGSOutput::list[3]->isr();
	if ((maskout & 0x10) && (FTM0_C4SC & 0x80)) FlightControllerTGSOutput::list[4]->isr();
	if ((maskout & 0x20) && (FTM0_C5SC & 0x80)) FlightControllerTGSOutput::list[5]->isr();
	#if defined(KINETISK)
	if ((maskout & 0x40) && (FTM0_C6SC & 0x80)) FlightControllerTGSOutput::list[6]->isr();
	if ((maskout & 0x80) && (FTM0_C7SC & 0x80)) FlightControllerTGSOutput::list[7]->isr();
	#endif
	FlightControllerTGSInput::overflow_inc = false;
}

// some explanation regarding this C to C++ trickery can be found here:
// http://forum.pjrc.com/threads/25278-Low-Power-with-Event-based-software-architecture-brainstorm?p=43496&viewfull=1#post43496

uint16_t FlightControllerTGSInput::overflow_count = 0;
bool FlightControllerTGSInput::overflow_inc = false;
uint8_t FlightControllerTGSInput::channelmask = 0;
FlightControllerTGSInput * FlightControllerTGSInput::list[8];

FlightControllerTGSInput::FlightControllerTGSInput(void)
{
	cscEdge = 0b01000100;
}

FlightControllerTGSInput::FlightControllerTGSInput(int polarity)
{
	cscEdge = (polarity == FALLING) ? 0b01001000 : 0b01000100;
}


bool FlightControllerTGSInput::begin(uint8_t pin)
{
	uint32_t channel;
	volatile void *reg;

	if (FTM0_MOD != 0xFFFF || (FTM0_SC & 0x7F) != FTM0_SC_VALUE) {
		FTM0_SC = 0;
		FTM0_CNT = 0;
		FTM0_MOD = 0xFFFF;
		FTM0_SC = FTM0_SC_VALUE;
		#if defined(KINETISK)
		FTM0_MODE = 0;
		#endif
	}
	switch (pin) {
	  case  6: channel = 4; reg = &FTM0_C4SC; break;
	  case  9: channel = 2; reg = &FTM0_C2SC; break;
	  case 10: channel = 3; reg = &FTM0_C3SC; break;
	  case 20: channel = 5; reg = &FTM0_C5SC; break;
	  case 22: channel = 0; reg = &FTM0_C0SC; break;
	  case 23: channel = 1; reg = &FTM0_C1SC; break;
	  #if defined(KINETISK)
	  case 21: channel = 6; reg = &FTM0_C6SC; break;
	  case  5: channel = 7; reg = &FTM0_C7SC; break;
	  #endif
	  default:
		return false;
	}
	prev = 0;
	write_index = 255;
	available_flag = false;
	ftm = (struct ftm_channel_struct *)reg;
	list[channel] = this;
	channelmask |= (1<<channel);
	*portConfigRegister(pin) = PORT_PCR_MUX(4);
	CSC_CHANGE(ftm, cscEdge); // input capture & interrupt on rising edge
	NVIC_SET_PRIORITY(IRQ_FTM0, 32);
	NVIC_ENABLE_IRQ(IRQ_FTM0);
	return true;
}

void FlightControllerTGSInput::isr(void)
{
	uint32_t val, count;

	val = ftm->cv;
	CSC_INTACK(ftm, cscEdge); // input capture & interrupt on rising edge
	count = overflow_count;
	if (val > 0xE000 && overflow_inc) count--;
	val |= (count << 16);
	count = val - prev;
	prev = val;
	 //Serial.print(val, HEX);
	 //Serial.print("  ");
	 //Serial.println(count);
	if (count >= RX_MINIMUM_SPACE_CLOCKS) {
		if (write_index < 255) {
			for (int i=0; i < write_index; i++) {
				pulse_buffer[i] = pulse_width[i];
			}
			total_channels = write_index;
			available_flag = true;
		}
		write_index = 0;
	} else {
		if (write_index < FlightControllerTGS_MAXCHANNELS) {
			pulse_width[write_index++] = count;
		}
	}
}

int FlightControllerTGSInput::available(void)
{
	uint32_t total;
	bool flag;

	__disable_irq();
	flag = available_flag;
	total = total_channels;
	__enable_irq();
	if (flag) return total;
	return -1;
}

float FlightControllerTGSInput::read(uint8_t channel)
{
	uint32_t total, index, value=0;

	if (channel == 0) return 0.0;
	index = channel - 1;
	__disable_irq();
	total = total_channels;
	if (index < total) value = pulse_buffer[index];
	if (channel >= total) available_flag = false;
	__enable_irq();
	return (float)value / (float)CLOCKS_PER_MICROSECOND;
}


#endif
