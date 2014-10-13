FROM ubuntu:14.04
ENV DEBIAN_FRONTEND noninteractive
RUN apt-get update && apt-get -y upgrade
RUN apt-get -y install git cmake g++ libz-dev libboost-all-dev \
  libssl-dev libreadline-dev libdb++-dev
ADD . src
RUN (cd src && cmake . && make -j && \
  install programs/client/bitshares_client /usr/local/bin/keyid) && \
  rm -rf src
ENTRYPOINT ["keyid", "--httpdendpoint=0.0.0.0:80", "--data-dir=/data"]
EXPOSE 80 1791
VOLUME ["/data"]
