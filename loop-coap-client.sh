STOPPED=0
trap ctrl_c INT TERM

function ctrl_c() {
    STOPPED=1
    killall coap-client
}

while [ $STOPPED -eq 0 ];
do
	./coap-client -i tun0 2001:db8::2
	sleep 1
done
