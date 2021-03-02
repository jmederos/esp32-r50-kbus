# Hardware

## Microcontrollers
* [Sparkfun MicroMod ESP32](https://www.sparkfun.com/products/16781)
* [Sparkfun MicroMod ATP Board](https://www.sparkfun.com/products/16885)

## Power Regulation
| Part | Notes |
| --- | --- |
| ~~[On Semi NCV7805BTG 5V Regulator](https://www.digikey.com/en/products/detail/on-semiconductor/921437)~~ | Too inefficient
| [USB Power Supply](https://www.amazon.com/gp/product/B07KWRH61D) | Worked well for prototyping w/MicroMod ESP32
| [OKI-78SR-5](https://www.digikey.com/en/products/detail/OKI-78SR-5-1-5-W36H-C/3438675) | For v1 of feather board

## Candidate K-bus ICs
| Part | Status | Notes |
| --- | --- | --- |
| [ON Semi NCV7428D13R2G](https://www.digikey.com/en/products/detail/on-semiconductor/5022588) | ❓ | Have, but not yet tested
| [NXP TJA1021T](https://www.digikey.com/en/products/detail/nxp-usa-inc/2034448) | ✔ | Using with a `CP2102` USB ⇄ Serial to interface with NavCoder and simulate network on desktop
| [TI SN65HVDA195QDRQ1](https://www.digikey.com/en/products/detail/texas-instruments/2094636) | ✔ | Used on initial perfboard prototypes. Works ok, but susceptible to glitching the bus; doesn't have tx timeout or contention detection.
| [Microchip MCP2025-330E-SN](https://www.digikey.com/en/products/detail/microchip-technology/3543134) | ❓ | Have, but not yet tested
| [Microchip MCP2004(A)](https://www.digikey.com/en/products/detail/MCP2004AT-E-SN/2803651) | ✔ | Selected for tx timeout and bus contention detection + added bonus of `FAULT` pin. So far, only tested `MCP2004A`; works in simulated bus.

<!--stackedit_data:
eyJoaXN0b3J5IjpbMjA3ODQxNjE2Nl19
-->