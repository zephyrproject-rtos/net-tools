# Create Docker image that can be used for testing Zephyr network
# sample applications.

FROM fedora
FROM gcc

# RUN http_proxy=... git clone...
RUN git clone http://github.com/zephyrproject-rtos/net-tools.git && \
    cd /net-tools && \
    make tunslip6 && make echo-client && \
    make echo-server && make throughput-client && \
    make coap-client

WORKDIR /net-tools

# We do not run any command automatically but let the test script run
# the proper test application script.
