# Witty-Pi-5-Software
This repository is for the software (wp5) for [Witty Pi 5 HAT+](https://www.uugear.com/product/witty-pi-5/).

Witty Pi 5 HAT+ is an add-on board for Raspberry Pi that brings real-time clock (RTC) and power management functionalities to your Raspberry Pi. It can define the on/off sequence of your Raspberry Pi, perform power on/off actions based on temperature or voltage thresholds, and significantly reduce overall energy consumption. As a Mode 1 Power HAT+ board, Witty Pi 5 can deliver up to 5A current to Raspberry Pi and its peripherals.

### Build the software
To build the software, just run:
```
git clone https://github.com/uugear/Witty-Pi-5-Software.git
cd Witty-Pi-5-Software
make
```

### Run Unit Tests
Run the host-side unit tests with:
```
make test
```
These tests verify DST rule encoding, decoding,
timezone matching, and daylight saving transitions.
They do not require Witty Pi 5 hardware.
