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
# as RTP; rtp_audio.c says how. Any player that knows RTP can play it, and
# this plays it with ffplay into the first Bluetooth sound output the
# desktop has. Pair and connect the earphone from the desktop's Bluetooth
# menu first. The author asked for this to be kept: a Bluetooth earphone on
# the radio was a much wanted thing, and this is all it took.
#
# The desktop's sound panel shows no slider for a Bluetooth output, which
# is why the level is set here. The radio's own volume control does not
# change it: the sound is sent out before the sound card's volume.
#
# It needs ffplay, of the package ffmpeg, and the desktop's sound server,
# PipeWire or PulseAudio, both of which Raspberry Pi OS has.

GROUP=239.255.73.88
PORT=5004

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"

# stopping needs no earphone: the player is stopped whether or not the
# earphone it was playing into is still there
if [ "$1" = stop ]; then
	pkill -f "ffplay .*rtp://$GROUP:$PORT" && echo "stopped"
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
	if pgrep -f "ffplay .*rtp://$GROUP:$PORT" > /dev/null; then
		echo "already playing into $sink"
		exit 0
	fi
	PULSE_SINK="$sink" SDL_AUDIODRIVER=pulse setsid ffplay -nodisp -loglevel error \
		-fflags nobuffer -flags low_delay -probesize 32 -analyzeduration 0 \
		"rtp://$GROUP:$PORT?localaddr=127.0.0.1" < /dev/null > /dev/null 2>&1 &
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
