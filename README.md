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
## Build Modules
```

$ make

```
## Install Modules

$ sudo make install
