#!/bin/bash

function help
{
	echo
	echo
	echo "example:"
	echo "sudo $0 ../wifi_example.yaml 192.168.1.2 255.255.255.0"

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

	radio_manager -w $1

	echo "bat0 up.."
	batctl if add mesh0
	ifconfig bat0 up

	echo "bat0 ip address.."
	ifconfig bat0 "$2" netmask "$3"

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

off()
{
	# service off
	pkill -f "/tmp/wpa_supplicant.conf" 2>/dev/null
	rm -fr /tmp/wpa* 2>/dev/null
	killall alfred 2>/dev/null
	killall batadv-vis 2>/dev/null
	rm -f /var/run/alfred.sock 2>/dev/null
}

### MAIN ###
main ()
{
	# parameters:

	echo "- init_device $1"
	echo

	case "$1" in
		*wifi*)
			mesh_activation "$@"
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


