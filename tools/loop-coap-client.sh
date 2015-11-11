while [ 1 ];
do
	./coap-client -i tun0 2001:db8::2
	sleep 1
done
