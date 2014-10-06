FROM ubuntu:14.04
ENV DEBIAN_FRONTEND noninteractive
RUN apt-get update && apt-get -y upgrade
RUN apt-get -y install git cmake g++ libz-dev libboost-all-dev \
  libssl-dev libreadline-dev libdb++-dev
ENTRYPOINT ["keyid"]
ADD . src
RUN (cd src && cmake . && \
  make -j`grep ^processor /proc/cpuinfo | wc -l` && \
  install programs/client/bitshares_client /usr/bin/keyid) && \
  rm -rf src
