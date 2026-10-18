# ESP32-S3 als USB-Boot-Keyboard am Apollo Vampire V4

Vollständiges, mit **ESP-IDF 6.1** und **esp_tinyusb 2.4.0** gebautes Beispiel.
Der ESP32-S3 läuft als Full-Speed-USB-Device am nativen USB-OTG-Port
(GPIO19 = D-, GPIO20 = D+) und meldet sich als einklassige
Boot-Protocol-Tastatur an.

> **Am Vampire V4 verifiziert:** Die Firmware enumeriert am V4 und tippt
> im Testmodus alle 3 Sekunden `Hallo` + Return. Verwendet werden dabei
> `bcdUSB=0x0110`, `bMaxPacketSize0=8` (EP0=8), die 34-Byte-Config und
> der 65-Byte-Keyboard-Report aus `usbtest.pcapng`.

## 0. Was in diesem Projekt steckt

| Datei | Inhalt |
|---|---|
| `main/usb_descriptors.c` | Device-, Configuration-, Interface-, HID-, Endpoint- und String-Deskriptoren; HID-Report-Deskriptor; Prüfsummen-/Längencheck |
| `main/usb_descriptors.h` | VID/PID, Längenmakros, Exporte |
| `main/keyboard.c` | TinyUSB-Installation, 8-Byte-Report senden, Taste drücken/loslassen, LED-Callback |
| `main/keyboard.h` | öffentliche API |
| `main/main.c` | Minimalbeispiel: warten, bis der Host konfiguriert hat |
| `tools/usbdesc.py` | Hex aus tshark in C-Array umwandeln und Deskriptoren prüfen |
| `captures/usbtest.pcapng` | Referenz-Capture der funktionierenden Logitech-Tastatur |
| `captures/usbtest_analysis.md` | vollständige Auswertung inkl. 59-Byte-Config und 184-Byte-Consumer-Report |
| `sdkconfig.defaults` | N16R8, HID=1, UART-Konsole statt USB-Serial-JTAG |

Getestete Versionen:

* ESP-IDF `v6.1`
* `espressif/esp_tinyusb` `2.4.0`
* darin enthalten: `espressif/tinyusb` `0.21.0~2`

Der Code ist absichtlich auf diese API-Version gepinnt. Bei älteren
esp_tinyusb-Versionen (1.x) heißt die Konfigurationsstruktur anders
(flache Felder `device_descriptor`, `configuration_descriptor`, ...).

## 1. Bauen und flashen

```bash
. $HOME/esp-idf/export.sh
cd $HOME/esp32s3_vampire_keyboard

# sdkconfig.defaults wird beim ersten set-target übernommen
idf.py set-target esp32s3
idf.py build

# UART0: GPIO43 (TX), GPIO44 (RX), 115200 Baud
idf.py -p /dev/ttyUSB0 flash monitor
```

Wenn im Projekt bereits eine `sdkconfig` liegt, werden `sdkconfig.defaults`
nicht automatisch angewendet. Dann prüfen:

```bash
grep -E 'CONFIG_TINYUSB_HID_COUNT|CONFIG_ESP_CONSOLE_UART_DEFAULT|CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG' sdkconfig
# CONFIG_TINYUSB_HID_COUNT=1
# CONFIG_ESP_CONSOLE_UART_DEFAULT=y
# CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG is not set
```

Falls die Werte abweichen: `idf.py menuconfig` (TinyUSB → HID interfaces
count = 1, Console → UART0) oder `sdkconfig` löschen und
`idf.py set-target esp32s3` erneut ausführen.

Wichtig beim ESP32-S3:

* USB-OTG und USB-Serial-JTAG teilen sich **GPIO19/20**. Sobald TinyUSB
  läuft, ist die USB-Serial-JTAG-Konsole weg. Deshalb ist die Konsole hier
  auf UART0 gelegt (`CONFIG_ESP_CONSOLE_UART_DEFAULT=y`).
* Zum Flashen über USB-Serial-JTAG: BOOT gedrückt halten, RESET drücken,
  BOOT loslassen, dann `idf.py -p /dev/ttyACM0 flash`. Nach dem Reset
  erscheint das Gerät als Tastatur, nicht mehr als serieller Port.
* Der ESP32-S3 kann **nur Full-Speed (12 Mbit/s)**, nicht Low-Speed
  (1,5 Mbit/s). Der Vampire-Port muss Full-Speed-Geräte akzeptieren. Zum
  Gegenprüfen eine bekannte Full-Speed-Tastatur verwenden; wenn der V4
  ausschließlich Low-Speed kann, ist keine S3-Lösung möglich.
* Wenn das Board selbst versorgt wird, nur GND, D+ und D- mit dem V4
  verbinden; VBUS nicht zusätzlich einspeisen. Bei Busversorgung muss der
  V4-Port genug Strom liefern (Descriptor: 90 mA, siehe `bMaxPower`).
  Ein N16R8-Board mit aktivem PSRAM kann mehr ziehen; dann entweder
  `bMaxPower` auf den tatsächlichen Wert erhöhen (z. B. `0xFA` = 500 mA)
  oder das Board selbst versorgen.

## 2. Der Code: Deskriptoren und Sende-Funktionen

### 2.1 Device Descriptor

`main/usb_descriptors.c:21`

```c
const tusb_desc_device_t usb_device_descriptor = {
    .bLength            = sizeof(tusb_desc_device_t), /* 18 */
    .bDescriptorType    = TUSB_DESC_DEVICE,           /* 0x01 */
    .bcdUSB             = 0x0110,
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = USB_KEYBOARD_EP0_SIZE,      /* 8 */
    .idVendor           = USB_KEYBOARD_VID,           /* 0x1209 */
    .idProduct          = USB_KEYBOARD_PID,           /* 0x0001 */
    .bcdDevice          = 0x6402,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x00,
    .bNumConfigurations = 0x01,
};
```

Diese Werte stammen aus dem Mitschnitt `usbtest.pcapng` (Logitech K120,
VID `046d`, PID `c31c`): `bcdUSB=0x0110`, `bMaxPacketSize0=8`,
`bcdDevice=0x6402`, keine Seriennummer.

`bMaxPacketSize0=8` funktioniert nur, wenn TinyUSB EP0 ebenfalls auf 8
Byte läuft. Das geschieht im Projekt über:

```cmake
idf_build_set_property(COMPILE_DEFINITIONS "CFG_TUD_ENDPOINT0_SIZE=8" APPEND)
```

in `CMakeLists.txt`. Ein Descriptor mit 8 bei tatsächlich 64 Byte EP0
würde fehlerhafte Control-Transfers erzeugen. Wenn du EP0 wieder auf 64
stellst, muss auch `bMaxPacketSize0` auf 64 geändert werden.

### 2.2 Configuration + Interface + HID + Endpoint

`main/usb_descriptors.c:53` – ein 34-Byte-Teil der Referenzkonfiguration:

```c
const uint8_t usb_configuration_descriptor[USB_KEYBOARD_CONFIG_DESC_LEN] = {
    /* Configuration: 1 interface, config string 3, 90 mA */
    0x09, 0x02, 0x22, 0x00, 0x01, 0x01, 0x03, 0xA0, 0x2D,
    /* Interface: HID boot keyboard, string 2 */
    0x09, 0x04, 0x00, 0x00, 0x01, 0x03, 0x01, 0x01, 0x02,
    /* HID: bcdHID 1.10, report length 65 */
    0x09, 0x21, 0x10, 0x01, 0x00, 0x01, 0x22, 0x41, 0x00,
    /* Endpoint IN 0x81: interrupt, 8 bytes, 10 ms */
    0x07, 0x05, 0x81, 0x03, 0x08, 0x00, 0x0A,
};
```

Das ist **kein** mit `TUD_CONFIG_DESCRIPTOR()` erzeugter Deskriptor,
sondern der rohe USB-Byte-Strom. Die Referenz-Tastatur hat zusätzlich
eine zweite HID-Schnittstelle (Consumer Control, 184-Byte-Report,
Endpoint `0x82`, `bInterval=255`). Für eine reine Boot-Tastatur ist sie
nicht nötig; die vollständigen 59 Byte stehen in
[Abschnitt 9](#9-auswertung-von-usbtestpcapng).

### 2.3 HID Report Descriptor

`main/usb_descriptors.c:104` – die 65 Byte aus dem Referenz-Mitschnitt:

```c
const uint8_t usb_hid_report_descriptor[USB_KEYBOARD_HID_REPORT_DESC_LEN] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01,
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
    0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05,
    0x91, 0x02, 0x95, 0x01, 0x75, 0x03, 0x91, 0x01,
    0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x26, 0xFF, 0x00,
    0x05, 0x07, 0x19, 0x00, 0x2A, 0xFF, 0x00, 0x81, 0x00,
    0xC0,
};
```

Das 8-Byte-Report-Layout ist unverändert:

| Byte | Bedeutung |
|---|---|
| 0 | Modifier-Bitmap: Bit0 LeftCtrl, Bit1 LeftShift, Bit2 LeftAlt, Bit3 LeftGUI, Bit4 RightCtrl, Bit5 RightShift, Bit6 RightAlt, Bit7 RightGUI |
| 1 | reserviert, immer 0 |
| 2..7 | bis zu sechs HID-Usage-IDs gleichzeitig gedrückter Tasten |

Der Unterschied zur kanonischen 63-Byte-Version sind nur die größeren
Logisch-/Usage-Maxima (`0x00FF` statt `0x0065`). Der Report hat **keine
Report-ID**. `tud_hid_report(0, ...)` sendet also kein führendes
Report-ID-Byte.

### 2.4 String Descriptors

`main/usb_descriptors.c:147`

```c
const char *usb_string_descriptors[] = {
    (const char[]){0x09, 0x04},  /* 0: LangID 0x0409 */
    "Papa",                      /* 1: iManufacturer */
    "USB Keyboard",              /* 2: iProduct, iInterface */
    "USB Keyboard",              /* 3: iConfiguration */
};
```

Die Referenz hat `iSerialNumber=0`. Der Produktstring `USB Keyboard`
wurde aus dem Capture extrahiert. Der Herstellerstring war im Capture
nicht abgefragt; `"Papa"` ist ein Platzhalter. esp_tinyusb wandelt die
UTF-8-Strings selbst in UTF-16LE-String-Deskriptoren um. Für ASCII ist
das byteweise äquivalent zu einem mitgeschnittenen String-Deskriptor
(abgesehen von Länge und LangID).

### 2.5 8-Byte-Report senden

`main/keyboard.c:102`

```c
esp_err_t keyboard_send_report(uint8_t modifiers, const uint8_t keycodes[6])
{
    /* ... Bereitschaft und Mutex ... */
    uint8_t report[8] = { modifiers, 0x00, 0, 0, 0, 0, 0, 0 };
    if (keycodes != NULL) {
        memcpy(&report[2], keycodes, 6);
    }
    const bool queued = tud_hid_report(0, report, sizeof(report));
    /* ... */
}
```

Rückgabewerte:

* `ESP_OK` – Report wurde in die TinyUSB-Queue gelegt.
* `ESP_ERR_NOT_FINISHED` – der vorige Report ist noch nicht übertragen;
  nach einigen Millisekunden erneut versuchen.
* `ESP_ERR_INVALID_STATE` – Host hat noch nicht konfiguriert oder Gerät
  wurde getrennt.

### 2.6 Einzelne Taste drücken und loslassen

`main/keyboard.c:161`

```c
esp_err_t keyboard_tap(uint8_t keycode, uint8_t modifiers, uint32_t hold_ms)
{
    const uint8_t keys[6] = { keycode, 0, 0, 0, 0, 0 };
    esp_err_t err = keyboard_send_report(modifiers, keys);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(hold_ms));
    return keyboard_release_all();   /* 8-Byte-Nullreport, mit Retry */
}
```

Beispiel:

```c
#include "keyboard.h"
#include "class/hid/hid.h"

ESP_ERROR_CHECK(keyboard_init());
ESP_ERROR_CHECK(keyboard_wait_ready(5000));

/* Shift+A */
uint8_t keys[6] = { HID_KEY_A, 0, 0, 0, 0, 0 };
ESP_ERROR_CHECK(keyboard_send_report(KEYBOARD_MODIFIER_LEFTSHIFT, keys));
vTaskDelay(pdMS_TO_TICKS(50));
ESP_ERROR_CHECK(keyboard_release_all());

/* Enter */
ESP_ERROR_CHECK(keyboard_tap(HID_KEY_ENTER, 0, 50));

/* Ctrl+C */
ESP_ERROR_CHECK(keyboard_tap(HID_KEY_C, KEYBOARD_MODIFIER_LEFTCTRL, 50));
```

### 2.7 Testmodus: alle 3 Sekunden `Hallo` + Return

`main/main.c` startet nach der Enumeration den Task `hello_task`. Er
wartet mit `keyboard_wait_ready()` auf den Host und tippt dann
`H` (mit LeftShift), `a`, `l`, `l`, `o`, `Return`:

```c
static const key_press_t HELLO[] = {
    {HID_KEY_H, KEYBOARD_MODIFIER_LEFTSHIFT},
    {HID_KEY_A, 0},
    {HID_KEY_L, 0},
    {HID_KEY_L, 0},
    {HID_KEY_O, 0},
    {HID_KEY_ENTER, 0},
};
```

Jede Taste wird 30 ms gehalten, danach 20 ms Pause. Der Task verwendet
`vTaskDelayUntil()` mit 3000 ms Periode, damit der Abstand von Start zu
Start exakt 3 Sekunden beträgt, unabhängig von der Tippdauer. Bei
fehlender Enumeration oder nach einem Detach wartet er wieder auf den Host.

Zum Abschalten des Testmodus den `hello_task`-Aufruf in `app_main()`
entfernen oder die Konstante `HELLO`/den Task entsprechend auskommentieren.

## 3. Komplette Enumeration mit Wireshark + USBPcap mitschneiden

### 3.1 Windows / USBPcap

1. Wireshark installieren und bei der Installation **USBPcap** mit
   auswählen.
2. Wireshark starten, die Capture-Liste öffnen. Dort erscheinen
   `USBPcap1`, `USBPcap2`, ... (je nach Root-Hub).
3. Die zu untersuchende Tastatur **noch nicht** anstecken.
4. Capture auf dem richtigen USBPcap-Interface starten.
5. Tastatur anstecken, 2–3 Sekunden warten, Capture stoppen.
6. Die Tastatur direkt am Root-Hub anstecken, nicht hinter einem
   externen Hub, wenn USBPcap den Hub nicht mitcaptured.

Auf der Kommandozeile (Windows):

```bat
tshark.exe -D
tshark.exe -i USBPcap1 -w C:\temp\vampire_keyboard_enum.pcapng
```

`USBPcap1` ggf. durch die Nummer ersetzen, die `tshark -D` ausgibt.

### 3.2 Die richtigen Pakete filtern

Zuerst alle Control-Transfers:

```text
usb.transfer_type == 0x02
```

Dann gezielt die Deskriptor-Anfragen. `usb.setup.wValue` enthält im
High-Byte den Deskriptor-Typ und im Low-Byte den Index:

| `wValue` | Bedeutung |
|---|---|
| `0x0100` | Device Descriptor |
| `0x0200` | Configuration Descriptor |
| `0x0300` | String 0 (LangIDs) |
| `0x0301` | String 1 (Hersteller) |
| `0x0302` | String 2 (Produkt) |
| `0x0303` | String 3 (Seriennummer) |
| `0x2100` | HID Descriptor |
| `0x2200` | HID Report Descriptor |
| `0x0600` | Device Qualifier |

Display-Filter:

```text
usb.setup.bRequest == 0x06 && usb.setup.wValue == 0x0100
usb.setup.bRequest == 0x06 && usb.setup.wValue == 0x0200
usb.setup.bRequest == 0x06 && usb.setup.wValue == 0x2200
usb.setup.bRequest == 0x09
usb.setup.bRequest == 0x0B
```

`0x09` ist `SET_CONFIGURATION`, `0x0B` ist `SET_PROTOCOL` (HID),
`0x0A` ist `SET_IDLE`.

In Wireshark als Spalten ergänzen:

* `usb.setup.bRequest`
* `usb.setup.wValue`
* `usb.setup.wIndex`
* `usb.setup.wLength`
* `usb.data_fragment` (Daten bei Control-Transfers)
* `usb.capdata` (Daten bei Interrupt-/Bulk-Transfers)

Die Antwort auf `GET_DESCRIPTOR` steht bei einem Control-IN-Transfer im
`Data Fragment` / `usb.data_fragment`, bei manchen USBPcap-Versionen auch
in `usb.capdata`. Im Zweifel beide Spalten anzeigen.

### 3.3 Erwarteter Ablauf einer funktionierenden Tastatur

1. Reset / Speed-Erkennung.
2. `GET_DESCRIPTOR(DEVICE)`, erst 8 Byte, dann 18 Byte, Adresse 0.
3. `SET_ADDRESS`.
4. `GET_DESCRIPTOR(DEVICE)` an der neuen Adresse.
5. `GET_DESCRIPTOR(CONFIG)`, erst 9 Byte (nur Header), dann die vollen
   `wTotalLength` Byte.
6. `GET_DESCRIPTOR(STRING)` für Index 0, 1, 2, 3 (nicht jeder Host
   fragt alle Strings ab).
7. `SET_CONFIGURATION(1)`.
8. HID-Klasse: `GET_DESCRIPTOR(HID Report)`, oft `SET_IDLE`,
   `SET_PROTOCOL(0)` für Boot-Protocol, danach periodische
   Interrupt-IN-Transfers am Endpoint `0x81`.

Der V4 muss nicht jeden Schritt genauso machen wie Windows. Vergleiche
den V4-Trace mit dem Trace derselben Tastatur an einem Linux-PC. Der
erste Punkt, an dem der V4 anders reagiert, ist der Ansatzpunkt.

## 4. Deskriptor-Bytes aus dem Capture extrahieren

### 4.1 Mit tshark als Hex

Device Descriptor (18 Byte):

```bash
tshark -r vampire_keyboard_enum.pcapng \
  -Y 'usb.setup.bRequest == 0x06 && usb.setup.wValue == 0x0100' \
  -T fields -e usb.data_fragment
```

Configuration Descriptor (voll, nicht die 9-Byte-Vorabantwort):

```bash
tshark -r vampire_keyboard_enum.pcapng \
  -Y 'usb.setup.bRequest == 0x06 && usb.setup.wValue == 0x0200 && usb.setup.wLength > 9' \
  -T fields -e usb.data_fragment
```

HID Report Descriptor:

```bash
tshark -r vampire_keyboard_enum.pcapng \
  -Y 'usb.setup.bRequest == 0x06 && usb.setup.wValue == 0x2200' \
  -T fields -e usb.data_fragment
```

Strings:

```bash
tshark -r vampire_keyboard_enum.pcapng \
  -Y 'usb.setup.bRequest == 0x06 && usb.setup.wValue == 0x0300' \
  -T fields -e usb.data_fragment -e usb.capdata
```

Wenn `usb.data_fragment` leer bleibt, `-e usb.capdata` verwenden. Wenn
die Ausgabe abgeschnitten aussieht, mit `-T json` arbeiten:

```bash
tshark -r vampire_keyboard_enum.pcapng \
  -Y 'usb.setup.bRequest == 0x06' -T json > enum.json
```

### 4.2 Hex in C-Array umwandeln

`tools/usbdesc.py` akzeptiert Leerzeichen, Kommas und Doppelpunkte:

```bash
tshark -r vampire_keyboard_enum.pcapng \
  -Y 'usb.setup.bRequest == 0x06 && usb.setup.wValue == 0x0200 && usb.setup.wLength > 9' \
  -T fields -e usb.data_fragment \
  | python3 tools/usbdesc.py --parse --c usb_configuration_descriptor
```

Beispielausgabe:

```c
/* 34 bytes */
const uint8_t usb_configuration_descriptor[34] = {
    0x09, 0x02, 0x22, 0x00, 0x01, 0x01, 0x03, 0xA0, 0x2D, 0x09, 0x04, 0x00,
    0x00, 0x01, 0x03, 0x01, 0x01, 0x02, 0x09, 0x21, 0x10, 0x01, 0x00, 0x01,
    0x22, 0x41, 0x00, 0x07, 0x05, 0x81, 0x03, 0x08, 0x00, 0x0A,
};
```

`--parse` prüft zusätzlich `wTotalLength`, die Interface-/HID-/Endpoint-
Kette und die HID-Report-Länge. Für den HID-Report:

```bash
tshark ... -e usb.data_fragment \
  | python3 tools/usbdesc.py --parse --type report --c usb_hid_report_descriptor
```

### 4.3 Wo genau die Bytes in den Code kommen

| Capture-Deskriptor | Code-Stelle | Besonderheit |
|---|---|---|
| Device Descriptor | `main/usb_descriptors.c:21`, Felder von `usb_device_descriptor` | Kein Byte-Array, sondern `tusb_desc_device_t`; Feld für Feld übernehmen. |
| Configuration + Interface + HID + Endpoint | `main/usb_descriptors.c:53`, `usb_configuration_descriptor[]` | Kompletter Block inkl. aller `bLength`-Bytes. |
| HID Report Descriptor | `main/usb_descriptors.c:104`, `usb_hid_report_descriptor[]` | Länge muss zu `wDescriptorLength` im HID-Deskriptor passen. |
| Strings | `main/usb_descriptors.c:147`, `usb_string_descriptors[]` | Indizes müssen zu `iManufacturer`, `iProduct`, `iConfiguration` passen; `iSerialNumber=0`. |
| VID/PID | `main/usb_descriptors.h:24` | Beide Werte im Device Descriptor. |
| Längen | `main/usb_descriptors.h:28` | `USB_KEYBOARD_CONFIG_DESC_LEN`, `USB_KEYBOARD_HID_REPORT_DESC_LEN`. |

Beim Ersetzen sind **drei Längen** konsistent zu halten:

1. `USB_KEYBOARD_CONFIG_DESC_LEN` (Array-Größe),
2. `wTotalLength` an Offset 2/3 des Configuration Descriptors,
3. `wDescriptorLength` im HID-Deskriptor (Offset 25/26 des
   Configuration-Blocks, relativ zum HID-Deskriptor Offset 7/8).

Beim HID-Report zusätzlich:

4. `USB_KEYBOARD_HID_REPORT_DESC_LEN` (Array-Größe),
5. `wDescriptorLength` im HID-Deskriptor.

`usb_descriptors_self_check()` prüft alle diese Punkte beim Boot und
bricht mit einer klaren `ESP_LOGE`-Meldung ab, bevor TinyUSB startet.

### 4.4 Endpoint-Reihenfolge und IAD

Für **eine** HID-Schnittstelle gilt diese Reihenfolge:

```text
Configuration Descriptor
Interface Descriptor (bInterfaceNumber=0, bNumEndpoints=1)
HID Descriptor
Endpoint Descriptor (0x81, interrupt IN)
```

Der Endpoint-Deskriptor muss **direkt nach** dem HID-Deskriptor der
zugehörigen Schnittstelle stehen. `usb_descriptors_self_check()` erzwingt
in diesem Projekt, dass der Endpoint der letzte Deskriptor ist.

Ein **Interface Association Descriptor (IAD)** ist bei einer einzelnen
HID-Schnittstelle nicht nötig und kann von einfachen Host-Stacks
sogar abgelehnt werden. Nur bei einem Composite-Gerät (z. B. Keyboard +
Mouse + Consumer Control) kommt ein IAD **vor** die erste zugehörige
Interface-Deskriptor-Gruppe. Dann:

* `wTotalLength` um 8 Byte pro IAD erhöhen,
* `bNumInterfaces` auf die tatsächliche Anzahl setzen,
* Device-Class bei Composite mit IAD auf `0xEF/0x02/0x01`
  (Miscellaneous/Common/IAD) setzen,
* die HID-Klasse muss mehrere Interfaces verwalten; das ist in diesem
  Beispiel absichtlich nicht enthalten.

## 5. Fehlgeschlagene Enumeration analysieren

### 5.1 Zuerst: ist es überhaupt ein Enumeration-Problem?

Ohne Hardware-Analyzer:

* Tastatur an einen Linux-PC stecken und `dmesg -w` beobachten.
* `lsusb -v -d 1209:0001` (VID:PID anpassen) prüfen.
* Wenn das Gerät am Linux-PC sauber erscheint, aber am V4 nicht, ist
  der V4-Host bzw. dessen USB-Stack die Besonderheit.

Am V4 selbst:

* Hardware-USB-Analysator (z. B. Total Phase Beagle USB 480/5000,
  Ellisys, LeCroy) zwischen V4 und Tastatur.
* Trigger auf `SETUP`-Paket, Capture ab Reset bis mindestens 2 Sekunden
  nach dem letzten Enumeration-Paket.
* Decoder auf USB 2.0 Full-Speed stellen.
* Auf `STALL`, `NAK`, `Timeout`, `CRC`/`Babble` und wiederholte
  `RESET`-Sequenzen achten.

`usbmon` auf Linux capturet **den Linux-Host**, nicht den V4-Host. Es ist
trotzdem sehr nützlich, um die eigene Firmware gegen einen bekannten
Host-Stack zu prüfen und den Referenz-Trace zu erzeugen. Für den V4
braucht man einen Inline-Analysator oder einen Host, auf dem man den
V4-USB-Stack debuggen kann.

### 5.2 Linux usbmon + Wireshark

```bash
sudo modprobe usbmon
sudo apt install wireshark tshark
lsusb -t                 # Bus-Nummer ermitteln
sudo tshark -D           # usbmon0, usbmon1, ... anzeigen
sudo tshark -i usbmon1 -w /tmp/usb_keyboard.pcapng
```

`usbmon1` gehört zu Bus 1; `usbmon0` capturet alle Busse. Alternativ:

```bash
sudo dumpcap -i usbmon0 -w /tmp/usb_keyboard.pcapng
```

Danach in Wireshark:

```text
usb.device_address == 5
usb.transfer_type == 0x02
usb.setup.bRequest == 0x06
usb.setup.bRequest == 0x09
```

`dmesg -w` parallel laufen lassen. Typische Fehler:

| Kernel-Meldung | Bedeutung |
|---|---|
| `device descriptor read/64, error -71` | EPROTO: Protokollfehler, oft EP0/Deskriptor |
| `device descriptor read/64, error -32` | EPIPE/STALL |
| `device descriptor read/64, error -110` | Timeout, Gerät antwortet nicht |
| `device not accepting address` | SET_ADDRESS/Reset-Problem |
| `unable to enumerate USB device` | Abbruch vor `SET_CONFIGURATION` |

### 5.3 Den Abbruchpunkt im Trace finden

| Beobachtung | Wahrscheinliche Ursache | Prüfen |
|---|---|---|
| Gar kein `RESET`/`SOF` am V4-Port | Verkabelung, VBUS, Port, S3 gar nicht gestartet | 5V/GND, D+/D-, UART-Log, BOOT/Reset |
| `GET_DESCRIPTOR(DEVICE)` wird gesendet, aber keine Antwort / STALL | EP0, `bMaxPacketSize0`, `bLength`, Firmware hängt | UART-Log, Device-Deskriptor, `CFG_TUD_ENDPOINT0_SIZE` |
| Device-Deskriptor kommt, Host wiederholt `GET_DESCRIPTOR(DEVICE)` oder RESET | Deskriptor unplausibel, EP0-Paketgröße, `bcdUSB`, `bNumConfigurations` | 18-Byte-Response, `bMaxPacketSize0`, `bDescriptorType` |
| `GET_DESCRIPTOR(CONFIG)` wird mit 9 Byte beantwortet, danach STALL/Reset | `wTotalLength` falsch, Deskriptor-Kette überläuft, `bLength=0` | `wTotalLength` == Array-Länge; alle `bLength` |
| Config wird komplett gesendet, aber Host Reset | Interface-/HID-/Endpoint-Felder unplausibel | `bNumInterfaces`, Klasse 3/1/1, HID-Länge |
| `SET_CONFIGURATION` wird gesendet, aber nicht quittiert | Endpoint-Konfiguration, `wMaxPacketSize`, EP-Adresse | Endpoint 0x81, 8 Byte, interrupt, TinyUSB-Log |
| `SET_CONFIGURATION` quittiert, aber keine Interrupt-INs | HID-Report-Deskriptor, `SET_PROTOCOL`, Endpoint-Polling | HID-Report 65 Byte, `bInterval`, `keyboard_ready()` |
| Interrupt-INs kommen, aber immer NAK/keine Daten | `tud_hid_ready()` false, Report nicht gequeued | `keyboard_send_report` Rückgabewert, Mount-Event |
| `SET_PROTOCOL(0)` kommt, danach STALL | Boot-Protocol nicht unterstützt / Report nicht boot-kompatibel | Interface-Protocol 1, 8-Byte-Report, keine Report-ID |

Im Wireshark-Trace bei einem Control-Transfer immer alle drei Stages
prüfen:

1. **Setup Stage** – `bmRequestType`, `bRequest`, `wValue`, `wIndex`,
   `wLength`.
2. **Data Stage** – tatsächliche Länge und Inhalt; bei IN-Transfers die
   Antwort, bei OUT-Transfers die gesendeten Bytes.
3. **Status Stage** – ACK, STALL oder Timeout.

Ein `SET_CONFIGURATION` „ohne Antwort“ ist meist kein fehlendes
Status-Paket, sondern ein vorheriger Descriptor-/Endpoint-Fehler. Der
Host bricht dann vor oder während der Konfiguration ab.

### 5.4 Referenz-Trace vs. ESP32-Trace

Am einfachsten beide Geräte am **selben Linux-Host** capturen:

1. funktionierende Tastatur anstecken, `usbmon`-Capture, Deskriptoren
   mit `lsusb -v` und tshark extrahieren;
2. ESP32-S3 anstecken, gleicher Capture;
3. die beiden Traces ab dem ersten `GET_DESCRIPTOR(DEVICE)` vergleichen.

Der erste Unterschied ist der relevante. Typische Kandidaten sind
`bMaxPacketSize0`, `bcdUSB`, `bmAttributes`, `bInterval`,
`bInterfaceProtocol` und die Länge des HID-Report-Deskriptors.

## 6. Checkliste für die 1:1-Übernahme

### Device Descriptor

- [ ] `bLength = 18`, `bDescriptorType = 0x01`
- [ ] `bcdUSB` exakt übernehmen (häufig `0x0110` oder `0x0200`)
- [ ] `bDeviceClass/SubClass/Protocol`: bei einer HID-Tastatur meist
      `0x00/0x00/0x00`; bei Composite mit IAD `0xEF/0x02/0x01`
- [ ] `bMaxPacketSize0`: Referenz `8`; dafür muss
      `CFG_TUD_ENDPOINT0_SIZE=8` gesetzt sein. Alternativ `64` mit
      EP0=64 verwenden, aber nicht Deskriptor und Stack mischen
- [ ] VID/PID: eigene, rechtlich zulässige Werte; nicht einfach eine
      fremde VID/PID klonen
- [ ] `bcdDevice` und String-Indizes passend zu den Strings
- [ ] `bNumConfigurations = 1`

### Configuration Descriptor

- [ ] `wTotalLength` == tatsächliche Array-Länge (inkl. IAD, falls
      vorhanden)
- [ ] `bNumInterfaces` == tatsächliche Anzahl; für dieses Beispiel `1`
- [ ] `bConfigurationValue != 0` (üblich `1`)
- [ ] `iConfiguration` passt (Referenz `3`)
- [ ] `bmAttributes`: Bit 7 = 1; Bit 6 = self-powered; Bit 5 =
      remote wakeup. Referenz `0xA0` (bus-powered + remote wakeup)
- [ ] `bMaxPower` in 2-mA-Einheiten; Referenz `0x2D` = 90 mA. Muss zum
      tatsächlichen Strombedarf passen.

### Interface Descriptor

- [ ] `bInterfaceNumber = 0`, `bAlternateSetting = 0`
- [ ] `bNumEndpoints = 1` (bei dieser Tastatur)
- [ ] `bInterfaceClass = 0x03` (HID)
- [ ] `bInterfaceSubClass = 0x01` (Boot Interface)
- [ ] `bInterfaceProtocol = 0x01` (Keyboard)
- [ ] `iInterface` passt (Referenz `2` = Produktstring)

### HID Descriptor

- [ ] `bLength = 9`, `bDescriptorType = 0x21`
- [ ] `bcdHID` exakt übernehmen (Referenz `0x0110`)
- [ ] `bCountryCode` exakt übernehmen (meist `0`)
- [ ] `bNumDescriptors = 1`
- [ ] `bDescriptorType = 0x22` (Report)
- [ ] `wDescriptorLength` == exakte Länge von
      `usb_hid_report_descriptor[]`

### Endpoint Descriptor

- [ ] `bLength = 7`, `bDescriptorType = 0x05`
- [ ] `bEndpointAddress = 0x81` (IN 1); nicht mit einem OUT-Endpoint
      verwechseln
- [ ] `bmAttributes = 0x03` (Interrupt)
- [ ] `wMaxPacketSize = 8` (Boot-Keyboard-Report)
- [ ] `bInterval` exakt übernehmen: 1, 8, 10 oder 24 ms sind typisch;
      der V4 kann auf ein unpassendes Polling-Intervall empfindlich
      reagieren
- [ ] Endpoint-Deskriptor steht direkt nach dem HID-Deskriptor und ist
      der letzte Deskriptor der Konfiguration

### HID Report Descriptor

- [ ] exakt 65 Byte wie im Referenz-Capture (oder 63 Byte kanonisch)
- [ ] Byte 0 = Modifier, Byte 1 = reserviert, Byte 2..7 = sechs
      Keycodes
- [ ] **keine Report-ID** (`0x85` am Anfang) bei Boot-Protocol
- [ ] LED-Output-Report vorhanden, wenn der Host LEDs setzen soll
- [ ] Länge im HID-Deskriptor und Array identisch

### Strings / IAD

- [ ] String 0 = LangID `0x0409` (bzw. die LangID des Captures)
- [ ] `iManufacturer`, `iProduct`, `iConfiguration` verweisen auf
      vorhandene Indizes; Referenz `iSerialNumber=0`
- [ ] Seriennummer nur setzen, wenn der Host sie erwartet
- [ ] **Kein IAD** bei nur einer HID-Schnittstelle
- [ ] Bei Composite: IAD vor der Interface-Gruppe, `wTotalLength`
      anpassen, Device-Class `0xEF/0x02/0x01`

## 7. Bekannte Stolperfallen

1. **USB-Serial-JTAG vs. OTG**: Beide nutzen GPIO19/20. Die
   USB-Serial-JTAG-Konsole verschwindet, sobald TinyUSB läuft. UART0
   verwenden oder während der Entwicklung einen zweiten UART-Adapter
   anschließen.
2. **VBUS/Backfeed**: Ein Dev-Board, das über seinen USB-Port versorgt
   wird, darf nicht gleichzeitig über einen zweiten 5V-Eingang
   rückgespeist werden. Bei Busversorgung die Stromaufnahme prüfen.
3. **Full-Speed vs. Low-Speed**: Der S3 kann kein Low-Speed. Wenn der
   V4-Port ausschließlich Low-Speed-Geräte akzeptiert, hilft nur ein
   anderer USB-Mikrocontroller mit Low-Speed-Unterstützung.
4. **Deskriptor-Länge**: `wTotalLength` und `wDescriptorLength` werden
   von vielen Host-Stacks streng geprüft. Ein einzelnes falsches
   Längenbyte führt oft zu einem Reset mitten in der Enumeration.
5. **Endpoint-Reihenfolge**: Endpoint-Deskriptoren gehören unmittelbar
   hinter ihren Interface-/HID-Deskriptor. Nicht alle Endpoints ans Ende
   des Config-Blocks sammeln.
6. **Report-ID**: Die kanonische Boot-Tastatur hat keine Report-ID.
   `tud_hid_report(0, report, 8)` ist korrekt; kein zusätzliches `0x00`
   vor den 8 Bytes einfügen.
7. **`bInterval`**: Der Wert steht im Endpoint-Deskriptor und wird von
   TinyUSB an den Host gemeldet. Nicht zur Laufzeit „optimieren“, ohne
   den Deskriptor zu ändern.
8. **`bMaxPacketSize0`**: Nicht isoliert von der EP0-Konfiguration des
   Stacks ändern.

## 8. Weitere Befehle

Deskriptoren im gebauten ELF gegenprüfen:

```bash
source $HOME/esp-idf/export.sh

xtensa-esp32s3-elf-nm -S --size-sort build/esp32s3_vampire_keyboard.elf \
  | grep -E 'usb_(configuration|hid|device|string)'

xtensa-esp32s3-elf-objcopy \
  --dump-section .flash.rodata=/tmp/rodata.bin \
  build/esp32s3_vampire_keyboard.elf

# Adresse aus nm einsetzen; Beispiel für den 65-Byte-Report:
xxd -g1 -s 0xA140 -l 65 /tmp/rodata.bin
```

Firmware-Größe und Partitionen:

```bash
idf.py size
idf.py size-components
```

USB-Traffic live ansehen (Linux):

```bash
sudo tshark -i usbmon0 -Y 'usb.transfer_type == 0x02' -V
```

## 9. Auswertung von `usbtest.pcapng`

Der Capture enthält 1716 USBPcap-Pakete auf Bus 2. Die funktionierende
Tastatur ist **Bus 2, Device 1**: Logitech `046d:c31c` (K120). Device 2
ist ein weiteres HID-Gerät (`046d:c534`) mit 4-Byte-Interrupt-Reports und
für die Tastatur nicht relevant.

Wichtigste Erkenntnisse:

* Device: `bcdUSB=0x0110`, `bMaxPacketSize0=8`, `bcdDevice=0x6402`,
  `iSerialNumber=0`.
* Config: **59 Byte, 2 Interfaces**, `bmAttributes=0xA0`,
  `bMaxPower=0x2D` (90 mA), kein IAD.
* Interface 0: Boot-Keyboard, Report **65 Byte**, Endpoint `0x81`,
  8 Byte, `bInterval=10`.
* Interface 1: Consumer Control, Report 184 Byte, Endpoint `0x82`,
  4 Byte, `bInterval=255`.
* Produktstring: `USB Keyboard`; `SET_PROTOCOL` kommt im Capture nicht
  vor, die Tastatur läuft direkt im Boot-Protocol.
* Tastatur-Reports sind 8 Byte: Modifier, reserviert, sechs Keycodes,
  keine Report-ID.

Das Projekt nutzt die **Single-Interface-Teilmenge** mit dem 65-Byte-
Report. Die vollständigen 59 Byte, den 184-Byte-Consumer-Report und den
Enumerationsablauf findest du in
[captures/usbtest_analysis.md](captures/usbtest_analysis.md).

Wenn der V4 die vollständige 2-Interface-Konfiguration erwartet, muss
`CONFIG_TINYUSB_HID_COUNT=2` gesetzt und der zweite Report über
`tud_hid_descriptor_report_cb(instance)` bereitgestellt werden. Die
59-Byte-Konfiguration kann dann unverändert übernommen werden.
