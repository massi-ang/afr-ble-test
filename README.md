## Cloning
This repo uses [Git Submodules](https://git-scm.com/book/en/v2/Git-Tools-Submodules) to bring in dependent components.

Note: If you download the ZIP file provided by GitHub UI, you will not get the contents of the submodules. (The ZIP file is also not a valid git repository)

To clone use:
```
git clone <repo> --recurse-submodules
```

If you have downloaded the repo without using the `--recurse-submodules` argument, you need to run:
```
git submodule update --init --recursive
```

## Customization

The firmware is designed to blink a led connected to the GPIO pin set for `LED_PIN`. You can change the pin number to match what is available in your setup.

You find the `#define` for the `LED_PIN` in `vendor/espressif/boards/esp32/aws_demos/application_code/main.c`

## How to run

Run the following to build and install the demo on your own ESP32 (on Mac or Linux):

```
cd afr-ble-test
mkdir build
cmake -DVENDOR=espressif -DBOARD=esp32_wrover_kit -DCOMPILER=xtensa-esp32 -S . -B ../build
cd build
make flash -j4
```

Ensure to have the ESP32 board connected via USB before executing the above commands. In case the flashing does not work, you can set the ESPPORT environment variable to the correct serial port (on Mac it is typically `/dev/cu.SLAB_USBtoUART`)

After the device is flashed you need to connect via the serial port

```
screen /dev/cu.SLAB_USBtoUART 115200
```

You must have this screen open when pairing from the mobile app in order to be able to confirm the pairing code.


## Getting Started

For more information on FreeRTOS, refer to the [Getting Started section of FreeRTOS webpage](https://aws.amazon.com/freertos).

To directly access the **Getting Started Guide** for supported hardware platforms, click the corresponding link in the Supported Hardware section below.

For detailed documentation on FreeRTOS, refer to the [FreeRTOS User Guide](https://aws.amazon.com/documentation/freertos).


