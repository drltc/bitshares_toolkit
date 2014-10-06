KeyID (a.k.a. DNS DAC)
======================

Powered by the BitShares DAC toolkit, KeyID is a decentralized autonomous
company based on delegated proof-of-stake (DPoS) blockchain technology.
See <http://keyid.info> for more information.


Running KeyID using Docker
--------------------------

The easiest way to run the KeyID client in a command-line environment is by
using the official Docker images automatically built from this repository:

    $ docker run -it keyid/keyid

To run a KeyID node in the background and expose the HTTP API on port 5044:

    $ docker run -d --name=keyid -p 5044:80 -v /var/local/lib/keyid:/root \
        keyid/keyid --daemon --httpdendpoint=127.0.0.1:80 \
          --rpcuser=user --rpcpass=pass

See also <https://github.com/dbrock/btsc> for a convenient way to communicate
with a KeyID node running in the background.


Building and installing KeyID manually
--------------------------------------

    $ git submodule update --init
    $ cmake . && make -j
    $ install programs/client/bitshares_client /usr/local/bin/keyid
