# radio_hal
WIP:Generic Radio HAL for different wireless radio(Wi-Fi/BT/15.4)
Generic Radio HAL provides Generic API to application layer and abstraction to
1) Standard radio driver interfaces(NL80211/wpa_supplicant/D-BUS/serial device).
2) Non-standard radio driver interfaces ex:sysfs, debugfs, relayfs, vendor IOCTL.

## Install & Clone


## Clone repositories:

```

$ git clone https://github.com/tiiuae/radio_hal

$ cd radio_hal


```
## Install netlink library
```

$ sudo apt install libnl-genl-3-dev

```
## Install yaml library
```

$ sudo apt install libyaml-dev

```
## Install libqmi-utils for modem (qmicli)
```

$ sudo apt-get install libqmi-utils

```
## Build Modules
```

$ make

```

## Install Modules
```

$ sudo make install

```

## Build and install for unit testing
```
$ make RADIO_HAL_UNIT_TEST=1
$ sudo make install RADIO_HAL_UNIT_TEST=1
```
