# Third-party notices — HondaDash M1

HondaDash uses the unmodified **Qt 6.8.3** QtCore, QtGui and QtWidgets
shared libraries and their platform/image/style plugins. Qt copyright
belongs to The Qt Company Ltd. and other contributors; the authoritative
copyright and component licence declarations are in Qt's source files.

The open-source Qt libraries used here are available under the GNU Lesser
General Public License version 3, with the GNU GPL version 3 incorporated
by reference, or under Qt's other offered licensing options. This package
uses shared libraries; users may replace them with compatible modified
Qt libraries. No restriction on debugging modifications to these
libraries is imposed by this notice.

Complete original licence and attribution texts copied from the fixed
`qtbase` tag `v6.8.3` are included in `licenses/qtbase-6.8.3/` in the
Windows package (`docs/licenses/qtbase-6.8.3/` in the repository).
The directory includes the original `LICENSES/` texts, `COPYING`/`LICENSE`
documents and `qt_attribution.json` files from bundled third-party source
directories. It includes attribution for components such as FreeType,
HarfBuzz, libpng, zlib, PCRE2 and double-conversion where used by Qt.
Some bundled notices cover optional Qt code absent from this application.

Original source and licensing references:

- [Qt 6.8.3 source archive](https://download.qt.io/archive/qt/6.8/6.8.3/submodules/)
- [Exact QtBase source tag](https://github.com/qt/qtbase/tree/v6.8.3)
- [Qt licensing documentation](https://doc.qt.io/qt-6.8/licensing.html)
- [Qt Windows deployment documentation](https://doc.qt.io/qt-6.8/windows-deployment.html)

The source links identify the unmodified library version and allow
obtaining its corresponding source. The LGPL and GPL full texts in the
bundle are included to accompany Qt; they do not select a licence for
HondaDash's own source code.

Microsoft Visual C++ runtime libraries and/or the redistributable
installer, when deployed by `windeployqt`, are Microsoft components under
Microsoft's redistribution terms, not Qt LGPL code. The generated package
README explicitly states whether an external Visual C++ Redistributable
2015–2022 x64 installation is required. Windows Universal CRT is a system
component on supported Windows 10/11. Compiler imports are recorded in
the accompanying dependency inspection report.

HondaDash contains no OEM ROM, Honda proprietary assets, Hondash code,
third-party UI artwork or additional application runtime framework.
The repository owner has not chosen a licence for the project itself.

## Nano firmware components (M1)

The separately built Nano firmware statically links the unmodified Arduino AVR
core **1.8.6**, including HardwareSerial, Print's binary write interface, the
millis timer, and startup code. These files are distributed under
**LGPL-2.1-or-later** with copyright notices from Nicholas Zambetti, David A.
Mellis, the Arduino project and other original contributors. Original file
header notices and the complete LGPL-2.1 text are in `licenses/firmware/`.
No text print routine is used on the binary protocol channel.

The fixed Arduino compiler package `7.3.0-atmel3.6.1-arduino7` provides
**avr-libc 2.0.0** (BSD-style licence; its complete original LICENSE is included)
and **GCC 7.3.0 libgcc** (GPLv3 with the GCC Runtime Library Exception 3.1;
both complete original texts are included). These are embedded firmware
components, not additional Windows runtime dependencies.

Corresponding unmodified source and original licensing references:

- [Arduino AVR core 1.8.6](https://github.com/arduino/ArduinoCore-avr/tree/1.8.6)
- [Arduino AVR toolchain build/source recipes](https://github.com/arduino/toolchain-avr)
- [avr-libc 2.0.0 source](https://github.com/avrdudes/avr-libc/tree/avr-libc-2_0_0-release)
- [GCC 7.3.0 source and runtime exception](https://github.com/gcc-mirror/gcc/tree/releases/gcc-7.3.0)

The build scripts retain the firmware sketch object files, Arduino core archive,
ELF, linker map and full memory reports in each firmware build directory. Retain
those files and the firmware notice bundle with distributed firmware to support
inspection and relinking against modified compatible Arduino core sources.
The repository's firmware source and pinned build scripts describe the complete
sketch build; the Arduino core sources installed by the CLI remain unmodified.
The generated `with_bootloader.hex`, if present in a raw Arduino build tree, is
not the upload artifact recommended here: use `nano_synthetic.ino.hex` only.
No bootloader binary, fuse change or EEPROM write is part of HondaDash's build
or upload instructions. These third-party texts do not choose a licence for
HondaDash's own source.

## Qt SerialPort 6.8.3 (M1 full desktop build)

The full build dynamically links the unmodified QtSerialPort 6.8.3 module.
Its original license texts are included in `licenses/qtserialport-6.8.3/`
(`docs/licenses/qtserialport-6.8.3/` in the repository). The source copyright
notices include Denis Shienkov, Sergey Belyashov, Laszlo Papp, Andre Hartmann,
The Qt Company and other contributors, as recorded in the source files.
The module's LGPL-3.0-only option is used alongside QtBase's shared-library
terms above; its files also offer commercial/GPL alternatives.
Corresponding unmodified source: [Qt SerialPort v6.8.3](https://github.com/qt/qtserialport/tree/v6.8.3).
Simulation-only builds omit this module entirely.
