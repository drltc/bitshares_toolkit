KeyID (a.k.a. DNS DAC)
======================

Powered by the BitShares DAC toolkit, KeyID is a free and open-source
distributed autonomous identity platform.

Running KeyID using Docker
--------------------------

An easy way to run KeyID on GNU/Linux is by using Docker.  For example:

    docker run -it keyid/keyid

The first time you run this command, Docker automatically downloads the latest
version of the `keyid/keyid` image --- like a package manager would.

The `-i` ("interactive") and `-t` ("terminal") options are necessary if you
want to use the command prompt.  (Otherwise, you have to pass the `--daemon`
flag to KeyID --- see below.)

Blockchain and wallet data is stored in a volume called `/data`, which we can
mount to the host file system using the `-v` option.

    export KEYID_DATA=~/.keyid
    docker run -it -v $KEYID_DATA:/data keyid/keyid

### Running KeyID as a daemon

To run KeyID as a daemon under Docker, pass `-d` to `docker run`, name the
container, and pass `--daemon` to KeyID itself:

    export KEYID_DATA=/var/local/lib/keyid
    docker run -d --name=keyid -v $KEYID_DATA:/data keyid/keyid --daemon

Note that if the host machine reboots, in a default setup, Docker will restart
any container that was running when the host shut down.

If you're going to call KeyID programmatically, you may want to expose the
JSON-RPC API server running on port 80 to a port on the host (e.g., 5044):

    docker run -d --name=keyid -p 5044:80 keyid/keyid --daemon

The JSON-RPC API would then be accessible at `http://localhost:5044/rpc`.

### Seed nodes

Seed nodes need to expose port 1791 so that others can connect to them:

    docker run --name=keyid -d -p 1791:1791 keyid/keyid --daemon

### Storing data in a volume container

Instead of mounting a host directory as the `/data` volume, you can create a
so-called "volume container" --- a named dummy container whose only purpose is
to have a designated volume for storing data:

    docker run --name=keyid-data --entrypoint=true keyid/keyid

This allows you to restart KeyID (and run different versions if you want)
while retaining all data in the `keyid-data` volume container:

    docker run -it --rm --volumes-from=keyid-data keyid/keyid

Note that we can tell Docker to remove the container after it stops (`--rm`)
because all the interesting data will be in the `keyid-data` container.


Running KeyID outside Docker
----------------------------

Of course, there is no need to use Docker if you prefer not to.  The next
section contains installation instructions for Ubuntu 14.04.

When you run KeyID directly on your machine, the `~/.KeyID` directory is used
to store blockchain and wallet data.  This can be changed using `--data-dir`:

    keyid --data-dir=~/.KeyID-foo

To use the JSON-RPC API, you may want to specify the port to listen to:

    keyid --daemon --httpdendpoint=127.0.0.1:5044

No special configuration is necessary to run a seed node outside Docker except
to make sure that port 1791 is open for incoming connections in your firewall.


Building and installing KeyID on Ubuntu 14.04
---------------------------------------------

To run KeyID on Ubuntu 14.04, the following dependencies should suffice:

    sudo apt-get -y install git cmake g++ libz-dev libboost-all-dev \
      libssl-dev libreadline-dev libdb++-dev

To build and install the `keyid` program, run the following commands:

    git clone https://github.com/keyid/keyid
    cd keyid
    git submodule update --init
    cmake .
    make -j
    sudo install programs/client/bitshares_client /usr/local/bin/keyid

### Building and installing the Qt app

To build the Qt application, some extra steps are required:

    sudo apt-get install qt5-default libqt5webkit5-dev
    cmake -DINCLUDE_QT_WALLET=ON .
    make package
    sudo install programs/qt_wallet/BitSharesXT /usr/local/bin/KeyID

Note that we installed the command-line client as `keyid`, while the Qt app is
installed under the (capitalized) name `KeyID`.

To make the app capable of handling `keyid:` URIs:

    sudo mkdir -p /usr/local/share/{applications,icons}
    sudo install -m644 programs/qt_wallet/BitSharesXT.desktop \
      /usr/local/share/applications/KeyID.desktop
    sudo install -m644 programs/qt_wallet/images/qtapp80.png \
      /usr/local/share/icons/KeyID.png
