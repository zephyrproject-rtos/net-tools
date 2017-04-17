.. _net-tools-systemd-unit-files:

systemd unit files
##################

These will start the tap0 network interface for you so that you don't have to
have loop-socat.sh and loop-slip-tap.sh open in other terminals when you run
Zephyr in QEMU with networking.

Installation
************

The loop-socat creates a file which needs to be readable and writeable by the
user who is running Zephyr in QEMU. Therefore you need to create a zephyr user
before running these unit files.

.. code-block:: console

   $ sudo useradd -r zephyr
   $ sudo usermod -aG zephyr $USER
   $ cp *.service /usr/lib/systemd/system/
   $ sudo chown root:root /usr/lib/systemd/system/loop-s*

Now edit the unit files to provide them with the locations of your net-tools
scripts. Then start the services.

.. code-block:: console

   $ sudo systemctl daemon-reload
   $ sudo systemctl enable loop-socat loop-slip-tap
   $ sudo systemctl start loop-socat loop-slip-tap
