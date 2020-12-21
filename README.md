# ESP32 K-bus Interface

[![MPL 2.0 License][license-shield]][license-url]
[![LinkedIn][linkedin-shield]][linkedin-url]

## Table of Contents

* [About the Project](#about)
  * [Hardware](#hardware)
  * [Software](#software)
* [Getting Started](#getting-started)
  * [Prerequisites](#prerequisites)
  * [Building](#building)
  * [Installing](#installing)
* [Roadmap](#roadmap)
* [License](#license)

## About

K-bus/I-bus to Bluetooth bridge for Mini/BMWs. Based on ESP32.

### Hardware

* [Sparkfun MicroMod ESP32](https://www.sparkfun.com/products/16781)
* [Sparkfun MicroMod ATP Board](https://www.sparkfun.com/products/16885)
* ~~[On Semi NCV7805BTG 5V Regulator](https://www.digikey.com/en/products/detail/on-semiconductor/921437)~~
* [USB Power Supply](https://www.amazon.com/gp/product/B07KWRH61D)

#### Candidate K-bus ICs

* [ON Semi NCV7428D13R2G](https://www.digikey.com/en/products/detail/on-semiconductor/5022588) ❓
* [NXP TJA1021T](https://www.digikey.com/en/products/detail/nxp-usa-inc/2034448) ❓
* [TI SN65HVDA195QDRQ1](https://www.digikey.com/en/products/detail/texas-instruments/2094636) ✔
* [Microchip MCP2025-330E-SN](https://www.digikey.com/en/products/detail/microchip-technology/3543134) ❓

### Software

* [esp-idf](https://github.com/espressif/esp-idf)
* [btstack](https://github.com/bluekitchen/btstack)

<!-- GETTING STARTED -->
## Getting Started

Clone, build, and flash.

### Prerequisites

Install `esp-idf` and the ESP32 `btstack` port by following the respective instructions.

### Building

#### Hardware

**TBD**

#### Software

* Clone this repo and its submodules `git clone --recursive https://github.com/jmederos/esp32-r50-kbus.git`
* After installing prequisites, run the helper script `./tools/build.sh` to compile
* `./tools/flash_monitor.sh` to load onto ESP32 and run `idf.py monitor`
_Note: Helper scripts in tools folder assume a WSL Ubuntu install w/ESP32 on Windows COM4_

### Installing

* Use OEM CD changer pre-wiring in R50 behind right side panel in trunk.

#### Reference
* [How to find pre-wiring](https://www.northamericanmotoring.com/forums/navigation-and-audio/224408-can-t-find-cd-changer-pre-wiring.html)
* [CD changer installation instructions](https://new.minimania.com/images/instructions/OEM%20CD%20Changer.pdf)

## Roadmap
| Feature | Status | Notes |
| --- | --- | --- |
| Software POC: control phone via AVRCP w/o A2DP advertised. | 👌 | _Although part of the BT spec, example implementations on AVRCP-only are hard to come by._ |
| Hardware POC: acquire + test k-line transceivers. | 🙌 |   |
| K-bus proof concept, pickup MFL messages. | 👍 | _Have yet to test in vehicle, works in mockup w/Navcoder + iPhone streaming to a BT headphone amp._ |
| [AMS](https://developer.apple.com/library/archive/documentation/CoreBluetooth/Reference/AppleMediaService_Reference/Specification/Specification.html) instead of AVRCP |   | _Didn't know this was a thing, seems like a better bet; btstack + esp-idf is a little clunky._ |
| Write to radio head unit display |   |   |
| [ANCS](https://developer.apple.com/library/archive/documentation/CoreBluetooth/Reference/AppleNotificationCenterServiceSpecification/Introduction/Introduction.html) Support & update radio display w/push notification | 🤞 |   |
| ~~i2s DAC for fully integrated solution?...~~|  | _nah... AMS + existing FiiO DAC is better._ |

_See the [open issues](https://github.com/jmederos/esp32-r50-kbus/issues) for a list of proposed features (and known issues)._

## License

Distributed under the MPL 2.0 License. See `LICENSE` for more information.

<!-- MARKDOWN LINKS & IMAGES -->
<!-- https://www.markdownguide.org/basic-syntax/#reference-style-links -->
[license-shield]: https://img.shields.io/badge/license-MPL%202.0-blue
[license-url]: https://github.com/jmederos/esp32-r50-kbus/blob/master/LICENSE
[linkedin-shield]: https://img.shields.io/badge/-LinkedIn-black.svg?style=flat-square&logo=linkedin&colorB=555
[linkedin-url]: https://linkedin.com/in/jacobmederos
