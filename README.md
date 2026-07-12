# Fuel Gauge Configuration (bq34z100-PWR-R2)

> Hello, Nerds! 👋

The repository serves as a practical reference for initializing, configuring, and interacting with the fuel gauge over I²C. It is not intended to be a complete library, but rather a template and learning resource that demonstrates the configuration flow and can be adapted for other projects.

Whether you're bringing up a new board or trying to understand how the gauge works, I hope this saves you some debugging time.


## Specifications
- **Battery Pack**
	- [ ] Li-ion battery
	- [ ] 2S2P Battery
	- [ ] 10000 mAh
	- [ ] 8.4 V
- **Host**
	- [ ] **Board:** NXP i.MX8MP EVK
	- [ ] **Architecture:** AArch64
	- [ ] **Operating System:** Embedded Linux (Yocto)

## Who is this for?

If you're working with the **bq34z100-PWR-R2** and don't want to spend hours digging through the TRM and datasheet, this repository should give you a solid starting point.

Feel free to modify it according to your battery profile and hardware design.

## Uses
```bash
git clone https://github.com/xploitoverload/fuel_gauge.git
cd fuel_gauge
make
./gauge_test # use --configure if you want
```

## Acknowledgements
> Special thanks to the people who guided and supported this work
- **Arunkumar Pal**  (Lead Hardware Engineer)
- **Bhavin Darji** (Hardware Master Engineer)
- **Jay Sapra** (Senior Hardware Engineer)

And finally...
- **Kalpesh Solanki** — _The Newbie who refused to give up.
