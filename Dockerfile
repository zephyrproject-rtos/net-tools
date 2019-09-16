# Create Docker image that can be used for testing Zephyr network
# sample applications.

FROM fedora
FROM gcc

RUN git clone http://github.com/zephyrproject-rtos/net-tools.git && \
    make /net-tools/tunslip6 && make /net-tools/echo-client && \
    make /net-tools/echo-server && make /net-tools/throughput-client

WORKDIR /net-tools

# We do not run any command automatically but let the test script run
# the proper test application script.
