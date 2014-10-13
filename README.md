KeyID (a.k.a. DNS DAC)
======================

Powered by the BitShares DAC toolkit, KeyID is a decentralized autonomous
company based on delegated proof-of-stake (DPoS) blockchain technology.

See <http://keyid.info> for more information.


Running KeyID using Docker
--------------------------

The easiest way to run the KeyID client in a command-line environment is by
using the Docker images automatically built from this repository:

    $ docker run -it keyid/keyid

Data is stored in the `/root` directory in the container, so if you want to
store data on the host file system, you can mount that directory using `-v`:

    $ docker run -it -v /var/local/lib/keyid:/root keyid/keyid

To run a KeyID node in the background, you should do something like this:

    $ docker run --name=keyid -d keyid/keyid --daemon

Seed nodes need to expose port 1791 so that others can connect to them:

    $ docker run --name=keyid -d -p 1791:1791 keyid/keyid --daemon

To run a seed node outside of Docker, no special configuration is necessary
except to make sure that your firewall allows incoming connections to 1791.


Building and installing KeyID manually
--------------------------------------

Of course, there is no need to use Docker if you prefer not to.  To build and
install KeyID directly onto your machine, run the following commands:

    $ git submodule update --init
    $ cmake .
    $ make -j
    $ install programs/client/bitshares_client /usr/local/bin/keyid
