#!/bin/bash

function help
{
	echo
	echo "Wifi usage: sudo $0 [radio_index] [ap|mesh|sta] <ssid> <psk> <ip> <mask> <freq>"
	echo "Modem usage:"
	echo "    sudo $0 modem <apn> <pin> <at_serial_port>"
	echo "    Hox!! if you are testing with ubuntu, then might need to disable modem manager:"
	echo "    sudo systemctl stop ModemManager.service"
	echo "    sudo systemctl disable ModemManager.service"
	echo
	echo "example:"
	echo "sudo $0 mesh test 12345678 192.168.1.2 255.255.255.0 5220"
	echo "sudo $0 ap test 12345678 192.168.1.2 255.255.255.0 5220"
	echo "sudo $0 sta test 12345678 192.168.1.2 255.255.255.0 5220"
	echo
	echo "modem example:"
	echo "sudo $0 modem internet 1234 /dev/ttyUSB2"
	exit
}

if [ -z "$1" ]; then
	echo "check arguments..."
	help
fi

mesh_activation()
{
	killall alfred 2>/dev/null
	killall batadv-vis 2>/dev/null
	rm -f /var/run/alfred.sock

	modprobe batman-adv

	radio_hal_daemon -w 0 radio_mesh_join "$2" "$3" "$6"

	echo "bat0 up.."
	batctl if add mesh0
	ifconfig bat0 up

	echo "bat0 ip address.."
	ifconfig bat0 "$4" netmask "$5"

	echo "mesh0 mtu size"
	ifconfig mesh0 mtu 1560
	echo "bat0 mtu size"
	ifconfig bat0 mtu 1460

	ifconfig bat0
	sleep 3

	# for visualisation
	(alfred -i bat0 -m)&
	echo "started alfred"
	(batadv-vis -i bat0 -s)&
	echo "started batadv-vis"
}

sta_activation()
{

	radio_hal_daemon -w 0 radio_hal_connect_ap "$2" "$3"
	# udhcp -i wlp1s0 TODO
	ifconfig wlp1s0 "$4" netmask "$5"
}

ap_activation()
{
	radio_hal_daemon -w 0 radio_hal_create_ap "$2" "$3" "$6"
	# dhcp service TODO
	ifconfig wlp1s0 "$4" netmask "$5"
}

connect_internet()
{
  radio_hal_daemon -m 0 radio_hal_get_rssi "$2" "$3" "$4"
  #radio_hal_daemon -m radio_hal_connect "$2" "$3" "$4"
  if [ $? -eq 0 ]; then
    ### get cdc-wdm device ####
    device=$(ls /sys/class/usbmisc/ |grep cdc)

    ### get wwan name ###
    wwan=$(qmicli --device=/dev/$device --get-wwan-iface)

    udhcpc -q -f -n -i $wwan
  else
    echo "radio_hal_daemon fails"
  fi
}

off()
{
	# service off
	pkill -f "/tmp/wpa_supplicant.conf" 2>/dev/null
	rm -fr /tmp/wpa* 2>/dev/null
	killall alfred 2>/dev/null
	killall batadv-vis 2>/dev/null
	rm -f /var/run/alfred.sock 2>/dev/null

	#modem
	killall udhcpc 2>/dev/null
}

### MAIN ###
main ()
{
	# parameters:
	#  1             2      3     4    5      6
	# [ap|mesh|sta] <ssid> <psk> <ip> <mask> <freq>"

	echo "- init_device $1"
	echo

	case "$1" in
		mesh)
			mesh_activation "$@"
			;;
		ap)
			ap_activation "$@"
			;;
		sta)
			sta_activation "$@"
			;;
		modem)
      connect_internet "$@"
			;;
		off)
			off "$@"
			;;
		*)
			help
			;;
	esac
}

main "$@"