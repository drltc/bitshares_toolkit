#!/bin/sh

set -e

if [ ! -e bts_tests ]
then
    cd tests/drltc_tests/py
fi

VE_BASE=ve

if [ ! -e "$VE_BASE" ]
then
    virtualenv -p $(readlink -f $(which python3)) "$VE_BASE"
    . "$VE_BASE/bin/activate"
    pip install -e .
else
    . "$VE_BASE/bin/activate"
fi

cd ../../..
python3 -m bts_tests "$@"
