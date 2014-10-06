FROM ubuntu:14.04
ENV DEBIAN_FRONTEND noninteractive
RUN apt-get update && apt-get -y upgrade
RUN apt-get -y install git cmake g++ libz-dev libboost-all-dev \
  libssl-dev libreadline-dev libdb++-dev
ENTRYPOINT ["keyid"]
ADD . src
RUN (cd src && cmake . && make -j && \
  install programs/client/bitshares_client /usr/local/bin/keyid) && \
  rm -rf src
