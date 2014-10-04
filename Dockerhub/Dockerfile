FROM ubuntu:14.04
ENV DEBIAN_FRONTEND noninteractive
RUN apt-get update && apt-get -y upgrade
RUN apt-get -y install git cmake g++ libz-dev libboost-all-dev \
  libssl-dev libreadline-dev libdb++-dev
CMD ["keyid"]
RUN (git clone https://github.com/keyid/keyid src && cd src && \
  git checkout develop && \
  git submodule update --init && cmake . && make -j && \
  install programs/client/bitshares_client /usr/bin/keyid) && \
  rm -rf src
