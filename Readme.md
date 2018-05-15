This is a Linux driver for the Watchdog functionality of the NCT5104D SuperIO chip.

The NCT5104D is an LPC SuperIO chip that provides:
 - Port80 debug output
 - 4 x UARTs
 - GPIO (muxed with UART C and UART D)
 - Watchdog

The [datasheet](NCT5104D_Datasheet_V1_9.pdf) is openly available.

This driver deals only with the watchdog functionality.

The UART functionality is supported by the Linux serial8250 driver as a 16550A device.

GPIO drivers for this chip have been seen on github and linux-gpio mailing lists.

Build the driver:

    $ make KDIR=/path/to/kernel-sources/

or

    $ export KDIR=/path/to/kernel-sources/
    $ make
    $ make modules_install
