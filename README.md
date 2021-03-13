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

See [hardware](hardware/README) folder

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

#### R50 Support Docs
* [How to find pre-wiring](https://www.northamericanmotoring.com/forums/navigation-and-audio/224408-can-t-find-cd-changer-pre-wiring.html)
* [CD changer installation instructions](https://new.minimania.com/images/instructions/OEM%20CD%20Changer.pdf)

## Roadmap
| Feature | Status | Notes |
| --- | --- | --- |
| Software POC: control phone via AVRCP w/o A2DP advertised. | 👌🏽 | _Although part of the BT spec, example implementations on AVRCP-only are hard to come by._ |
| Hardware POC: acquire + test k-line transceivers. | 🙌🏽 |   |
| K-bus proof concept, pickup MFL messages. | 👍🏽 | _Works in vehicle, works in mockup w/Navcoder + iPhone streaming to a BT headphone amp._ |
| Write to radio head unit display | 🥳 | _Seems to work, now let's do scrolling display_ |
| SDRS Emulation | 👍🏽 | _Functional, didn't really like the fact that FiiO BT amp didn't shutdown with car; will stick to aux input + `TEL` display updates for now._ |
| Write to radio head unit display | 🥳 | _Seems to work, now let's do scrolling display_ |
| [AMS](https://developer.apple.com/library/archive/documentation/CoreBluetooth/Reference/AppleMediaService_Reference/Specification/Specification.html) instead of AVRCP |   | _Didn't know this was a thing, seems like a better bet; btstack + esp-idf is a little clunky._ |
| [ANCS](https://developer.apple.com/library/archive/documentation/CoreBluetooth/Reference/AppleNotificationCenterServiceSpecification/Introduction/Introduction.html) Support & update radio display w/push notification | 🤞 |   |

_See the [open issues](https://github.com/jmederos/esp32-r50-kbus/issues) for a list of proposed features (and known issues)._

## References
* [iPod ↔ Sirius IBus adapter](https://github.com/blalor/iPod_IBus_adapter) - _Referenced for SDRS packet logs (my car isn't equiped with one.)_
* [Hack The IBus](http://web.archive.org/web/20110320205413/http://ibus.stuge.se/Main_Page)

## License

Distributed under the MPL 2.0 License. See `LICENSE` for more information.

<!-- MARKDOWN LINKS & IMAGES -->
<!-- https://www.markdownguide.org/basic-syntax/#reference-style-links -->
[license-shield]: https://img.shields.io/badge/license-MPL%202.0-blue
[license-url]: https://github.com/jmederos/esp32-r50-kbus/blob/master/LICENSE
[linkedin-shield]: https://img.shields.io/badge/-LinkedIn-black.svg?style=flat-square&logo=linkedin&colorB=555
[linkedin-url]: https://linkedin.com/in/jacobmederos
