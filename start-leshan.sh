#!/bin/sh -eu

# Download Leshan if needed
if [ ! -f leshan-server-demo.jar ]; then
        wget https://ci.eclipse.org/leshan/job/leshan/lastSuccessfulBuild/artifact/leshan-server-demo.jar
fi

if [ ! -f leshan-bsserver-demo.jar ]; then
	wget 'https://ci.eclipse.org/leshan/job/leshan/lastSuccessfulBuild/artifact/leshan-bsserver-demo.jar'
fi

mkdir -p log

start-stop-daemon --make-pidfile --pidfile log/leshan.pid --chdir $(pwd) --background --start \
        --startas /bin/bash -- -c "exec java -jar ./leshan-server-demo.jar -wp 8080 -vv --models-folder objects >log/leshan.log 2>&1"

start-stop-daemon --make-pidfile --pidfile log/leshan_bs.pid --chdir $(pwd) --background --start \
        --startas /bin/bash -- -c "exec java -jar ./leshan-bsserver-demo.jar -lp=5783 -slp=5784 -wp 8081 -vv >log/leshan_bs.log 2>&1"
