SID Bus → SIDKick Pico

Pico 2 GPIO	Signal	Notes
GP0–GP7	D0–D7	Data bus
GP8–GP12	A0–A4	Address bus (5-bit)
GP13	/CS	Chip select (active low)
GP15	/RES	Reset (active low)
GP16	phi2	PWM clock ~985 kHz
GND	R/W	Tied to GND (write-only)
SD Card → SPI0

Pico 2 GPIO	Signal
GP17	/CS
GP18	SCK
GP19	MOSI
GP20	MISO
TFT Display (ST7735 160×128) → SPI1

Pico 2 GPIO	Signal
GP26	SCK
GP27	SDA (MOSI)
GP28	DC
3.3V	RES (wired direct, no GPIO)
3.3V	BL backlight (wired direct, no GPIO)
Button

Pico 2 GPIO	Signal
GP22	Next-track button (active low, internal pull-up)
