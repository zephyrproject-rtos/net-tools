# Create Docker image that can be used for testing Zephyr network
# sample applications.

FROM gcc

# RUN https_proxy=... git clone...
RUN git clone https://github.com/zephyrproject-rtos/net-tools.git && \
    cd /net-tools && \
    make tunslip6 && make echo-client && \
    make echo-server && make throughput-client && \
    make coap-client

# RUN https_proxy=... git clone...
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
    rm -rf /mosquitto

RUN addgroup --system mosquitto && \
    adduser --system \
    --no-create-home \
    --disabled-password \
    --disabled-login \
    --ingroup mosquitto \
    mosquitto

COPY mosquitto.conf /usr/local/etc/mosquitto/
COPY mosquitto-tls.conf /usr/local/etc/mosquitto/

# RUN http_proxy=... apt...
RUN apt update && apt install dante-server -y

COPY danted.conf /etc/

WORKDIR /net-tools

# We do not run any command automatically but let the test script run
# the proper test application script.
