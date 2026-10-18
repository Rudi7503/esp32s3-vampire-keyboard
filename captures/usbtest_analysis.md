# Auswertung `usbtest.pcapng`

Capture: 1716 USBPcap-Pakete, Bus 2.

| Gerät | VID:PID | Rolle |
|---|---|---|
| Bus 2, Device 1 | `046d:c31c` | Logitech K120, die funktionierende Tastatur |
| Bus 2, Device 2 | `046d:c534` | zweites HID-Gerät mit 4-Byte-Interrupt-Reports, für die Tastatur nicht relevant |

Die Tastatur hat **zwei HID-Schnittstellen**:

1. Interface 0: Boot-Keyboard, Report 65 Byte, Endpoint `0x81`, 8 Byte, 10 ms
2. Interface 1: Consumer Control, Report 184 Byte, Endpoint `0x82`, 4 Byte, 255 ms

Es gibt **keinen IAD**, `bDeviceClass=0x00`.

## Device Descriptor (18 Byte)

```text
12 01 10 01 00 00 00 08 6d 04 1c c3 02 64 01 02 00 01
```

| Feld | Wert |
|---|---|
| `bcdUSB` | `0x0110` |
| `bDeviceClass/SubClass/Protocol` | `0x00/0x00/0x00` |
| `bMaxPacketSize0` | `8` |
| `idVendor` | `0x046d` (Logitech) |
| `idProduct` | `0xc31c` |
| `bcdDevice` | `0x6402` |
| `iManufacturer` | `1` |
| `iProduct` | `2` |
| `iSerialNumber` | `0` |
| `bNumConfigurations` | `1` |

Der Herstellerstring wurde im Capture nicht abgefragt. Der Produktstring
ist `USB Keyboard`.

## Configuration Descriptor (59 Byte, vollständig)

```text
09 02 3b 00 02 01 03 a0 2d
09 04 00 00 01 03 01 01 02
09 21 10 01 00 01 22 41 00
07 05 81 03 08 00 0a
09 04 01 00 01 03 00 00 02
09 21 10 01 00 01 22 b8 00
07 05 82 03 04 00 ff
```

| Feld | Wert |
|---|---|
| `wTotalLength` | 59 |
| `bNumInterfaces` | 2 |
| `bConfigurationValue` | 1 |
| `iConfiguration` | 3 |
| `bmAttributes` | `0xA0` (bus-powered, remote wakeup) |
| `bMaxPower` | `0x2D` = 90 mA |
| Interface 0 | HID, Boot, Keyboard, 1 EP, `iInterface=2` |
| HID 0 | `bcdHID=0x0110`, Report 65 Byte |
| Endpoint 0x81 | Interrupt IN, 8 Byte, `bInterval=10` |
| Interface 1 | HID, SubClass 0, Protocol 0, 1 EP, `iInterface=2` |
| HID 1 | `bcdHID=0x0110`, Report 184 Byte |
| Endpoint 0x82 | Interrupt IN, 4 Byte, `bInterval=255` |

## Keyboard HID Report Descriptor (65 Byte)

```text
05 01 09 06 a1 01 05 07 19 e0 29 e7 15 00 25 01
75 01 95 08 81 02 95 01 75 08 81 01 95 05 75 01
05 08 19 01 29 05 91 02 95 01 75 03 91 01 95 06
75 08 15 00 26 ff 00 05 07 19 00 2a ff 00 81 00
c0
```

Das ist die 65-Byte-Variante der Boot-Tastatur: gleiches 8-Byte-Report-
Layout wie die kanonische 63-Byte-Version, aber mit Logisch-/Usage-Maximum
`0x00FF` statt `0x0065`.

## Consumer-Control-Report Descriptor (184 Byte)

Nur zur Vollständigkeit; für eine reine Boot-Tastatur nicht nötig:

```text
05 0c 09 01 a1 01 85 01 09 e0 15 e8 25 18 75 07 95 01 81 06 15 00 25 01
75 01 09 e2 81 06 c0 06 01 00 09 80 a1 01 85 02 25 01 15 00 75 01 0a 81
00 0a 82 00 0a 83 00 95 03 81 06 95 05 81 01 c0 06 0c 00 09 01 a1 01 85
03 25 01 15 00 75 01 0a b5 00 0a b6 00 0a b7 00 0a b8 00 0a cd 00 0a e2
00 0a e9 00 0a ea 00 95 08 81 02 0a 83 01 0a 8a 01 0a 92 01 0a 94 01 0a
21 02 0a 23 02 0a 24 02 0a 25 02 95 08 81 02 0a 26 02 0a 27 02 0a 2a 02
0a b3 00 0a b4 00 95 05 81 02 95 03 81 01 c0 06 00 ff 09 01 a1 01 85 05
15 00 26 ff 00 19 01 29 02 75 08 95 05 b1 02 c0
```

## Strings

| Index | Inhalt | Capture |
|---|---|---|
| 0 | LangID `0x0409` | `04 03 09 04` |
| 1 | Hersteller | nicht abgefragt |
| 2 | `USB Keyboard` | `1a 03 55 00 53 00 42 00 20 00 4b 00 65 00 79 00 62 00 6f 00 61 00 72 00 64 00` |
| 3 | Konfiguration | nicht abgefragt |

## Enumerationsablauf im Capture

1. `GET_DESCRIPTOR(DEVICE)` 18 Byte
2. `GET_DESCRIPTOR(CONFIG)` 59 Byte
3. `SET_CONFIGURATION(1)`
4. Später erneute Enumeration mit:
   - `GET_DESCRIPTOR(DEVICE)` und `CONFIG`
   - `SET_CONFIGURATION(1)`
   - String 2 (`USB Keyboard`)
   - `SET_IDLE` für Interface 0
   - `GET_DESCRIPTOR(HID Report)` Interface 0, 65 Byte
   - `SET_IDLE` für Interface 1
   - `GET_DESCRIPTOR(HID Report)` Interface 1, 184 Byte
   - `SET_REPORT` (Output, LED-Daten `01`)
   - periodische Interrupt-INs auf `0x81` (8 Byte) und `0x82` (4 Byte)

Die ersten sechs Pakete sind eine minimale Boot-Enumeration **ohne**
HID-Report-Request. Das ist genau das Muster, das ein einfacher
Boot-Host wie der V4 fahren kann: Device-Descriptor, Config-Descriptor,
`SET_CONFIGURATION`, danach direkt Boot-Reports. Die Tastatur funktioniert
dort, weil sie bereits im Boot-Protocol ist und 8-Byte-Reports liefert.

Ein `SET_PROTOCOL`-Request ist im Capture **nicht** enthalten. Die
Tastatur meldet `bInterfaceProtocol=1` (Boot) und wird deshalb direkt im
Boot-Protocol benutzt.

## Interrupt-Reports der Tastatur

Alle Tastatur-Reports auf `0x81` sind 8 Byte:

```text
00 00 4f 00 00 00 00 00
00 00 00 00 00 00 00 00
00 00 50 00 00 00 00 00
00 00 00 00 00 00 00 00
```

Das bestätigt: Byte 0 Modifier, Byte 1 reserviert, Byte 2..7 Keycodes,
keine Report-ID.

## Konsequenz für den ESP32-S3

Das Projekt verwendet die **Single-Interface-Teilmenge**:

- Device: `bcdUSB=0x0110`, `bMaxPacketSize0=8` (dazu
  `CFG_TUD_ENDPOINT0_SIZE=8`), `bcdDevice=0x6402`, `iSerialNumber=0`
- Config: 34 Byte, 1 Interface, `bmAttributes=0xA0`, `bMaxPower=0x2D`
- HID: `bcdHID=0x0110`, Report 65 Byte
- Endpoint `0x81`, 8 Byte, 10 ms
- Report: 65 Byte aus diesem Capture

Die zweite (Consumer-Control-)Schnittstelle ist weggelassen. Für eine
Boot-Tastatur ist das ausreichend und reduziert die Angriffsfläche für
einen wählerischen Host. Wenn der V4 die vollständige 59-Byte-Konfiguration
erwartet, muss das Projekt auf **zwei HID-Instanzen** erweitert werden:

```text
CONFIG_TINYUSB_HID_COUNT=2
```

und `tud_hid_descriptor_report_cb(instance)` muss für `instance=0` den
65-Byte-Report, für `instance=1` den 184-Byte-Report zurückgeben. Die
59-Byte-Konfiguration oben ist dann unverändert einsetzbar; `wTotalLength`
und `wDescriptorLength` bleiben 59/65/184.
