#!/bin/bash
# bluetooth_audio.sh — the radio's sound in a Bluetooth earphone or speaker
# that has been paired with the radio's computer.
#
#     setup/bluetooth_audio.sh            start playing
#     setup/bluetooth_audio.sh stop       stop
#     setup/bluetooth_audio.sh 70         set how loud, 0 to 100
#     setup/bluetooth_audio.sh +10        louder by so much; -10, softer
#
# The radio program sends what its speaker plays out on the radio itself,
# as RTP; rtp_audio.c says how: 16-bit samples, 48000 a second, to a
# multicast group on the radio. This takes the samples out of the packets
# and hands them to pacat, the sound server's own player, which plays them
# into the first Bluetooth sound output the desktop has. Pair and connect
# the earphone from the desktop's Bluetooth menu first. The author asked for
# this to be kept: a Bluetooth earphone on the radio was a much wanted
# thing, and this is all it took. It is run by hand, or by the gateway when
# BLUETOOTH among the radio's CONNECTIONS is turned on.
#
# It was ffplay that played it, when the sound was the telephone's, which
# any player of RTP knows without being told. The sound as it is now has no
# number of its own in RTP, and a player has to be told what it is; taking
# the samples out here asks nothing of any player.
#
# The desktop's sound panel shows no slider for a Bluetooth output, which
# is why the level is set here. The radio's own volume control does not
# change it: the sound is sent out before the sound card's volume.
#
# It needs python3 and the desktop's sound server, PipeWire or PulseAudio,
# with its pacat and pactl, all of which Raspberry Pi OS has.

GROUP=239.255.73.88
PORT=5004

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"

# stopping needs no earphone: the player is stopped whether or not the
# earphone it was playing into is still there
# what takes the samples out of the packets is known by this word in it
MARK=zbitx_bluetooth_audio

if [ "$1" = stop ]; then
	pkill -f "$MARK" && echo "stopped"
	exit 0
fi

sink=$(pactl list short sinks 2>/dev/null | awk '$2 ~ /^bluez_output/ { print $2; exit }')
if [ -z "$sink" ]; then
	echo "no Bluetooth sound output is connected: connect the earphone from the desktop's"
	echo "Bluetooth menu, or switch it on, and run this again"
	exit 1
fi

case "${1:-start}" in
start)
	if pgrep -f "$MARK" > /dev/null; then
		echo "already playing into $sink"
		exit 0
	fi
	# each packet is RTP's 12 bytes and then the samples
	setsid sh -c "python3 -u -c '
import socket, sys  # $MARK
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind((\"\", $PORT))
s.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP,
             socket.inet_aton(\"$GROUP\") + socket.inet_aton(\"127.0.0.1\"))
out = sys.stdout.buffer
while True:
    out.write(s.recv(4000)[12:])
    out.flush()
' | pacat --raw --format=s16be --rate=48000 --channels=1 --latency-msec=60 \
		--device='$sink' --client-name=zbitx" < /dev/null > /dev/null 2>&1 &
	echo "playing the radio into $sink"
	;;
[+-][0-9]*)
	pactl set-sink-volume "$sink" "$1%"
	pactl get-sink-volume "$sink" | grep -o '[0-9]*%' | head -1
	;;
[0-9]*)
	pactl set-sink-volume "$sink" "$1%"
	pactl get-sink-volume "$sink" | grep -o '[0-9]*%' | head -1
	;;
*)
	echo "bluetooth_audio.sh [start | stop | 0 to 100 | +N | -N]"
	exit 1
	;;
esac
