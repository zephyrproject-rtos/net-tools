# Create Docker image that can be used for testing Zephyr network
# sample applications.

FROM gcc:bookworm

# Get all the extra app we need in the container
RUN apt update && apt install -y \
    dante-server \
    curl \
    netcat-traditional \
    tcpdump \
    default-jre-headless \
    dnsmasq \
    iproute2 \
    iputils-ping

# We need the net-tools project as it contains helper apps needed
# in testing.
RUN git clone https://github.com/zephyrproject-rtos/net-tools.git && \
    cd /net-tools && \
    make tunslip6 && make echo-client && \
    make echo-server && make throughput-client && \
    make coap-client

# linuxptp daemon is needed in gPTP testing
RUN git clone https://git.code.sf.net/p/linuxptp/code linuxptp && \
    cd /linuxptp && \
    git checkout v3.1 && \
    make && \
    make install

# MQTT testing
RUN git clone https://github.com/eclipse/mosquitto.git && \
    cd /mosquitto && \
    git checkout v1.6.9 && \
    make binary && \
    install -d /usr/local/bin/ && \
    install -d /usr/local/sbin/ && \
    install -d /usr/local/lib/ && \
    install -d /usr/local/etc/mosquitto/certs && \
    install -d /var/lib/mosquitto && \
    install -s -m755 /mosquitto/client/mosquitto_pub \
	       /usr/local/bin/mosquitto_pub && \
    install -s -m755 /mosquitto/client/mosquitto_rr \
	       /usr/local/bin/mosquitto_rr && \
    install -s -m755 /mosquitto/client/mosquitto_sub \
	       /usr/local/bin/mosquitto_sub && \
    install -s -m644 /mosquitto/lib/libmosquitto.so.1 \
	       /usr/local/lib/libmosquitto.so.1 && \
    install -s -m755 /mosquitto/src/mosquitto /usr/local/sbin/mosquitto && \
    install -s -m755 /mosquitto/src/mosquitto_passwd \
	       /usr/local/bin/mosquitto_passwd && \
    rm -rf /mosquitto; \
    addgroup --system mosquitto && \
    adduser --system \
	    --no-create-home \
	    --disabled-password \
	    --disabled-login \
	    --ingroup mosquitto \
	    mosquitto

COPY mosquitto.conf mosquitto-tls.conf /usr/local/etc/mosquitto/

# Simple Python based HTTP server for http-client API testing
# The http-get-file-test.sh is used for testing TCP with dumb-http-server-mt
# network sample.
# The syslog-receiver.py will test syslog-net sample
COPY http-server.py https-server.py http-get-file-test.sh \
     syslog-receiver.py connectivity-check.sh /usr/local/bin/
COPY http-get-file-test.sh /usr/local/bin/https-get-file-test.sh

# Dante is SOCKS proxy. The gptp.conf is conf file for linuxptp.
COPY danted.conf gptp.cfg /etc/

# Leshan demo server
RUN cd /net-tools && \
    curl 'https://repo1.maven.org/maven2/org/eclipse/leshan/leshan-server-demo/2.0.0-M14/leshan-server-demo-2.0.0-M14-jar-with-dependencies.jar' -o leshan-server-demo.jar && \
    curl 'https://repo1.maven.org/maven2/org/eclipse/leshan/leshan-bsserver-demo/2.0.0-M14/leshan-bsserver-demo-2.0.0-M14-jar-with-dependencies.jar' -o leshan-bsserver-demo.jar

# DNS resolver to test online connectivity checker
COPY dnsmasq.conf /etc/

WORKDIR /net-tools

# We do not run any command automatically but let the test script run
# the proper test application script.
