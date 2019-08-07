cp libusbwrap.* ~/20140524/makestuff/libs/libusbwrap/
cd ~/20140524/makestuff/libs/libusbwrap
make deps
cd -
cp libfpgalink.* ~/20140524/makestuff/libs/libfpgalink
cd ~/20140524/makestuff/libs/libfpgalink
cd -
cp main.c ~/20140524/makestuff/apps/flcli/
cd ~/20140524/makestuff/apps/flcli
make deps

