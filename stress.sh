#!/bin/sh

#(cd src/deps/projectm; make -j 4) || exit 1
# make -j 4 || exit 1
while true; do
    sh -c "(cd ~/Rack/; ./Rack /home/korfuri/Code/Rack-SDK/plugins/Milkrack/testpatch_stress.vcv)" &
    sleep .3
    wmctrl -c :ACTIVE:
    wait
    sleep .3
done

