#include <Arduino.h>
#include <TimerOne.h>
#include <PLCutils.h>



/*
* Classed used to send a trigger to the camera without blocking the event loop.
*/
class NikonCameraTrigger {
	private:
		uint8_t _outPin{9};
		uint16_t _dutyCycle{614};  // %60 percent duty cycle
		float _period{23.8};       // 42 khz
		TON pulseTimer{micros};

		/*
		* sends the high pulse
		*/
		void _pulseHigh(uint32_t duration)
		{
			this->pulseTimer.reset();
			this->pulseTimer.PRE = duration;
			Timer1.pwm(this->_outPin, this->_dutyCycle);
			while(this->pulseTimer.update(true).TT) {}
		}

		/*
		* sends the low pulse
		*/
		void _pulseLow(uint32_t duration)
		{
			this->pulseTimer.reset();
			this->pulseTimer.PRE = duration;
			digitalWrite(this->_outPin, LOW);
			while(this->pulseTimer.update(true).TT);
		}

	public:

		void initialize() {
			pinMode(this->_outPin, OUTPUT);
			Timer1.initialize(23.8);  // must be initialized in the main loop
		}

		/*
		* function used to sent the PWM pin at a specific frequency and duty cycle
		*/
		void trigger() {
			// send the sequence the first time
			this->_pulseHigh(2000);
			this->_pulseLow(27800);
			this->_pulseHigh(500);
			this->_pulseLow(1500);
			this->_pulseHigh(500);
			this->_pulseLow(3500);
			this->_pulseHigh(500);
			// pause for a 63 millis
			this->_pulseLow(63000);
			// send the sequence again
			this->_pulseHigh(2000);
			this->_pulseLow(27800);
			this->_pulseHigh(500);
			this->_pulseLow(1500);
			this->_pulseHigh(500);
			this->_pulseLow(3500);
			this->_pulseHigh(500);
			// turn off the pwm pin
			this->_pulseLow(1);
		}
};



// camera triggering variables
NikonCameraTrigger nikonCameraTrigger{};
constexpr uint16_t timingIncrement{500};
constexpr uint16_t timingLowLimit{1000};
constexpr uint16_t timingHighLimit{10000};
TON triggerTimer{millis};

// button input & filtering variables
uint8_t buttonPin{8};
bool buttonInputRaw{false};
bool buttonInputDebounced{false};
TON highDebounceTimer{millis};
TOF lowDebounceTimer{millis};;

// state machine variables
ONS modeChangeONS{};
TON modeChangeTimer{millis};
constexpr uint8_t ENUM_LENGTH = 2;
enum Mode {SINGLE, REPEATING};
Mode currentMode{SINGLE};

// auto off for battery conservation variables
uint8_t relayPin{7};
ONS idleONS{};
TON idleTimer{millis};
TON powerOffTimer{millis};


void setup() {
	// setup the camera
	pinMode(buttonPin, INPUT);
	nikonCameraTrigger.initialize();  // must be initialized in setup
	modeChangeTimer.PRE = timingLowLimit;
	highDebounceTimer.PRE = 10;
	lowDebounceTimer.PRE = 50;

	// setup the auto off variables
	pinMode(relayPin, OUTPUT);
	digitalWrite(relayPin, HIGH);
	powerOffTimer.PRE = 6000; // 6 second hold to manually turn off
	idleTimer.PRE = 300000;   // 5 minute auto off

	// init the serial port
	Serial.begin(9600);
	Serial.println("Starting...");
}


void loop() {
	// poll and debounce the input
	buttonInputRaw = !digitalRead(buttonPin);
	buttonInputDebounced = highDebounceTimer.update(buttonInputRaw).DN
													|| (buttonInputDebounced && lowDebounceTimer.update(buttonInputRaw).DN);

	// poll the idle timer and turn off
	if(idleONS.update(idleTimer.update(true).DN) || (powerOffTimer.update(buttonInputDebounced).DN)){
		Serial.println("Turning off...");
		digitalWrite(relayPin, LOW);
	}

	// run the state machine
	switch (currentMode)
	{
		// this mode takes a single photo every time the button is pressed
		case SINGLE:
			// take a photo on the falling edge
			if(!buttonInputDebounced && modeChangeTimer.TT) {
				Serial.println("Camera triggered");
				nikonCameraTrigger.trigger();
				idleTimer.reset();
			}
			break;
		// this mode changes the interval every time the button is pressed
		case REPEATING:
			// increment the timer on the falling edge
			if(!buttonInputDebounced && modeChangeTimer.TT) {
				// increment timing from 2 -> 10 seconds
				triggerTimer.PRE = constrain((	(triggerTimer.PRE + timingIncrement) % (timingHighLimit+timingIncrement)	),
																			timingLowLimit, timingHighLimit);
				triggerTimer.reset();
				Serial.print("Duration = "); Serial.println(triggerTimer.PRE);
			}
			// take photos on repeat but only when button not being pressed
			if(!buttonInputDebounced && triggerTimer.update(!triggerTimer.DN).DN) {
				Serial.println("Camera triggered");
				nikonCameraTrigger.trigger();
				idleTimer.reset();
			}
			break;
	}

	// if it's been held for two seconds, change state
	if(modeChangeONS.update(modeChangeTimer.update(buttonInputDebounced).DN)){
		// do some setup for repeating
		currentMode = static_cast<Mode>( (static_cast<uint8_t>(currentMode) + 1) % ENUM_LENGTH );
		if(currentMode == REPEATING) {
			triggerTimer.PRE = timingLowLimit;
			triggerTimer.reset();
			Serial.println("Mode: Repeating");
		} else {
			Serial.println("Mode: Single");
		}
	}
}