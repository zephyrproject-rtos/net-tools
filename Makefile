all: tunslip6 echo-client echo-server monitor_15_4 coap-client dtls-client dtls-server throughput-client

tunslip6: tunslip6.o
	$(CC) -o $@ $(CFLAGS) $(LIBS) tunslip6.c

echo-client: echo-client.o
	$(CC) -o $@ $(CFLAGS) $(LIBS) echo-client.c

echo-server: echo-server.o
	$(CC) -o $@ $(CFLAGS) $(LIBS) echo-server.c

throughput-client: throughput-client.o
	$(CC) -o $@ $(CFLAGS) $(LIBS) throughput-client.c

TINYDTLS = tinydtls-0.8.2
TINYDTLS_CFLAGS = -I$(TINYDTLS) -DDTLSv12 -DWITH_SHA256 -DDTLS_ECC -DDTLS_PSK
TINYDTLS_LIB = $(TINYDTLS)/libtinydtls.a

MBEDTLS = mbedtls-2.4.0
MBEDTLS_CFLAGS = -I$(MBEDTLS)/include
MBEDTLS_LIB =  $(MBEDTLS)/library/libmbedtls.a \
	$(MBEDTLS)/library/libmbedx509.a $(MBEDTLS)/library/libmbedcrypto.a

$(TINYDTLS_LIB):
	(cd tinydtls-0.8.2; ./configure; make)

.PHONY: tinydtls
tinydtls: $(TINYDTLS_LIB)

$(MBEDTLS_LIB):
	(cd mbedtls-2.4.0; make)

.PHONY: mbedtls
mbedtls: $(MBEDTLS_LIB)

dtls-client.o: dtls-client.c
	$(CC) -c -o $@ $(CFLAGS) $(MBEDTLS_CFLAGS) dtls-client.c

dtls-client: dtls-client.o $(MBEDTLS_LIB)
	$(CC) -o $@ $(LIBS) dtls-client.o $(MBEDTLS_LIB)

dtls-server.o: dtls-server.c
	$(CC) -c -o $@ $(CFLAGS) $(MBEDTLS_CFLAGS) dtls-server.c

dtls-server: dtls-server.o $(MBEDTLS_LIB)
	$(CC) -o $@ $(LIBS) dtls-server.o $(MBEDTLS_LIB)

GLIB=`pkg-config --cflags --libs glib-2.0`

monitor_15_4.o: monitor_15_4.c
	$(CC) -c -o $@ $(CFLAGS) $(GLIB) monitor_15_4.c

monitor_15_4: monitor_15_4.o
	$(CC) -o $@ $(LIBS) monitor_15_4.o $(GLIB)

LIBCOAP = libcoap
LIBCOAP_CFLAGS = -I$(LIBCOAP)/include -I$(LIBCOAP) -DWITH_POSIX
LIBCOAP_LIB = $(LIBCOAP)/.libs/libcoap-1.a

$(LIBCOAP_LIB):
	(cd libcoap; ./autogen.sh; ./configure --disable-documentation; make)

.PHONY: libcoap
libcoap: $(LIBCOAP_LIB)

coap-client.o: coap-client.c libcoap tinydtls
	$(CC) -c -o $@ $(CFLAGS) $(TINYDTLS_CFLAGS) $(LIBCOAP_CFLAGS) coap-client.c

coap-client: coap-client.o $(TINYDTLS_LIB) $(LIBCOAP_LIB)
	$(CC) -o $@ $(LIBS) coap-client.o $(TINYDTLS_LIB) $(LIBCOAP_LIB)

.PHONY: clean-libcoap
clean-libcoap:
	(cd libcoap; make distclean)

.PHONY: clean-tinydtls
clean-tinydtls:
	(cd tinydtls-0.8.2; make distclean)

.PHONY: clean-mbedtls
clean-mbedtls:
	(cd mbedtls-2.4.0; make clean)

clean: clean-libcoap clean-tinydtls clean-mbedtls
	rm -f *.o tunslip6 tunslip echo-client echo-server dtls-client dtls-server monitor_15_4 coap-client throughput-client
